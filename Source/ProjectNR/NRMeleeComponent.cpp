// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRMeleeComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "MotionWarpingComponent.h"
#include "MoverDataModelTypes.h"
#include "MoverSimulationTypes.h"
#include "NRAttackInput.h"
#include "NRMoveInput.h"
#include "NRWeaponComponent.h"
#include "MoverComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRMelee, Log, All);

// --- FNRMeleeRootMotionMove --------------------------------------------------------

bool FNRMeleeRootMotionMove::GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep,
	const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove)
{
	// Ends with the montage, the same rule Mover's own anim root motion move follows.
	const USkeletalMeshComponent* Mesh = MoverComp ? Cast<USkeletalMeshComponent>(MoverComp->GetPrimaryVisualComponent()) : nullptr;
	const UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Montage || !AnimInstance || !AnimInstance->Montage_IsPlaying(Montage))
	{
		DurationMs = 0.0f;
		return false;
	}

	const FMoverDefaultSyncState* SyncState = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	const float DeltaSeconds = TimeStep.StepMs * 0.001f;
	if (!SyncState || DeltaSeconds <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// Montage time for this step, from elapsed simulation time.
	const double Elapsed = (TimeStep.BaseSimTimeMs - StartSimTimeMs) * 0.001;
	const float From = StartPosition + static_cast<float>(Elapsed) * PlayRate;
	const float To = From + DeltaSeconds * PlayRate;

	const FTransform LocalRootMotion = UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Montage, From, To);
	const FTransform SimActorTransform(SyncState->GetOrientation_WorldSpace().Quaternion(), SyncState->GetLocation_WorldSpace());
	const FTransform WorldRootMotion = MoverComp->ConvertLocalRootMotionToWorld(LocalRootMotion, DeltaSeconds, &SimActorTransform);

	OutProposedMove = FProposedMove();
	OutProposedMove.MixMode = MixMode;
	OutProposedMove.LinearVelocity = WorldRootMotion.GetTranslation() / DeltaSeconds;
	const FQuat RootRotation = WorldRootMotion.GetRotation().GetShortestArcWith(FQuat::Identity);
	OutProposedMove.AngularVelocityDegrees = FMath::RadiansToDegrees(RootRotation.ToRotationVector() / DeltaSeconds);

	// The start-of-attack turn, on top of whatever turn the clip makes (none, for these).
	// Spent as "angle due by the end of this step minus angle due by its start", so the total
	// comes out at exactly TurnDegrees whatever the step size.
	if (!FMath::IsNearlyZero(TurnDegrees) && TurnSeconds > 0.0f)
	{
		const double StepStart = Elapsed;
		const double StepEnd = Elapsed + DeltaSeconds;
		const float DueBefore = TurnDegrees * FMath::Clamp(static_cast<float>(StepStart / TurnSeconds), 0.0f, 1.0f);
		const float DueAfter = TurnDegrees * FMath::Clamp(static_cast<float>(StepEnd / TurnSeconds), 0.0f, 1.0f);
		OutProposedMove.AngularVelocityDegrees.Z += (DueAfter - DueBefore) / DeltaSeconds;
	}
	return true;
}

bool FNRMeleeRootMotionMove::HasGameplayTag(FGameplayTag TagToFind, bool bExactMatch) const
{
	const FGameplayTag MontageTag = Mover_AnimRootMotion_Montage.GetTag();
	return (bExactMatch ? MontageTag.MatchesTagExact(TagToFind) : MontageTag.MatchesTag(TagToFind))
		|| Super::HasGameplayTag(TagToFind, bExactMatch);
}

FLayeredMoveBase* FNRMeleeRootMotionMove::Clone() const
{
	return new FNRMeleeRootMotionMove(*this);
}

void FNRMeleeRootMotionMove::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);
	Ar << Montage;
	Ar << StartPosition;
	Ar << PlayRate;
	Ar << TurnDegrees;
	Ar << TurnSeconds;
}

UScriptStruct* FNRMeleeRootMotionMove::GetScriptStruct() const
{
	return FNRMeleeRootMotionMove::StaticStruct();
}

FString FNRMeleeRootMotionMove::ToSimpleString() const
{
	return TEXT("NRMeleeRootMotion");
}

void FNRMeleeRootMotionMove::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(Montage);
}

// --- UNRMeleeComponent -------------------------------------------------------------

UNRMeleeComponent::UNRMeleeComponent()
{
	// Ticks only while an attack plays, to run the hit window and the combo handover.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UNRMeleeComponent::BeginPlay()
{
	Super::BeginPlay();

	LightMontages.Reset();
	for (const FNRMeleeAttack& Attack : LightCombo)
	{
		LightMontages.Add(Attack.Animation
			? UAnimMontage::CreateSlotAnimationAsDynamicMontage(Attack.Animation, SlotName, BlendInTime, BlendOutTime)
			: nullptr);
	}
	HeavyMontage = Heavy.Animation
		? UAnimMontage::CreateSlotAnimationAsDynamicMontage(Heavy.Animation, SlotName, BlendInTime, BlendOutTime)
		: nullptr;

	BuildInput();

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		BindInput();
		Pawn->ReceiveRestartedDelegate.AddDynamic(this, &UNRMeleeComponent::OnPawnRestarted);
	}
}

void UNRMeleeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &UNRMeleeComponent::OnPawnRestarted);
	}

	bMeleeEquipped = false;
	UpdateMappingContext();

	Super::EndPlay(EndPlayReason);
}

void UNRMeleeComponent::OnPawnRestarted(APawn* Pawn)
{
	BindInput();
	// Possibly a different controller: UpdateMappingContext moves the context over if so.
	UpdateMappingContext();
}

// --- Input -------------------------------------------------------------------------

void UNRMeleeComponent::BuildInput()
{
	// Built here rather than as assets: two buttons, no modifiers, and nothing a designer
	// needs to rebind yet. Boolean actions with no triggers fire Started on press.
	LightAction = NewObject<UInputAction>(this, TEXT("IA_NRMeleeLight"));
	HeavyAction = NewObject<UInputAction>(this, TEXT("IA_NRMeleeHeavy"));

	MeleeContext = NewObject<UInputMappingContext>(this, TEXT("IMC_NRMelee"));
	MeleeContext->MapKey(LightAction, EKeys::LeftMouseButton);
	MeleeContext->MapKey(HeavyAction, EKeys::RightMouseButton);
}

void UNRMeleeComponent::BindInput()
{
	if (bInputBound)
	{
		return;
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	UEnhancedInputComponent* Input = Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
	if (!Input)
	{
		return;
	}

	Input->BindAction(LightAction, ETriggerEvent::Started, this, &UNRMeleeComponent::OnLightInput);
	Input->BindAction(HeavyAction, ETriggerEvent::Started, this, &UNRMeleeComponent::OnHeavyInput);
	bInputBound = true;
}

void UNRMeleeComponent::UpdateMappingContext()
{
	if (!MeleeContext)
	{
		return;
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Current = PC && PC->GetLocalPlayer()
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())
		: nullptr;

	// Where the context should be now: the current player's subsystem while melee is
	// equipped, nowhere otherwise - including when this pawn has no controller at all.
	UEnhancedInputLocalPlayerSubsystem* Wanted = bMeleeEquipped ? Current : nullptr;

	if (ContextSubsystem.Get() == Wanted)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Previous = ContextSubsystem.Get())
	{
		Previous->RemoveMappingContext(MeleeContext);
	}
	ContextSubsystem = Wanted;
	if (Wanted)
	{
		Wanted->AddMappingContext(MeleeContext, InputPriority);
	}

	UE_LOG(LogNRMelee, Log, TEXT("%s melee input %s"), *GetNameSafe(GetOwner()),
		Wanted ? TEXT("on (mouse buttons taken)") : TEXT("off"));
}

void UNRMeleeComponent::SetMeleeEquipped(bool bEquipped)
{
	if (bMeleeEquipped == bEquipped)
	{
		return;
	}

	bMeleeEquipped = bEquipped;
	UpdateMappingContext();
	// Ticks for as long as melee is in hand, so holding the button can start an attack.
	SetComponentTickEnabled(bMeleeEquipped || IsAttacking());
}

void UNRMeleeComponent::OnLightInput(const FInputActionValue& Value)
{
	LightAttack();
}

void UNRMeleeComponent::OnHeavyInput(const FInputActionValue& Value)
{
	HeavyAttack();
}

// --- Attacks -----------------------------------------------------------------------

const FNRMeleeAttack* UNRMeleeComponent::GetCurrentAttack() const
{
	if (!CurrentMontage)
	{
		return nullptr;
	}
	if (CurrentComboIndex == INDEX_NONE)
	{
		return &Heavy;
	}
	return LightCombo.IsValidIndex(CurrentComboIndex) ? &LightCombo[CurrentComboIndex] : nullptr;
}

int32 UNRMeleeComponent::GetNextComboIndex() const
{
	if (CurrentComboIndex == INDEX_NONE || LightCombo.Num() == 0)
	{
		return INDEX_NONE;
	}
	const int32 Next = CurrentComboIndex + 1;
	if (LightCombo.IsValidIndex(Next))
	{
		return Next;
	}
	return bLoopLightCombo ? 0 : INDEX_NONE;
}

bool UNRMeleeComponent::CanStartAttack() const
{
	const AActor* Owner = GetOwner();
	if (!bMeleeEquipped || !Owner)
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

	if (const UCharacterMoverComponent* Mover = Owner->FindComponentByClass<UCharacterMoverComponent>())
	{
		if (!Mover->IsOnGround())
		{
			return false;
		}
	}

	// Something else owns the body - a roll, a vault. Our own attacks are handled by the
	// combo logic before this is asked.
	const USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
	const UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	return AnimInstance && !AnimInstance->IsAnyMontagePlaying();
}

void UNRMeleeComponent::LightAttack()
{
	const UWorld* World = GetWorld();
	if (!World || !bMeleeEquipped || LightCombo.Num() == 0)
	{
		return;
	}

	if (IsAttacking())
	{
		// Mid-combo: remember the press if the window is open and there is a next hit. The
		// window closes at ChainTime - past it the body has already left the pose the next
		// clip starts from, so a late press lets this hit play out instead of snapping.
		const FNRMeleeAttack* Current = GetCurrentAttack();
		const bool bHasNext = GetNextComboIndex() != INDEX_NONE;
		const float Elapsed = GetAttackClipTime();
		if (Current && bHasNext && Elapsed >= Current->ComboInputStart && Elapsed <= Current->ChainTime)
		{
			bComboQueued = true;
			// A later click without a side key does not erase an earlier one's.
			const float Side = ReadSideInput();
			if (Side != 0.0f)
			{
				QueuedSide = Side;
			}
		}
		return;
	}

	if (CanStartAttack())
	{
		StartAttack(LightCombo[0], LightMontages.IsValidIndex(0) ? LightMontages[0].Get() : nullptr, 0, ReadSideInput());
	}
	else
	{
		UE_LOG(LogNRMelee, Log, TEXT("%s light attack refused (not on ground, dead, or another montage playing)"),
			*GetNameSafe(GetOwner()));
	}
}

void UNRMeleeComponent::HeavyAttack()
{
	if (IsAttacking())
	{
		return;
	}
	if (CanStartAttack())
	{
		StartAttack(Heavy, HeavyMontage, INDEX_NONE, ReadSideInput());
	}
	else
	{
		UE_LOG(LogNRMelee, Log, TEXT("%s heavy attack refused (not on ground, dead, or another montage playing)"),
			*GetNameSafe(GetOwner()));
	}
}

float UNRMeleeComponent::GetAttackClipTime() const
{
	const UWorld* World = GetWorld();
	return World ? (World->GetTimeSeconds() - AttackStartTime) * AttackPlayRate : 0.0f;
}

FNRWeaponRuntimeStats UNRMeleeComponent::ReadMeleeStats() const
{
	const UNRWeaponComponent* Weapons = GetOwner() ? GetOwner()->FindComponentByClass<UNRWeaponComponent>() : nullptr;
	return Weapons ? Weapons->GetRuntimeStats(ENRWeaponId::Melee) : FNRWeaponRuntimeStats();
}

float UNRMeleeComponent::ReadSideInput() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return 0.0f;
	}
	const FVector Input = NRMoveInput::Read(Owner);
	const float Sideways = FVector::DotProduct(Input, Owner->GetActorRightVector().GetSafeNormal2D());
	return FMath::Abs(Sideways) >= SideInputThreshold ? FMath::Sign(Sideways) : 0.0f;
}

bool UNRMeleeComponent::StartAttack(const FNRMeleeAttack& Attack, UAnimMontage* Montage, int32 ComboIndex, float PressedSide)
{
	AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	UMoverComponent* Mover = Owner ? Owner->FindComponentByClass<UMoverComponent>() : nullptr;
	const USkeletalMeshComponent* Mesh = Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Montage || !Mover || !AnimInstance || !World)
	{
		UE_LOG(LogNRMelee, Warning, TEXT("%s cannot attack: missing %s"), *GetNameSafe(Owner),
			!Montage ? TEXT("animation") : !Mover ? TEXT("Mover") : TEXT("anim instance"));
		return false;
	}

	// Chaining replaces the playing montage. Clearing CurrentMontage first means its end event,
	// which fires during the call below, is recognised as stale and ignored.
	CurrentMontage = nullptr;

	// Only one lunge at a time. When chaining, the previous hit's move would otherwise keep
	// running through its montage's blend-out - and with equal priority the older move wins.
	Mover->CancelFeaturesWithTag(Mover_AnimRootMotion_Montage);

	// Attack speed from parts, frozen for this swing. The montage, the lunge and every timing
	// window below all use this one rate, so they stay on the same frames of the clip.
	const FNRWeaponRuntimeStats Stats = ReadMeleeStats();
	const float PlayRate = FMath::Max(Stats.AttackSpeedMultiplier, 0.1f);

	// A plain play: it stops the previous combo hit with that montage's blend-out. The pose's
	// root is locked by the anim instance's root motion extraction; the capsule is moved by
	// the layered move below, which reads the same root motion from the asset.
	if (AnimInstance->Montage_Play(Montage, PlayRate) <= 0.0f || !AnimInstance->Montage_IsPlaying(Montage))
	{
		EndAttack();
		UE_LOG(LogNRMelee, Warning, TEXT("%s could not start %s"), *GetNameSafe(Owner), *GetNameSafe(Montage));
		return false;
	}

	TSharedPtr<FNRMeleeRootMotionMove> Lunge = MakeShared<FNRMeleeRootMotionMove>();
	Lunge->Montage = Montage;
	Lunge->StartPosition = AnimInstance->Montage_GetPosition(Montage);
	Lunge->PlayRate = PlayRate;
	// A or D held now, or at the click that queued this hit: turn a fixed step towards it over
	// the first moments of the attack. The click wins if both disagree - it is what was meant.
	float TurnDegrees = 0.0f;
	if (AttackTurnAngle > 0.0f)
	{
		const float Side = PressedSide != 0.0f ? PressedSide : ReadSideInput();
		TurnDegrees = Side * AttackTurnAngle;
	}
	Lunge->TurnDegrees = TurnDegrees;
	Lunge->TurnSeconds = AttackTurnTime;
	Lunge->DurationMs = Montage->GetPlayLength() / PlayRate * 1000.0f;
	// Everything but vertical velocity: the lunge and the turn are the attack's, falling and
	// ground contact stay with the movement mode.
	Lunge->MixMode = EMoveMixMode::OverrideAllExceptVerticalVelocity;
	Mover->QueueLayeredMove(Lunge);

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UNRMeleeComponent::OnAttackMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, Montage);

	CurrentMontage = Montage;
	CurrentComboIndex = ComboIndex;
	AttackStartTime = World->GetTimeSeconds();
	AttackStats = Stats;
	AttackPlayRate = PlayRate;
	bComboQueued = false;
	QueuedSide = 0.0f;
	HitThisSwing.Reset();
	SetComponentTickEnabled(true);

	// Diagnostics: what the montage says the lunge covers, whether Mover was handed root motion
	// at all, and - once the clip is over - how far the capsule really went.
	const FVector AnimTravel = Montage->ExtractRootMotionFromTrackRange(0.0f, Montage->GetPlayLength(), FAnimExtractContext()).GetTranslation();
	UE_LOG(LogNRMelee, Log, TEXT("%s attacks: %s, turn %+.0f deg, rate x%.2f | montage root motion %s, travel %.1fcm %s | anim root motion mode %d"),
		*GetNameSafe(Owner),
		ComboIndex == INDEX_NONE ? TEXT("heavy") : *FString::Printf(TEXT("light %d"), ComboIndex + 1), TurnDegrees, PlayRate,
		Montage->HasRootMotion() ? TEXT("yes") : TEXT("NO"), AnimTravel.Size(), *AnimTravel.ToCompactString(),
		static_cast<int32>(AnimInstance->RootMotionMode.GetValue()));

	const FVector StartLocation = Owner->GetActorLocation();
	TWeakObjectPtr<AActor> WeakOwner(Owner);
	FTimerHandle ReportHandle;
	GetWorld()->GetTimerManager().SetTimer(ReportHandle, FTimerDelegate::CreateLambda([WeakOwner, StartLocation]()
	{
		if (const AActor* O = WeakOwner.Get())
		{
			UE_LOG(LogNRMelee, Log, TEXT("%s attack moved actor %.1fcm (2D)"), *GetNameSafe(O),
				FVector::Dist2D(StartLocation, O->GetActorLocation()));
		}
	}), FMath::Max((Montage->GetPlayLength() - BlendOutTime) / PlayRate, 0.1f), false);
	return true;
}

void UNRMeleeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UWorld* World = GetWorld();
	const FNRMeleeAttack* Attack = GetCurrentAttack();
	const bool bHeld = NRAttackInput::IsAttackHeld(GetOwner());
	if (!World || !Attack)
	{
		// Between attacks: holding the button starts the combo again as soon as the body is
		// free - after a roll, a heavy attack, or landing. Checked first so a refusal is silent.
		if (bMeleeEquipped && bHeld && LightCombo.Num() > 0 && CanStartAttack())
		{
			LightAttack();
		}
		SetComponentTickEnabled(bMeleeEquipped || IsAttacking());
		return;
	}

	const float Elapsed = GetAttackClipTime();

	// Held through a light attack: queue the next hit the moment its window opens, as a press
	// would. With the combo looping, holding plays 1, 2, 1, 2... until the button is let go.
	if (bHeld && !bComboQueued && CurrentComboIndex != INDEX_NONE && GetNextComboIndex() != INDEX_NONE
		&& Elapsed >= Attack->ComboInputStart && Elapsed <= Attack->ChainTime)
	{
		bComboQueued = true;
		QueuedSide = ReadSideInput();
	}

	if (Elapsed >= Attack->HitStart && Elapsed <= Attack->HitEnd)
	{
		TestHits(*Attack);
	}

	// Recovery cut: nothing queued by now, so there is nothing to wait for.
	if (!bComboQueued && Attack->RecoveryEndTime > 0.0f && Elapsed >= Attack->RecoveryEndTime)
	{
		UE_LOG(LogNRMelee, Log, TEXT("%s recovery cut at %.2fs"), *GetNameSafe(GetOwner()), Elapsed);
		CancelAttack();
		return;
	}

	if (bComboQueued && Elapsed >= Attack->ChainTime)
	{
		const int32 Next = GetNextComboIndex();
		const float Side = QueuedSide;
		bComboQueued = false;
		QueuedSide = 0.0f;
		if (LightCombo.IsValidIndex(Next) && LightMontages.IsValidIndex(Next))
		{
			StartAttack(LightCombo[Next], LightMontages[Next], Next, Side);
		}
	}
}

void UNRMeleeComponent::TestHits(const FNRMeleeAttack& Attack)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	const FVector Origin = Owner->GetActorLocation();
	const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(Attack.HalfAngle));

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NRMeleeHit), false, Owner);
	World->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeSphere(Attack.Range), Params);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Other = Overlap.GetActor();
		if (!Other || Other == Owner || HitThisSwing.Contains(Other))
		{
			continue;
		}

		UHealthComponent* Health = Other->FindComponentByClass<UHealthComponent>();
		if (!Health || Health->IsDead())
		{
			continue;
		}

		// Arc test on the ground plane, the same shape the enemy's own attack uses.
		FVector ToOther = Other->GetActorLocation() - Origin;
		ToOther.Z = 0.0f;
		const FVector ToOtherDir = ToOther.GetSafeNormal();
		if (FVector::DotProduct(Forward, ToOtherDir) < CosHalfAngle)
		{
			continue;
		}

		HitThisSwing.Add(Other);
		// Rolled per target, from the stats frozen at the start of the swing.
		bool bCritical = false;
		const float Damage = AttackStats.RollDamage(Attack.Damage, bCritical);
		Health->ApplyDamage(Damage, Owner, ToOtherDir);
		UE_LOG(LogNRMelee, Log, TEXT("%s hit %s for %.1f%s"), *GetNameSafe(Owner), *GetNameSafe(Other), Damage,
			bCritical ? TEXT(" (critical)") : TEXT(""));
	}

#if ENABLE_DRAW_DEBUG
	if (bDrawDebugHits)
	{
		DrawDebugCone(World, Origin, Forward, Attack.Range, FMath::DegreesToRadians(Attack.HalfAngle),
			FMath::DegreesToRadians(10.0f), 16, FColor::Orange, false, 0.0f, 0, 1.5f);
	}
#endif
}

void UNRMeleeComponent::CancelAttack()
{
	if (!IsAttacking())
	{
		return;
	}

	AActor* Owner = GetOwner();
	UAnimMontage* Montage = CurrentMontage;

	// Clear first so the montage's end event, fired by the stop below, is ignored as stale.
	EndAttack();

	if (UMoverComponent* Mover = Owner ? Owner->FindComponentByClass<UMoverComponent>() : nullptr)
	{
		Mover->CancelFeaturesWithTag(Mover_AnimRootMotion_Montage);
	}

	if (const USkeletalMeshComponent* Mesh = Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr)
	{
		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(BlendOutTime, Montage);
		}
	}

	UE_LOG(LogNRMelee, Log, TEXT("%s attack cancelled"), *GetNameSafe(Owner));
}

void UNRMeleeComponent::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// A combo hit that was replaced by the next one ends here too; only the current one counts.
	if (Montage != CurrentMontage)
	{
		return;
	}
	EndAttack();
}

void UNRMeleeComponent::EndAttack()
{
	QueuedSide = 0.0f;
	CurrentMontage = nullptr;
	CurrentComboIndex = INDEX_NONE;
	bComboQueued = false;
	HitThisSwing.Reset();
	SetComponentTickEnabled(bMeleeEquipped);
}
