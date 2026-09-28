// ProjectNR player pawn.

#include "NRPlayerPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/Modes/FlyingMode.h"
#include "DefaultMovementSet/NavMoverComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Core/CameraAsset.h"
#include "GameFramework/GameplayCameraComponent.h"
#include "InputMappingContext.h"
#include "MotionWarpingComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "MoveLibrary/BasedMovementUtils.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "MoverDataModelTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRPlayer, Log, All);

static TAutoConsoleVariable<int32> CVarPlayerInputDiagnostics(
	TEXT("NR.PlayerInput"),
	0,
	TEXT("Logs the player pawn's cached input state whenever it changes."),
	ECVF_Default);

ANRPlayerPawn::ANRPlayerPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Mover ticks the movement simulation itself, and ProduceInput reads cached state
	// rather than polling. Nothing here needs a per-frame update.
	PrimaryActorTick.bCanEverTick = false;

	// Mover owns movement replication. Leaving actor-level movement replication on would
	// have the two fight over the transform.
	SetReplicatingMovement(false);

	// Mover writes the pawn's rotation through its orientation intent, so the controller
	// must not also drive it.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// The sample's own capsule, read from SandboxCharacter_CMC: 86 half height, 30 radius.
	// Not Unreal's 96 x 42 template values - the camera rigs anchor at the capsule centre,
	// so a taller capsule lifts the whole camera, and a fatter one changes what the
	// character can squeeze through.
	CollisionCapsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionCapsule"));
	SetRootComponent(CollisionCapsule);
	CollisionCapsule->InitCapsuleSize(30.0f, 86.0f);
	CollisionCapsule->SetCollisionProfileName(TEXT("Pawn"));

	// Standard mannequin alignment: dropped by the capsule's half height so the feet sit
	// on the capsule's bottom, and yawed -90 because the mannequin faces +Y.
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(CollisionCapsule);
	Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -88.0f));
	Mesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CollisionCapsule);
	CameraBoom->TargetArmLength = 400.0f;
	// The boom follows the controller so looking around never rotates the pawn itself.
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Off by default, exactly as the sample leaves its own Camera component: an active
	// UCameraComponent on the view target is what APawn::CalcCamera returns, so leaving
	// this on would let it answer for the view while the gameplay camera also believes it
	// is driving. SetupCamera turns it on only when it is the one being used.
	FollowCamera->bAutoActivate = false;

	// Attached to the mesh, matching the sample, which parents its own GameplayCamera to
	// CharacterMesh0. That puts the rig's anchor at the character's feet rather than at
	// the capsule's centre - anchoring to the capsule instead lifts the whole camera by
	// the capsule's half height, 86cm. Not the spring arm either: the rigs do their own
	// arm and collision work, so hanging it there would apply that twice.
	GameplayCamera = CreateDefaultSubobject<UGameplayCameraComponent>(TEXT("GameplayCamera"));
	GameplayCamera->SetupAttachment(Mesh);

	MoverComponent = CreateDefaultSubobject<UCharacterMoverComponent>(TEXT("MoverComponent"));

	// The sample maps "Traversing" to a plain UFlyingMode, and AC_TraversalLogic switches
	// the Mover into it for the duration of a vault or climb. Flying has no gravity and no
	// ground friction, so the montage's root motion and the warp targets can move the
	// capsule freely.
	//
	// Without this key the switch silently fails, the walking mode keeps pushing the pawn
	// into the wall while the warp drags the capsule past it, and the character appears to
	// walk through solid geometry.
	//
	// The mode has to be created with MoverComponent as its Outer, not the pawn.
	// UBaseMovementMode::GetMoverComponent is Cast<UMoverComponent>(GetOuter()), and
	// UFlyingMode::OnRegistered dereferences the result without a null check, so a
	// pawn-outered mode crashes the editor the moment the simulation registers it.
	MoverComponent->MovementModes.Add(
		FName(TEXT("Traversing")),
		ObjectInitializer.CreateDefaultSubobject<UFlyingMode>(MoverComponent, TEXT("TraversingMode")));

	NavMoverComponent = CreateDefaultSubobject<UNavMoverComponent>(TEXT("NavMoverComponent"));

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));

	MotionWarping = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarping"));
}

void ANRPlayerPawn::ApplyMovementSettings()
{
	if (!MoverComponent)
	{
		return;
	}

	UCommonLegacyMovementSettings* Settings =
		MoverComponent->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>();

	if (!Settings)
	{
		UE_LOG(LogNRPlayer, Warning,
			TEXT("%s found no CommonLegacyMovementSettings; the pawn will run at Mover's defaults."), *GetName());
		return;
	}

	Settings->MaxSpeed = GetDesiredMaxSpeed();
	Settings->Acceleration = GetDesiredAcceleration();
	Settings->Deceleration = MovementDeceleration;
	Settings->GroundFriction = MovementGroundFriction;
	Settings->TurningRate = MovementTurningRate;
	Settings->MaxStepHeight = MovementMaxStepHeight;
}

float ANRPlayerPawn::GetDesiredMaxSpeed() const
{
	// Crouching caps speed regardless of gait, then the same precedence the animation
	// bridge uses for E_Gait. If these disagreed, the pawn would move at one gait's speed
	// while the animation claimed another, and the feet would slide.
	if (bWantsToCrouch)
	{
		return CrouchSpeed;
	}
	if (bWantsToSprint)
	{
		return SprintSpeed;
	}
	if (bWantsToWalk)
	{
		return WalkSpeed;
	}
	return RunSpeed;
}

float ANRPlayerPawn::GetDesiredAcceleration() const
{
	if (bWantsToSprint)
	{
		return SprintAcceleration;
	}
	if (bWantsToWalk)
	{
		return WalkAcceleration;
	}
	return RunAcceleration;
}

float ANRPlayerPawn::GetMaxAcceleration() const
{
	if (MoverComponent)
	{
		if (const UCommonLegacyMovementSettings* Settings =
			MoverComponent->FindSharedSettings<UCommonLegacyMovementSettings>())
		{
			return Settings->Acceleration;
		}
	}
	return GetDesiredAcceleration();
}

float ANRPlayerPawn::GetMaxDeceleration() const
{
	if (MoverComponent)
	{
		if (const UCommonLegacyMovementSettings* Settings =
			MoverComponent->FindSharedSettings<UCommonLegacyMovementSettings>())
		{
			return Settings->Deceleration;
		}
	}
	return MovementDeceleration;
}

void ANRPlayerPawn::SetupPlayerInputContext()
{
	if (bInputContextApplied)
	{
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
	if (!Subsystem)
	{
		return;
	}

	if (!DefaultMappingContext)
	{
		UE_LOG(LogNRPlayer, Warning,
			TEXT("%s has no DefaultMappingContext: no input will reach this pawn."), *GetName());
		return;
	}

	Subsystem->AddMappingContext(DefaultMappingContext, MappingContextPriority);
	bInputContextApplied = true;

	UE_LOG(LogNRPlayer, Log, TEXT("%s applied mapping context %s."),
		*GetName(), *DefaultMappingContext->GetName());
}

void ANRPlayerPawn::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// Possession is the first moment a GameMode-spawned pawn actually has a controller.
	SetupPlayerInputContext();
	SetupCamera();

	// Unique: this runs on every possession change, not once, and a second plain AddDynamic
	// trips the delegate's duplicate-binding ensure.
	if (MoverComponent)
	{
		MoverComponent->OnPostFinalize.AddUniqueDynamic(this, &ANRPlayerPawn::OnMoverPostFinalize);
		MoverComponent->OnBasedMovementApplied.AddUniqueDynamic(this, &ANRPlayerPawn::OnMoverBasedMovementApplied);
	}
}

void ANRPlayerPawn::OnMoverPostFinalize(const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState)
{
	// Same shape as the sample's Update_FloorValues. Running it here rather than inside the
	// animation bridge getter keeps the world query on the game thread, which matters
	// because the animation Blueprint updates on a worker thread.
	bHasFloor = false;
	FFloorCheckResult FloorResult;
	UFloorQueryUtils::TryFindFloor(MoverComponent, bHasFloor, FloorResult);

	if (bHasFloor)
	{
		FloorLocation = FloorResult.HitResult.ImpactPoint;
		FloorNormal = FloorResult.HitResult.ImpactNormal;
	}
	else
	{
		// Airborne or over a hole. Reporting the mesh's own origin and a flat normal keeps
		// the slope maths sane; a zero normal would make SlopeAngle meaningless and tilt
		// the pelvis against a plane that is not there.
		FloorLocation = Mesh ? Mesh->GetComponentLocation() : GetActorLocation();
		FloorNormal = FVector::UpVector;
	}
}

void ANRPlayerPawn::OnMoverBasedMovementApplied(const FTransform& TransformDelta, const FMoverTimeStep& TimeStep)
{
	BasedMovementDelta = TransformDelta.GetTranslation();
}

void ANRPlayerPawn::SetupCamera()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	// Only the gameplay camera knows about the sample's rigs, so it wins whenever a
	// camera asset is assigned. The spring arm stays as the fallback rather than being
	// removed, so a child Blueprint with no camera asset is still playable.
	// Mirrors the sample's own SetupCamera. The sample reads this console variable to
	// decide between the Gameplay Cameras rigs and a plain spring arm, and without the
	// same switch there is no way to fall back when a rig misbehaves.
	bool bUseGameplayCameraSystem = true;
	if (const IConsoleVariable* CVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("DDCVar.NewGameplayCameraSystem.Enable")))
	{
		bUseGameplayCameraSystem = CVar->GetBool();
	}

	if (bUseGameplayCameraSystem && GameplayCamera && GameplayCamera->CameraReference.GetCameraAsset())
	{
		// Belt and braces: a child Blueprint can re-enable auto-activate on the spring arm
		// camera without realising it would then shadow the rigs.
		if (FollowCamera && FollowCamera->IsActive())
		{
			FollowCamera->Deactivate();
		}

		GameplayCamera->ActivateCameraForPlayerController(PC);
		UE_LOG(LogNRPlayer, Log, TEXT("%s activated gameplay camera %s."),
			*GetName(), *GameplayCamera->CameraReference.GetCameraAsset()->GetName());
		return;
	}

	if (FollowCamera)
	{
		FollowCamera->Activate();
		PC->SetViewTarget(this);
		UE_LOG(LogNRPlayer, Log, TEXT("%s fell back to the spring arm camera."), *GetName());
	}
}

void ANRPlayerPawn::BeginPlay()
{
	Super::BeginPlay();

	ApplyMovementSettings();

	// These two need a controller. When the pawn is placed in the level with auto-possess
	// it already has one here; when the GameMode spawns it, it does not, and
	// NotifyControllerChanged picks them up instead.
	SetupPlayerInputContext();
	SetupCamera();
}

void ANRPlayerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogNRPlayer, Error,
			TEXT("%s expected an EnhancedInputComponent. Check DefaultPlayerInputClass in project settings."), *GetName());
		return;
	}

	// Every binding is guarded: a child Blueprint may legitimately leave an action unset
	// while a feature is not wired up yet, and BindAction on null asserts.
	if (MoveAction)
	{
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ANRPlayerPawn::OnMoveTriggered);
		Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &ANRPlayerPawn::OnMoveCompleted);
	}
	if (LookAction)
	{
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ANRPlayerPawn::OnLookTriggered);
	}
	if (JumpAction)
	{
		Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ANRPlayerPawn::OnJumpStarted);
		Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ANRPlayerPawn::OnJumpReleased);
	}
	if (SprintAction)
	{
		Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ANRPlayerPawn::OnSprintStarted);
		Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ANRPlayerPawn::OnSprintReleased);
	}
	if (WalkAction)
	{
		Input->BindAction(WalkAction, ETriggerEvent::Started, this, &ANRPlayerPawn::OnWalkStarted);
		Input->BindAction(WalkAction, ETriggerEvent::Completed, this, &ANRPlayerPawn::OnWalkReleased);
	}
	if (AimAction)
	{
		// The sample's IA_Aim carries Pressed and Released triggers, so Triggered fires on
		// both edges with the action value telling them apart. Started/Completed is used
		// here instead so the binding works whatever triggers the asset happens to have.
		Input->BindAction(AimAction, ETriggerEvent::Started, this, &ANRPlayerPawn::OnAimStarted);
		Input->BindAction(AimAction, ETriggerEvent::Completed, this, &ANRPlayerPawn::OnAimReleased);
	}
	if (StrafeAction)
	{
		Input->BindAction(StrafeAction, ETriggerEvent::Started, this, &ANRPlayerPawn::OnStrafeToggled);
	}
	if (CrouchAction)
	{
		Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ANRPlayerPawn::OnCrouchToggled);
	}
	if (TraverseAction)
	{
		Input->BindAction(TraverseAction, ETriggerEvent::Started, this, &ANRPlayerPawn::OnTraverseStarted);
	}
}

FRotator ANRPlayerPawn::GetAimingRotation() const
{
	if (const AController* C = GetController())
	{
		return C->GetControlRotation();
	}

	// No controller: the actor's own rotation is the only meaningful facing. Returning a
	// zero rotator instead would snap the aim offset to due north.
	return GetActorRotation();
}

float ANRPlayerPawn::GetMovementDirectionAngle() const
{
	if (!MoverComponent)
	{
		return 0.0f;
	}

	FVector Velocity = MoverComponent->GetVelocity();
	Velocity.Z = 0.0f;

	// Below the threshold the pawn is effectively still, and the direction of a velocity
	// that short is noise. Reporting "forward" keeps the idle pose stable.
	if (Velocity.SizeSquared() < FMath::Square(MovementDirectionSpeedThreshold))
	{
		return 0.0f;
	}

	const FRotator TravelRotation = Velocity.Rotation();
	const FRotator Delta = (TravelRotation - GetActorRotation()).GetNormalized();

	// Yaw alone: pitch and roll say nothing about which way the pawn is stepping.
	return Delta.Yaw;
}

void ANRPlayerPawn::OnMoveTriggered(const FInputActionValue& Value)
{
	// IA_Move is an Axis2D, and IMC_Sandbox puts a SwizzleAxis(YXZ) on W/S while leaving
	// A/D alone. So the action's X is strafe and its Y is forward - the opposite of
	// Unreal's own axis convention, where X is forward and Y is right.
	//
	// The swap happens here rather than in ProduceInput so that everything downstream
	// can treat CachedMoveInputIntent as a plain (forward, right, up) vector. Reading it
	// with the action's axes still crossed is what made W strafe instead of walk.
	const FVector2D MovementVector = Value.Get<FVector2D>();
	CachedMoveInputIntent.X = FMath::Clamp(MovementVector.Y, -1.0f, 1.0f);
	CachedMoveInputIntent.Y = FMath::Clamp(MovementVector.X, -1.0f, 1.0f);
	CachedMoveInputIntent.Z = 0.0f;
}

void ANRPlayerPawn::OnMoveCompleted(const FInputActionValue& Value)
{
	CachedMoveInputIntent = FVector::ZeroVector;
}

void ANRPlayerPawn::OnLookTriggered(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	AddControllerYawInput(LookAxisVector.X);
	AddControllerPitchInput(LookAxisVector.Y);
}

void ANRPlayerPawn::OnJumpStarted(const FInputActionValue& Value)
{
	// The traversal gets first refusal on this press, exactly as in the sample: there, a
	// successful vault or climb produces no jump input at all.
	//
	// This used to rely on the Blueprint calling CancelPendingJump from the separate
	// IA_Traverse binding, but the two input actions do not resolve on the same frame, so
	// the jump reached UCharacterMoverComponent a tick or two later and it queued Falling
	// over the Traversing mode the traversal had just queued. The montage then played while
	// the walking and falling modes moved the capsule, which is what drove the character
	// into the wall.
	OnTraversePressed();

	// A traversal that took the input is now playing its montage, so this press is spent.
	if (Mesh)
	{
		if (const UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			if (AnimInstance->IsAnyMontagePlaying())
			{
				return;
			}
		}
	}

	// Only an actual press raises the edge, so holding the key does not re-trigger.
	bIsJumpJustPressed = !bIsJumpPressed;
	bIsJumpPressed = true;

	if (CVarPlayerInputDiagnostics.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogNRPlayer, Log, TEXT("Jump pressed (edge=%d)"), bIsJumpJustPressed ? 1 : 0);
	}
}

void ANRPlayerPawn::OnJumpReleased(const FInputActionValue& Value)
{
	bIsJumpPressed = false;
	bIsJumpJustPressed = false;
}

void ANRPlayerPawn::OnSprintStarted(const FInputActionValue& Value)
{
	bWantsToSprint = true;
}

void ANRPlayerPawn::OnSprintReleased(const FInputActionValue& Value)
{
	bWantsToSprint = false;
}

void ANRPlayerPawn::OnWalkStarted(const FInputActionValue& Value)
{
	bWantsToWalk = true;
}

void ANRPlayerPawn::OnWalkReleased(const FInputActionValue& Value)
{
	bWantsToWalk = false;
}

void ANRPlayerPawn::OnAimStarted(const FInputActionValue& Value)
{
	bWantsToAim = true;

	if (CVarPlayerInputDiagnostics.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogNRPlayer, Log, TEXT("Aim ON"));
	}
}

void ANRPlayerPawn::OnAimReleased(const FInputActionValue& Value)
{
	bWantsToAim = false;

	if (CVarPlayerInputDiagnostics.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogNRPlayer, Log, TEXT("Aim OFF"));
	}
}

void ANRPlayerPawn::OnStrafeToggled(const FInputActionValue& Value)
{
	bWantsToStrafe = !bWantsToStrafe;
}

void ANRPlayerPawn::OnCrouchToggled(const FInputActionValue& Value)
{
	bWantsToCrouch = !bWantsToCrouch;
}

void ANRPlayerPawn::CancelPendingJump()
{
	// Both flags: the edge so this frame's jump never reaches Mover, and the held state
	// so keeping the key down through the traversal does not jump the moment it ends.
	bIsJumpJustPressed = false;
	bIsJumpPressed = false;
}

void ANRPlayerPawn::OnTraverseStarted(const FInputActionValue& Value)
{
	// Everything past this point lives in Blueprint: AC_TraversalLogic takes and returns
	// user-defined structs, so native code can only hand over control here.
	OnTraversePressed();
}

void ANRPlayerPawn::ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult)
{
	FCharacterDefaultInputs& CharacterInputs =
		InputCmdResult.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();

	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		// Unpossessed on the authority while a simulated proxy exists: hand back an
		// explicit do-nothing command instead of whatever the last controller left behind.
		if (GetLocalRole() == ROLE_Authority && GetRemoteRole() == ROLE_SimulatedProxy)
		{
			static const FCharacterDefaultInputs DoNothingInput;
			CharacterInputs = DoNothingInput;
		}

		// Otherwise leave the previous command untouched: simulated proxies extrapolate
		// from it, and zeroing it would make them stutter to a halt.
		return;
	}

	CharacterInputs.ControlRotation = PC->GetControlRotation();

	// Gait changes between frames, so the speed cap has to follow it here rather than
	// only at BeginPlay. Without this, pressing sprint would change the animation the
	// bridge reports while the pawn kept moving at run speed.
	if (UCommonLegacyMovementSettings* Settings =
		MoverComponent ? MoverComponent->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>() : nullptr)
	{
		Settings->MaxSpeed = GetDesiredMaxSpeed();
		Settings->Acceleration = GetDesiredAcceleration();
	}

	// The navigation system writes into both cached values, so this has to happen before
	// they are read. A nav request supplies a velocity, which outranks directional intent.
	bool bRequestedNavMovement = false;
	if (NavMoverComponent)
	{
		bRequestedNavMovement =
			NavMoverComponent->ConsumeNavMovementData(CachedMoveInputIntent, CachedMoveInputVelocity);
	}

	if (CachedMoveInputVelocity.IsZero())
	{
		// Input arrives in control space - W means "away from the camera" - so it only
		// becomes a world direction once rotated to face where the camera looks.
		//
		// Yaw only: rotating by the full control rotation would tilt the movement vector
		// with the camera's pitch, so looking at the ground would push the pawn downwards
		// and shorten its stride. Ground movement only cares about the heading.
		const FRotator MoveHeading(0.0f, CharacterInputs.ControlRotation.Yaw, 0.0f);
		const FVector WorldIntent = MoveHeading.RotateVector(CachedMoveInputIntent);
		CharacterInputs.SetMoveInput(EMoveInputType::DirectionalIntent, WorldIntent);
	}
	else
	{
		CharacterInputs.SetMoveInput(EMoveInputType::Velocity, CachedMoveInputVelocity);
	}

	// OnMoveCompleted clears input that came from the player, but a nav request never
	// fires that event, so it has to be cleared here or the pawn would keep coasting.
	if (bRequestedNavMovement)
	{
		CachedMoveInputIntent = FVector::ZeroVector;
		CachedMoveInputVelocity = FVector::ZeroVector;
	}

	// --- Facing ---

	static constexpr float RotationMagMin = 1e-3f;
	const bool bHasAffirmativeMoveInput = (CharacterInputs.GetMoveInput().Size() >= RotationMagMin);

	// Aiming and strafing both mean "keep facing where the camera looks". That is what
	// makes strafe locomotion read correctly and what the rifle aim layer assumes: the
	// aim offset is a delta from the actor's facing, so a pawn that turns away from the
	// camera would drive the offset to its limits.
	const bool bFaceCamera = bWantsToAim || bWantsToStrafe || !bOrientRotationToMovement;

	CharacterInputs.OrientationIntent = FVector::ZeroVector;

	if (bFaceCamera)
	{
		// Flattened: pitch must not tip the pawn over when looking up or down.
		CharacterInputs.OrientationIntent = CharacterInputs.ControlRotation.Vector().GetSafeNormal2D();
		LastAffirmativeOrientationIntent = CharacterInputs.OrientationIntent;
	}
	else if (bHasAffirmativeMoveInput)
	{
		CharacterInputs.OrientationIntent = CharacterInputs.GetMoveInput().GetSafeNormal();
		LastAffirmativeOrientationIntent = CharacterInputs.OrientationIntent;
	}
	else if (bMaintainLastInputOrientation)
	{
		CharacterInputs.OrientationIntent = LastAffirmativeOrientationIntent;
	}

	// --- Jump ---

	CharacterInputs.bIsJumpPressed = bIsJumpPressed;
	CharacterInputs.bIsJumpJustPressed = bIsJumpJustPressed;

	// Nothing forces a mode change yet; flying and crouching come later.
	CharacterInputs.SuggestedMovementMode = NAME_None;

	// --- Movement base ---

	CharacterInputs.bUsingMovementBase = false;

	if (bUseBaseRelativeMovement && MoverComponent)
	{
		if (UPrimitiveComponent* MovementBase = MoverComponent->GetMovementBase())
		{
			const FName MovementBaseBoneName = MoverComponent->GetMovementBaseBoneName();

			FVector RelativeMoveInput, RelativeOrientDir;
			UBasedMovementUtils::TransformWorldDirectionToBased(
				MovementBase, MovementBaseBoneName, CharacterInputs.GetMoveInput(), RelativeMoveInput);
			UBasedMovementUtils::TransformWorldDirectionToBased(
				MovementBase, MovementBaseBoneName, CharacterInputs.OrientationIntent, RelativeOrientDir);

			CharacterInputs.SetMoveInput(CharacterInputs.GetMoveInputType(), RelativeMoveInput);
			CharacterInputs.OrientationIntent = RelativeOrientDir;

			CharacterInputs.bUsingMovementBase = true;
			CharacterInputs.MovementBase = MovementBase;
			CharacterInputs.MovementBaseBoneName = MovementBaseBoneName;
		}
	}

	// Mirror what Mover was actually told, after the movement-base conversion above, so
	// the animation bridge and the simulation never disagree about this frame.
	LastWorldMoveIntent = CharacterInputs.GetMoveInput();
	LastOrientationIntent = CharacterInputs.OrientationIntent;

	if (CVarPlayerInputDiagnostics.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogNRPlayer, Log,
			TEXT("Move=(%.2f, %.2f) Orient=(%.2f, %.2f) Jump=%d/%d Aim=%d Sprint=%d Walk=%d Strafe=%d Crouch=%d"),
			CharacterInputs.GetMoveInput().X, CharacterInputs.GetMoveInput().Y,
			CharacterInputs.OrientationIntent.X, CharacterInputs.OrientationIntent.Y,
			bIsJumpPressed ? 1 : 0, bIsJumpJustPressed ? 1 : 0,
			bWantsToAim ? 1 : 0, bWantsToSprint ? 1 : 0, bWantsToWalk ? 1 : 0,
			bWantsToStrafe ? 1 : 0, bWantsToCrouch ? 1 : 0);
	}

	// Only the edge is consumed. Held state stays, because the game thread may tick
	// slower than the Mover simulation and the press must survive until it is used.
	bIsJumpJustPressed = false;
}
