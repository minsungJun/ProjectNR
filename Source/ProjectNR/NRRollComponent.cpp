// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRRollComponent.h"
#include "NRAnimUtils.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/HealthComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "NRAimComponent.h"
#include "MoverComponent.h"
#include "MoverDataModelTypes.h"
#include "MoverSimulationTypes.h"
#include "NRMeleeComponent.h"
#include "NRMoveInput.h"
#include "NRStaminaComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRRoll, Log, All);

// --- FNRRollLayeredMove ------------------------------------------------------------

bool FNRRollLayeredMove::GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep,
	const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove)
{
	// On the game thread the movement mode itself says whether we are grounded, which is
	// current for this step. The blackboard floor is only a fallback.
	bool bOnGround = true;
	if (const UCharacterMoverComponent* CharacterMover = Cast<UCharacterMoverComponent>(MoverComp))
	{
		bOnGround = CharacterMover->IsOnGround();
	}
	else if (SimBlackboard)
	{
		FFloorCheckResult Floor;
		bOnGround = SimBlackboard->TryGet(CommonBlackboard::LastFloorResult, Floor) && Floor.IsWalkableFloor();
	}

	return GenerateRollMove(StartState, TimeStep, bOnGround, OutProposedMove);
}

bool FNRRollLayeredMove::GenerateMove_Async(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep,
	UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove)
{
	FFloorCheckResult Floor;
	const bool bOnGround = SimBlackboard && SimBlackboard->TryGet(CommonBlackboard::LastFloorResult, Floor) && Floor.IsWalkableFloor();
	return GenerateRollMove(StartState, TimeStep, bOnGround, OutProposedMove);
}

bool FNRRollLayeredMove::GenerateRollMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, bool bOnGround, FProposedMove& OutProposedMove) const
{
	if (DurationMs <= 0.0f)
	{
		return false;
	}

	// Sample the middle of the step, so summing speed x step reproduces the curve's area and
	// the distance comes out right regardless of frame rate.
	const double ElapsedMs = (TimeStep.BaseSimTimeMs - StartSimTimeMs) + TimeStep.StepMs * 0.5;
	const float T = FMath::Clamp(static_cast<float>(ElapsedMs / DurationMs), 0.0f, 1.0f);

	const float DurationSeconds = DurationMs * 0.001f;
	const float AverageSpeed = Distance / DurationSeconds;

	// If the character was already going faster than the roll's average, surging would mean
	// slowing down first. Hold the entry speed instead; the roll then covers a little more.
	const float PeakSpeed = FMath::Max(ExitSpeed + 1.5f * (AverageSpeed - ExitSpeed), ExitSpeed);
	float Speed = ExitSpeed + (PeakSpeed - ExitSpeed) * (1.0f - T * T);

	if (!bOnGround)
	{
		Speed = FMath::Max(Speed * AirborneSpeedScale, ExitSpeed);
	}

	// Horizontal only. The mix mode leaves vertical velocity to the movement mode, so whatever
	// Z is proposed here is ignored anyway; zero keeps that intent visible.
	OutProposedMove.LinearVelocity = Direction * Speed;

	// The mix mode overrides angular velocity as well, so facing is this move's to set: hold
	// it (zero) or turn towards the roll direction, as fast as TurnRate allows this step.
	OutProposedMove.AngularVelocityDegrees = FVector::ZeroVector;
	if (bTurnToDirection && TimeStep.StepMs > 0.0f)
	{
		if (const FMoverDefaultSyncState* SyncState = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
		{
			const float StepSeconds = TimeStep.StepMs * 0.001f;
			const float CurrentYaw = SyncState->GetOrientation_WorldSpace().Yaw;
			const float YawDelta = FRotator::NormalizeAxis(Direction.Rotation().Yaw - CurrentYaw);
			const float MaxStep = TurnRate * StepSeconds;
			OutProposedMove.AngularVelocityDegrees.Z = FMath::Clamp(YawDelta, -MaxStep, MaxStep) / StepSeconds;
		}
	}
	return true;
}

FLayeredMoveBase* FNRRollLayeredMove::Clone() const
{
	return new FNRRollLayeredMove(*this);
}

void FNRRollLayeredMove::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	Ar << Direction;
	Ar << Distance;
	Ar << ExitSpeed;
	Ar << AirborneSpeedScale;
	Ar << bTurnToDirection;
	Ar << TurnRate;
}

UScriptStruct* FNRRollLayeredMove::GetScriptStruct() const
{
	return FNRRollLayeredMove::StaticStruct();
}

FString FNRRollLayeredMove::ToSimpleString() const
{
	return TEXT("NRRoll");
}

void FNRRollLayeredMove::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}

// --- UNRRollComponent --------------------------------------------------------------

UNRRollComponent::UNRRollComponent()
{
	// Rolls start on input and end on the montage's own event. The tick only runs while a
	// diagonal roll's mesh offset is turning in or out.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UNRRollComponent::BeginPlay()
{
	Super::BeginPlay();

	// One montage per direction, in ENRRollDirection order.
	const TArray<UAnimSequenceBase*> Sequences = { RollForward, RollBackward, RollLeft, RollRight };
	RollMontages.Reset();
	for (UAnimSequenceBase* Sequence : Sequences)
	{
		RollMontages.Add(Sequence
			? UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence, SlotName, BlendInTime, BlendOutTime, PlayRate)
			: nullptr);
	}

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		BindInput();
		Pawn->ReceiveRestartedDelegate.AddDynamic(this, &UNRRollComponent::OnPawnRestarted);
	}
}

void UNRRollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &UNRRollComponent::OnPawnRestarted);
	}

	Super::EndPlay(EndPlayReason);
}

void UNRRollComponent::OnPawnRestarted(APawn* Pawn)
{
	BindInput();
}

void UNRRollComponent::BindInput()
{
	if (bInputBound || !RollKey.IsValid())
	{
		return;
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->InputComponent)
	{
		return;
	}

	// Bound directly, like the weapon keys, so adding the component is the whole setup.
	Pawn->InputComponent->BindKey(RollKey, IE_Pressed, this, &UNRRollComponent::OnRollInput);
	bInputBound = true;
}

void UNRRollComponent::OnRollInput()
{
	TryRoll();
}

ENRRollDirection UNRRollComponent::GetRollDirectionFromInput() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return ENRRollDirection::Forward;
	}

	// Input, not velocity: a roll pressed while turning should go where the stick points,
	// not where momentum still carries the body.
	const FVector Intent = NRMoveInput::Read(Owner);
	if (Intent.Size() < InputDeadzone)
	{
		return ENRRollDirection::Forward;
	}

	// Snap to the nearest of the four axes in the character's own frame. There are no
	// diagonal clips, so the choice is which side of 45 degrees the input falls on.
	const FVector Local = Owner->GetActorRotation().UnrotateVector(Intent.GetSafeNormal());
	if (FMath::Abs(Local.X) >= FMath::Abs(Local.Y))
	{
		return Local.X >= 0.0f ? ENRRollDirection::Forward : ENRRollDirection::Backward;
	}
	return Local.Y >= 0.0f ? ENRRollDirection::Right : ENRRollDirection::Left;
}

bool UNRRollComponent::IsHoldingFacing() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	// The sample pawn keeps its input state in a Blueprint struct whose field names carry
	// GUID suffixes (WantsToStrafe_5_64E8...), so match on the authored name instead.
	const FStructProperty* InputState = FindFProperty<FStructProperty>(Owner->GetClass(), TEXT("PlayerInputState"));
	if (!InputState)
	{
		return false;
	}

	const void* StructData = InputState->ContainerPtrToValuePtr<void>(Owner);
	for (TFieldIterator<FBoolProperty> It(InputState->Struct); It; ++It)
	{
		const FString Name = It->GetAuthoredName();
		if ((Name == TEXT("WantsToStrafe") || Name == TEXT("WantsToAim")) && It->GetPropertyValue_InContainer(StructData))
		{
			return true;
		}
	}
	return false;
}

UAnimMontage* UNRRollComponent::GetMontage(ENRRollDirection Direction) const
{
	const int32 Index = static_cast<int32>(Direction);
	return RollMontages.IsValidIndex(Index) ? RollMontages[Index].Get() : nullptr;
}

bool UNRRollComponent::TryRoll()
{
	const UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner || bIsRolling || World->GetTimeSeconds() - LastRollEndTime < Cooldown)
	{
		return false;
	}

	if (const UHealthComponent* Health = Owner->FindComponentByClass<UHealthComponent>())
	{
		if (Health->IsDead())
		{
			return false;
		}
	}

	// Checked now, paid once the roll has actually started.
	UNRStaminaComponent* Stamina = Owner->FindComponentByClass<UNRStaminaComponent>();
	if (Stamina && !Stamina->HasStamina(StaminaCost))
	{
		return false;
	}

	// Ground only. The layered move leaves vertical velocity alone, so starting a roll in the
	// air would not float - but a roll animation in mid-fall reads as a bug.
	UMoverComponent* Mover = Owner->FindComponentByClass<UMoverComponent>();
	const UCharacterMoverComponent* CharacterMover = Cast<UCharacterMoverComponent>(Mover);
	if (!Mover || (CharacterMover && !CharacterMover->IsOnGround()))
	{
		return false;
	}

	// A montage already owning the body - a vault, a climb - wins over a roll. A melee attack
	// is the exception: rolling out of a swing is the point of having both.
	UNRMeleeComponent* Melee = Owner->FindComponentByClass<UNRMeleeComponent>();
	const bool bCancelsAttack = Melee && Melee->IsAttacking();
	const USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	// The reload (an upper-body montage) does not count: it carries on over the roll's legs, or the roll cuts it.
	if (!AnimInstance || (NRAnim::IsBodyMontagePlaying(AnimInstance) && !bCancelsAttack))
	{
		return false;
	}

	// Two rules, see IsHoldingFacing. Strafing: pick the clip relative to the fixed facing.
	// Otherwise: roll along the input itself with the forward clip, turning to face it.
	// Rolling out of a swing keeps facing the target too: S mid-attack should be a back-step,
	// not a turn and a forward roll away.
	const bool bHoldingFacing = IsHoldingFacing() || bCancelsAttack;
	const FVector InputDirection = NRMoveInput::Read(Owner);
	const bool bHasInput = InputDirection.Size() >= InputDeadzone;

	// Holding facing: the input angle relative to the facing, snapped to 45 degrees for eight
	// directions (or 90 for four). The clip is the nearest of the four; on a diagonal that is
	// forward or backward, and the 45 degrees it is off by become a mesh turn below.
	float SnappedAngle = 0.0f;
	float ClipAngle = 0.0f;
	ENRRollDirection Direction = ENRRollDirection::Forward;
	if (bHoldingFacing && bHasInput)
	{
		const FVector Local = Owner->GetActorRotation().UnrotateVector(InputDirection.GetSafeNormal());
		const float InputAngle = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
		const float Step = bEightDirections ? 45.0f : 90.0f;
		SnappedAngle = FRotator::NormalizeAxis(FMath::RoundToFloat(InputAngle / Step) * Step);

		const float AbsAngle = FMath::Abs(SnappedAngle);
		if (AbsAngle <= 45.0f)
		{
			Direction = ENRRollDirection::Forward;
			ClipAngle = 0.0f;
		}
		else if (AbsAngle >= 135.0f)
		{
			Direction = ENRRollDirection::Backward;
			ClipAngle = 180.0f;
		}
		else
		{
			Direction = SnappedAngle > 0.0f ? ENRRollDirection::Right : ENRRollDirection::Left;
			ClipAngle = SnappedAngle > 0.0f ? 90.0f : -90.0f;
		}
	}
	UAnimMontage* Montage = GetMontage(Direction);
	if (!Montage)
	{
		UE_LOG(LogNRRoll, Warning, TEXT("%s has no roll animation for direction %d"),
			*GetNameSafe(Owner), static_cast<int32>(Direction));
		return false;
	}

	// Only now, with a roll known to be possible: cancel the attack, which also drops the
	// lunge's root motion move so it cannot override the roll's own push.
	if (bCancelsAttack)
	{
		Melee->CancelAttack();
	}

	// Precision aim is first person; tumbling with the camera at eye height is disorienting,
	// so a roll drops back to third person. Shoulder aim is a held button and stays.
	if (UNRAimComponent* Aim = Owner->FindComponentByClass<UNRAimComponent>())
	{
		if (Aim->IsPrecisionAiming())
		{
			Aim->ExitAim();
		}
	}

	// A roll out of a slide ends the slide. The sample slides while crouched and fast, and
	// leaves the slide when no longer crouched (BP_MovementTransition_FromSlide), so standing
	// up is what ends it - and keeps it from starting again at the roll's speed, which a plain
	// switch to walking would not (BP_MovementTransition_ToSlide fires on crouch + speed).
	// The crouch itself is re-applied every sim tick from PlayerInputState.WantsToCrouch
	// (On_PreSimulateTick), which C toggles, so that is what must be cleared; UnCrouch alone
	// was undone on the next tick. The sample does the same when it starts falling.
	if (UCharacterMoverComponent* SlidingMover = Cast<UCharacterMoverComponent>(Mover))
	{
		if (SlidingMover->GetMovementModeName() == TEXT("sliding"))
		{
			if (const FStructProperty* InputState = FindFProperty<FStructProperty>(Owner->GetClass(), TEXT("PlayerInputState")))
			{
				void* StructData = InputState->ContainerPtrToValuePtr<void>(Owner);
				for (TFieldIterator<FBoolProperty> It(InputState->Struct); It; ++It)
				{
					if (It->GetAuthoredName() == TEXT("WantsToCrouch"))
					{
						It->SetPropertyValue_InContainer(StructData, false);
						break;
					}
				}
			}
			SlidingMover->UnCrouch();
			SlidingMover->QueueNextMode(TEXT("walking"));
			UE_LOG(LogNRRoll, Log, TEXT("%s rolls out of a slide"), *GetNameSafe(Owner));
		}
	}

	// A plain montage play: the clip has no root motion, so there is nothing for Mover to take
	// from it. PlayRate is already baked into the dynamic montage.
	if (AnimInstance->Montage_Play(Montage, 1.0f) <= 0.0f)
	{
		UE_LOG(LogNRRoll, Warning, TEXT("%s could not start %s"), *GetNameSafe(Owner), *GetNameSafe(Montage));
		return false;
	}

	if (Stamina)
	{
		Stamina->Consume(StaminaCost);
	}

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UNRRollComponent::OnRollMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, Montage);

	// Holding facing moves along the snapped angle; otherwise along the input itself.
	const FVector WorldDirection = (!bHoldingFacing && bHasInput)
		? InputDirection.GetSafeNormal()
		: FRotator(0.0f, Owner->GetActorRotation().Yaw + SnappedAngle, 0.0f).Vector();

	// On a diagonal, turn the mesh the rest of the way so the clip rolls along the movement.
	SetMeshYawOffset(bHoldingFacing ? FRotator::NormalizeAxis(SnappedAngle - ClipAngle) : 0.0f);

	// The montage length already accounts for its play rate, so this is real seconds.
	const float Duration = FMath::Max(Montage->GetPlayLength(), KINDA_SMALL_NUMBER);

	TSharedPtr<FNRRollLayeredMove> RollMove = MakeShared<FNRRollLayeredMove>();
	RollMove->Direction = WorldDirection;
	RollMove->Distance = RollDistance;
	// Only the part of the current motion that points along the roll carries through. Rolling
	// back out of a sprint should not inherit the sprint.
	const FVector EntryVelocity(Owner->GetVelocity().X, Owner->GetVelocity().Y, 0.0f);
	RollMove->ExitSpeed = FMath::Max(0.0f, FVector::DotProduct(EntryVelocity, WorldDirection));
	RollMove->AirborneSpeedScale = AirborneSpeedScale;
	RollMove->bTurnToDirection = !bHoldingFacing;
	RollMove->TurnRate = TurnRate;
	RollMove->DurationMs = Duration * 1000.0f;
	// Horizontal motion and facing belong to the roll; up and down stay with the movement
	// mode, which is what lets the roll follow slopes and drop off ledges instead of gliding.
	RollMove->MixMode = EMoveMixMode::OverrideAllExceptVerticalVelocity;
	// FinishVelocitySettings stays on its default. Mover's residual-velocity handling
	// (FLayeredMoveGroup::GatherResidualVelocitySettings) has no branch for this mix mode and
	// hits check(0) for both SetVelocity and ClampVelocity. The move's own speed curve ends
	// at zero instead, so there is nothing left over to hand on.
	Mover->QueueLayeredMove(RollMove);

	bIsRolling = true;

	UE_LOG(LogNRRoll, Log, TEXT("%s rolls %s (%s, relative %+.0f deg, mesh offset %+.0f, heading %.0f deg): %.0fcm over %.2fs, entry/exit speed %.0f"), *GetNameSafe(Owner),
		*StaticEnum<ENRRollDirection>()->GetNameStringByValue(static_cast<int64>(Direction)),
		bHoldingFacing ? TEXT("holding facing") : TEXT("turning to input"), SnappedAngle, MeshYawOffsetTarget, WorldDirection.Rotation().Yaw,
		RollDistance, Duration, RollMove->ExitSpeed);

	// Diagnostic kept for tuning: how far the actor really went, which differs from
	// RollDistance on slopes, against walls and at ledges.
	const FVector StartLocation = Owner->GetActorLocation();
	FTimerHandle ReportHandle;
	TWeakObjectPtr<AActor> WeakOwner(Owner);
	World->GetTimerManager().SetTimer(ReportHandle, FTimerDelegate::CreateLambda([WeakOwner, StartLocation]()
	{
		if (const AActor* O = WeakOwner.Get())
		{
			UE_LOG(LogNRRoll, Log, TEXT("%s roll moved actor %.1fcm (2D)"), *GetNameSafe(O),
				FVector::Dist2D(StartLocation, O->GetActorLocation()));
		}
	}), Duration + 0.1f, false);

	return true;
}

void UNRRollComponent::OnRollMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	SetMeshYawOffset(0.0f);

	// Completed and interrupted both land here. Either way the body is free again. An
	// interrupted roll's layered move still runs out its duration; it is under half a second.
	bIsRolling = false;
	if (const UWorld* World = GetWorld())
	{
		LastRollEndTime = World->GetTimeSeconds();
	}
}

void UNRRollComponent::SetMeshYawOffset(float TargetDegrees)
{
	USkeletalMeshComponent* Mesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (!Mesh)
	{
		return;
	}

	// Remember the mesh's own rotation the first time, so turning back lands on it exactly
	// rather than on an accumulated approximation.
	if (!bMeshOffsetActive)
	{
		if (FMath::IsNearlyZero(TargetDegrees))
		{
			return;
		}
		MeshBaseRotation = Mesh->GetRelativeRotation();
		MeshYawOffsetCurrent = 0.0f;
		bMeshOffsetActive = true;
	}

	MeshYawOffsetTarget = TargetDegrees;
	SetComponentTickEnabled(true);
}

void UNRRollComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USkeletalMeshComponent* Mesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (!Mesh || !bMeshOffsetActive)
	{
		SetComponentTickEnabled(false);
		return;
	}

	MeshYawOffsetCurrent = FMath::FInterpConstantTo(MeshYawOffsetCurrent, MeshYawOffsetTarget, DeltaTime, MeshTurnRate);
	FRotator Rotation = MeshBaseRotation;
	Rotation.Yaw += MeshYawOffsetCurrent;
	Mesh->SetRelativeRotation(Rotation);

	if (FMath::IsNearlyEqual(MeshYawOffsetCurrent, MeshYawOffsetTarget))
	{
		if (FMath::IsNearlyZero(MeshYawOffsetTarget))
		{
			Mesh->SetRelativeRotation(MeshBaseRotation);
			bMeshOffsetActive = false;
		}
		SetComponentTickEnabled(false);
	}
}
