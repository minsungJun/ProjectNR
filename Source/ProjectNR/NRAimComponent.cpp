// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRAimComponent.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "MoverComponent.h"
#include "MovementMode.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "MoverDataModelTypes.h"
#include "MoverSimulationTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRAim, Log, All);

namespace
{
	bool IsOwnerDead(const AActor* Owner)
	{
		const UHealthComponent* Health = Owner ? Owner->FindComponentByClass<UHealthComponent>() : nullptr;
		return Health && Health->IsDead();
	}
}

UNRAimComponent::UNRAimComponent()
{
	// Ticks only while aiming: to keep WantsToAim set and to move the first person camera.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// After movement, so the first person camera sits where the capsule ended up this frame.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UNRAimComponent::BeginPlay()
{
	Super::BeginPlay();

	// Built here rather than as assets, like the melee context: one button, no modifiers.
	// No triggers on the action: Started on press, Completed on release.
	AimAction = NewObject<UInputAction>(this, TEXT("IA_NRAim"));
	AimContext = NewObject<UInputMappingContext>(this, TEXT("IMC_NRAim"));
	AimContext->MapKey(AimAction, EKeys::RightMouseButton);

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		BindInput();
		UpdateMappingContext();
		Pawn->ReceiveRestartedDelegate.AddDynamic(this, &UNRAimComponent::OnPawnRestarted);
	}
}

void UNRAimComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &UNRAimComponent::OnPawnRestarted);
	}

	// No blend: the pawn is going away, and the view must not be left on our camera.
	if (Mode == ENRAimMode::Precision)
	{
		ExitFirstPerson(false);
	}
	Mode = ENRAimMode::None;

	if (FirstPersonCamera)
	{
		FirstPersonCamera->Destroy();
		FirstPersonCamera = nullptr;
	}

	ApplyAimFacing(false);
	bAimAllowed = false;
	UpdateMappingContext();

	Super::EndPlay(EndPlayReason);
}

void UNRAimComponent::OnPawnRestarted(APawn* Pawn)
{
	BindInput();
	UpdateMappingContext();
}

APlayerController* UNRAimComponent::GetPlayerController() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
}

// --- Input -------------------------------------------------------------------------

void UNRAimComponent::BindInput()
{
	if (bInputBound)
	{
		return;
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	UEnhancedInputComponent* Input = Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
	if (!Input || !AimAction)
	{
		return;
	}

	Input->BindAction(AimAction, ETriggerEvent::Started, this, &UNRAimComponent::OnAimPressed);
	Input->BindAction(AimAction, ETriggerEvent::Completed, this, &UNRAimComponent::OnAimReleased);
	bInputBound = true;
}

void UNRAimComponent::UpdateMappingContext()
{
	if (!AimContext)
	{
		return;
	}

	const APlayerController* PC = GetPlayerController();
	UEnhancedInputLocalPlayerSubsystem* Current = PC && PC->GetLocalPlayer()
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())
		: nullptr;
	UEnhancedInputLocalPlayerSubsystem* Wanted = bAimAllowed ? Current : nullptr;

	if (ContextSubsystem.Get() == Wanted)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Previous = ContextSubsystem.Get())
	{
		Previous->RemoveMappingContext(AimContext);
	}
	ContextSubsystem = Wanted;
	if (Wanted)
	{
		Wanted->AddMappingContext(AimContext, InputPriority);
	}
}

void UNRAimComponent::SetAimAllowed(bool bAllowed)
{
	if (bAimAllowed == bAllowed)
	{
		return;
	}

	bAimAllowed = bAllowed;
	if (!bAllowed)
	{
		ExitAim();
	}
	// Off while melee is in hand, so the melee context alone owns the right button.
	UpdateMappingContext();
}

void UNRAimComponent::OnAimPressed(const FInputActionValue& Value)
{
	if (!bAimAllowed || IsOwnerDead(GetOwner()))
	{
		return;
	}

	// Any press leaves precision aim, and that press is spent: its release does nothing.
	if (Mode == ENRAimMode::Precision)
	{
		bIgnoreRelease = true;
		SetMode(ENRAimMode::None);
		return;
	}

	// Shoulder aim starts at once; whether this was a click is only known on release.
	bIgnoreRelease = false;
	PressTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	SetMode(ENRAimMode::Shoulder);
}

void UNRAimComponent::OnAimReleased(const FInputActionValue& Value)
{
	if (bIgnoreRelease)
	{
		bIgnoreRelease = false;
		return;
	}
	if (Mode != ENRAimMode::Shoulder)
	{
		return;
	}

	const float Held = GetWorld() ? GetWorld()->GetTimeSeconds() - PressTime : TapThreshold + 1.0f;
	SetMode(Held <= TapThreshold ? ENRAimMode::Precision : ENRAimMode::None);
}

// --- Modes -------------------------------------------------------------------------

void UNRAimComponent::ExitAim()
{
	bIgnoreRelease = false;
	SetMode(ENRAimMode::None);
}

void UNRAimComponent::SetAimMode(ENRAimMode NewMode)
{
	// A dead character does not aim: entering would only flash first person for a frame.
	if (NewMode == ENRAimMode::None || (bAimAllowed && !IsOwnerDead(GetOwner())))
	{
		bIgnoreRelease = false;
		SetMode(NewMode);
	}
}

void UNRAimComponent::SetMode(ENRAimMode NewMode)
{
	if (Mode == NewMode)
	{
		return;
	}

	const ENRAimMode OldMode = Mode;
	Mode = NewMode;

	if (OldMode == ENRAimMode::Precision)
	{
		ExitFirstPerson(true);
	}
	if (NewMode == ENRAimMode::Precision)
	{
		EnterFirstPerson();
	}

	// Both aim modes are "aiming" to the sample: strafe, upper-body pose and aim offset.
	SetSampleWantsToAim(NewMode != ENRAimMode::None);
	if ((OldMode == ENRAimMode::None) != (NewMode == ENRAimMode::None))
	{
		ApplyAimFacing(NewMode != ENRAimMode::None);
	}
	if (OldMode == ENRAimMode::None && NewMode != ENRAimMode::None)
	{
		NextAimTurnTime = 0.0f;
		TryStartAimTurn();
	}
	SetComponentTickEnabled(NewMode != ENRAimMode::None);

	UE_LOG(LogNRAim, Log, TEXT("%s aim: %s -> %s"), *GetNameSafe(GetOwner()),
		*UEnum::GetValueAsString(OldMode), *UEnum::GetValueAsString(NewMode));
}

void UNRAimComponent::SetSampleWantsToAim(bool bWantsToAim) const
{
	AActor* Owner = GetOwner();
	const FStructProperty* InputState = Owner ? FindFProperty<FStructProperty>(Owner->GetClass(), TEXT("PlayerInputState")) : nullptr;
	if (!InputState)
	{
		return;
	}

	void* StructData = InputState->ContainerPtrToValuePtr<void>(Owner);
	for (TFieldIterator<FBoolProperty> It(InputState->Struct); It; ++It)
	{
		// Authored name: the Blueprint struct's real field names carry GUID suffixes.
		if (It->GetAuthoredName() == TEXT("WantsToAim"))
		{
			It->SetPropertyValue_InContainer(StructData, bWantsToAim);
			return;
		}
	}
}

void UNRAimComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Mode == ENRAimMode::None)
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (IsOwnerDead(GetOwner()))
	{
		ExitAim();
		return;
	}

	// Held every frame rather than set once: nothing else is meant to clear it, but if
	// anything in the sample does, aiming should not silently drop out.
	SetSampleWantsToAim(true);

	if (Mode == ENRAimMode::Precision)
	{
		UpdateFirstPersonCamera();
	}
}

// --- First person ------------------------------------------------------------------

FVector UNRAimComponent::GetEyeLocation() const
{
	const AActor* Owner = GetOwner();
	if (const UCapsuleComponent* Capsule = Owner ? Cast<UCapsuleComponent>(Owner->GetRootComponent()) : nullptr)
	{
		return Capsule->GetComponentLocation()
			+ FVector(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight() - EyeBelowCapsuleTop);
	}
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

void UNRAimComponent::UpdateFirstPersonCamera()
{
	const APlayerController* PC = GetPlayerController();
	if (FirstPersonCamera && PC)
	{
		FirstPersonCamera->SetActorLocationAndRotation(GetEyeLocation(), PC->GetControlRotation());
	}
}

void UNRAimComponent::EnterFirstPerson()
{
	APlayerController* PC = GetPlayerController();
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return;
	}

	if (!FirstPersonCamera)
	{
		FActorSpawnParameters Params;
		Params.Owner = GetOwner();
		Params.ObjectFlags |= RF_Transient;
		FirstPersonCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), GetEyeLocation(), PC->GetControlRotation(), Params);
		if (!FirstPersonCamera)
		{
			return;
		}
		// Letterboxing would change the frame shape the crosshair and HUD were laid out for.
		FirstPersonCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
	}

	FirstPersonCamera->GetCameraComponent()->SetFieldOfView(PrecisionFieldOfView);
	UpdateFirstPersonCamera();
	PC->SetViewTargetWithBlend(FirstPersonCamera, CameraBlendTime, VTBlend_EaseInOut, 2.0f);

	if (bHideCharacterInPrecision)
	{
		HideCharacter();
	}
}

void UNRAimComponent::ExitFirstPerson(bool bBlend)
{
	RestoreCharacter();

	// Back to the pawn: its Gameplay Camera takes over again as the view target's camera.
	if (APlayerController* PC = GetPlayerController())
	{
		PC->SetViewTargetWithBlend(GetOwner(), bBlend ? CameraBlendTime : 0.0f, VTBlend_EaseInOut, 2.0f);
	}
}

void UNRAimComponent::HideCharacter()
{
	RestoreCharacter();

	TArray<UMeshComponent*> Meshes;
	GetOwner()->GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!Mesh || !Mesh->IsVisible())
		{
			continue;
		}

		FHiddenMesh& Entry = HiddenMeshes.AddDefaulted_GetRef();
		Entry.Mesh = Mesh;
		if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Mesh))
		{
			// A hidden skeletal mesh may stop ticking its pose; montages, root motion and the
			// aim pose must carry on while the player cannot see them.
			Entry.bSkeletal = true;
			Entry.PreviousAnimTickOption = static_cast<uint8>(Skeletal->VisibilityBasedAnimTickOption);
			Skeletal->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
		Mesh->SetVisibility(false, false);
	}
}

void UNRAimComponent::RestoreCharacter()
{
	for (const FHiddenMesh& Entry : HiddenMeshes)
	{
		UMeshComponent* Mesh = Entry.Mesh.Get();
		if (!Mesh)
		{
			continue;
		}
		Mesh->SetVisibility(true, false);
		if (Entry.bSkeletal)
		{
			CastChecked<USkeletalMeshComponent>(Mesh)->VisibilityBasedAnimTickOption =
				static_cast<EVisibilityBasedAnimTickOption>(Entry.PreviousAnimTickOption);
		}
	}
	HiddenMeshes.Reset();
}

// --- Turning -----------------------------------------------------------------------

void UNRAimComponent::ApplyAimFacing(bool bAiming)
{
	// Put back whatever was replaced, whichever way this is going.
	for (const FFacingOverride& Entry : FacingOverrides)
	{
		if (UObject* ModeObject = Entry.Mode.Get())
		{
			Entry.Property->SetFloatingPointPropertyValue(Entry.Property->ContainerPtrToValuePtr<void>(ModeObject), Entry.Original);
		}
	}
	FacingOverrides.Reset();

	if (!bAiming || AimFacingTime <= 0.0f)
	{
		return;
	}

	const UMoverComponent* Mover = GetOwner()->FindComponentByClass<UMoverComponent>();
	UBaseMovementMode* Walking = Mover ? Mover->FindMovementModeByName(TEXT("walking")) : nullptr;
	if (!Walking)
	{
		return;
	}

	// The sample's BP_MovementMode_Walking sets SmoothWalkingSettings.FacingSmoothingTime every
	// step from these two variables, so it is these that must change. Matched on the authored
	// name; the class is a Blueprint and offers nothing to call.
	for (TFieldIterator<FNumericProperty> It(Walking->GetClass()); It; ++It)
	{
		const FString Name = It->GetAuthoredName();
		if (!It->IsFloatingPoint() || !(Name.Equals(TEXT("Walk/RunFacingTime"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("SprintFacingTime"), ESearchCase::IgnoreCase)))
		{
			continue;
		}

		void* Value = It->ContainerPtrToValuePtr<void>(Walking);
		FFacingOverride& Entry = FacingOverrides.AddDefaulted_GetRef();
		Entry.Mode = Walking;
		Entry.Property = *It;
		Entry.Original = It->GetFloatingPointPropertyValue(Value);
		It->SetFloatingPointPropertyValue(Value, AimFacingTime);
	}

	if (FacingOverrides.Num() == 0)
	{
		UE_LOG(LogNRAim, Warning, TEXT("%s: walking mode %s has no facing time variables; aim turning stays as it is"),
			*GetNameSafe(GetOwner()), *GetNameSafe(Walking->GetClass()));
	}
}

// --- Snap turn ---------------------------------------------------------------------

void UNRAimComponent::TryStartAimTurn()
{
	UWorld* World = GetWorld();
	const APlayerController* PC = GetPlayerController();
	UCharacterMoverComponent* Mover = GetOwner()->FindComponentByClass<UCharacterMoverComponent>();
	if (AimTurnTime <= 0.0f || !World || !PC || !Mover || World->GetTimeSeconds() < NextAimTurnTime)
	{
		return;
	}

	// On the ground only: the move hands back the current velocity, which in the air would
	// hold the fall for the length of the turn.
	if (!Mover->IsOnGround())
	{
		return;
	}

	const float Off = FMath::Abs(FRotator::NormalizeAxis(PC->GetControlRotation().Yaw - GetOwner()->GetActorRotation().Yaw));
	if (Off < AimTurnTriggerAngle)
	{
		return;
	}

	TSharedPtr<FNRAimTurnMove> Turn = MakeShared<FNRAimTurnMove>();
	Turn->TurnSeconds = AimTurnTime;
	// A short tail past the turn itself, during which the move only holds the aim direction.
	// The walking mode resumes from whatever angular velocity the sync state carries; ending
	// on the turn's last step would hand it ~1800 deg/s and the facing spring would carry the
	// body 30 degrees past the aim before pulling it back (measured with NR.TurnTrace).
	Turn->DurationMs = (AimTurnTime + 0.06f) * 1000.0f;
	Turn->MixMode = EMoveMixMode::OverrideVelocity;
	Mover->QueueLayeredMove(Turn);

	// One at a time; the next may start once this one has had its time.
	NextAimTurnTime = World->GetTimeSeconds() + AimTurnTime;
	UE_LOG(LogNRAim, Verbose, TEXT("%s aim turn: %.0f deg"), *GetNameSafe(GetOwner()), Off);
}

bool FNRAimTurnMove::GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep,
	const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove)
{
	const FMoverDefaultSyncState* SyncState = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	const FCharacterDefaultInputs* Inputs = StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
	const float DeltaSeconds = TimeStep.StepMs * 0.001f;
	if (!SyncState || !Inputs || DeltaSeconds <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// Time left in the turn, never less than this step, so the last step lands exactly.
	const double Elapsed = (TimeStep.BaseSimTimeMs - StartSimTimeMs) * 0.001;
	const float TimeLeft = FMath::Max(TurnSeconds - static_cast<float>(Elapsed), DeltaSeconds);
	const float Remaining = FRotator::NormalizeAxis(Inputs->ControlRotation.Yaw - SyncState->GetOrientation_WorldSpace().Yaw);

	OutProposedMove = FProposedMove();
	OutProposedMove.MixMode = MixMode;
	// Same velocity as now: the turn changes where the body faces, not how it moves.
	OutProposedMove.LinearVelocity = SyncState->GetVelocity_WorldSpace();
	OutProposedMove.AngularVelocityDegrees = FVector(0.0f, 0.0f, Remaining / TimeLeft);
	return true;
}

FLayeredMoveBase* FNRAimTurnMove::Clone() const
{
	return new FNRAimTurnMove(*this);
}

void FNRAimTurnMove::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);
	Ar << TurnSeconds;
}

UScriptStruct* FNRAimTurnMove::GetScriptStruct() const
{
	return FNRAimTurnMove::StaticStruct();
}

FString FNRAimTurnMove::ToSimpleString() const
{
	return TEXT("NRAimTurn");
}

void FNRAimTurnMove::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}
