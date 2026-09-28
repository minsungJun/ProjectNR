// ProjectNR player pawn.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "MoverSimulationTypes.h"
#include "NRPlayerPawn.generated.h"

class UCameraComponent;
class UCapsuleComponent;
class UCharacterMoverComponent;
class UGameplayCameraComponent;
class UHealthComponent;
class UInputAction;
class UMotionWarpingComponent;
class UInputMappingContext;
class UNavMoverComponent;
class USkeletalMeshComponent;
class USpringArmComponent;

/**
 * Player pawn moved by the Mover plugin.
 *
 * The split with the Blueprint child is deliberate. This class owns everything the
 * engine can express in C++: components, Enhanced Input, cached input state, and the
 * movement parameters handed to the Mover simulation each frame. The child Blueprint
 * owns the animation bridge, because the structs the Game Animation Sample's AnimBP
 * consumes (S_CharacterPropertiesForAnimation, S_PlayerInputState, S_MoverCustomInputs)
 * are user-defined Blueprint structs and cannot be referenced from native code.
 *
 * Unlike AMoverExamplesCharacter, which is abstract and finds its components in
 * PostInitializeComponents because its Blueprints supply them, this class builds its
 * own so a child Blueprint only has to assign assets.
 */
UCLASS()
class PROJECTNR_API ANRPlayerPawn : public APawn, public IMoverInputProducerInterface
{
	GENERATED_BODY()

public:
	ANRPlayerPawn(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = "Player")
	UCharacterMoverComponent* GetMoverComponent() const { return MoverComponent; }

	UFUNCTION(BlueprintPure, Category = "Player")
	USkeletalMeshComponent* GetMesh() const { return Mesh; }

	UFUNCTION(BlueprintPure, Category = "Player")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	/** Required by the sample's traversal system: Get_PropertiesForTraversal hands this to
	 *  AC_TraversalLogic so the montages can warp onto the ledge they found. */
	UFUNCTION(BlueprintPure, Category = "Player")
	UMotionWarpingComponent* GetMotionWarping() const { return MotionWarping; }

	UFUNCTION(BlueprintPure, Category = "Player")
	UCapsuleComponent* GetCapsule() const { return CollisionCapsule; }

	/**
	 * Fired when the traverse key goes down. Implemented in the child Blueprint because
	 * the whole traversal path runs on Blueprint structs (S_TraversalCheckInputs and
	 * friends) that native code cannot name.
	 *
	 * The Blueprint is expected to try the traversal and fall back to a jump when it
	 * fails, which is why the same key drives both in IMC_Sandbox.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Traversal", meta = (DisplayName = "On Traverse Pressed"))
	void OnTraversePressed();

	/**
	 * Drops the jump that the same key press queued. IMC_Sandbox maps IA_Jump and
	 * IA_Traverse to the same key, so a successful traversal has to cancel the jump or
	 * the pawn vaults and leaps at once.
	 *
	 * Call this from the Blueprint the moment a traversal is accepted, not on failure:
	 * failing should leave the jump alone so the key still does something.
	 */
	UFUNCTION(BlueprintCallable, Category = "Traversal")
	void CancelPendingJump();

	/** Movement intent in control space as (forward, right, 0), -1..1 per axis. Zero when
	 *  the stick is released. Already un-swizzled from the input action's own axis order. */
	UFUNCTION(BlueprintPure, Category = "Input State")
	FVector GetMoveInputIntent() const { return CachedMoveInputIntent; }

	// --- Values only ProduceInput knows. Exposed so the Blueprint animation bridge can
	//     fill S_CharacterPropertiesForAnimation without recomputing them and drifting
	//     out of step with what Mover was actually told. ---

	/** World-space direction fed to Mover this frame, already rotated by the camera heading. */
	UFUNCTION(BlueprintPure, Category = "Input State")
	FVector GetWorldMoveIntent() const { return LastWorldMoveIntent; }

	/** World-space facing the pawn is turning towards. */
	UFUNCTION(BlueprintPure, Category = "Input State")
	FVector GetOrientationIntent() const { return LastOrientationIntent; }

	/** Where the player is aiming. Falls back to the actor's rotation when unpossessed,
	 *  so the animation bridge never reads a zero rotator. */
	UFUNCTION(BlueprintPure, Category = "Input State")
	FRotator GetAimingRotation() const;

	/**
	 * Ground under the pawn, refreshed once per simulation frame.
	 *
	 * The animation Blueprint derives SmoothedGroundNormal, SlopeAngle and the foot
	 * placement targets from these. Left at zero - which is what happens when the
	 * S_CharacterPropertiesForAnimation pins are unconnected - the slope maths works on a
	 * degenerate normal and the pelvis and feet are placed against a plane that does not
	 * exist.
	 *
	 * Mirrors the sample's Update_FloorValues: when there is no walkable floor, the normal
	 * falls back to straight up and the location to the mesh's own origin, rather than
	 * reporting a floor at the world origin.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation Bridge")
	FVector GetFloorNormal() const { return FloorNormal; }

	UFUNCTION(BlueprintPure, Category = "Animation Bridge")
	FVector GetFloorLocation() const { return FloorLocation; }

	UFUNCTION(BlueprintPure, Category = "Animation Bridge")
	bool HasFloor() const { return bHasFloor; }

	/** Movement contributed by whatever the pawn is standing on, so the animation bridge
	 *  can subtract it before deciding the pawn is moving under its own power. */
	UFUNCTION(BlueprintPure, Category = "Animation Bridge")
	FVector GetBasedMovementDelta() const { return BasedMovementDelta; }

	/**
	 * Signed angle in degrees between where the pawn faces and where it is travelling:
	 * 0 is straight ahead, +90 is directly right, +/-180 is straight back.
	 *
	 * The animation bridge turns this into E_MovementDirection. The mapping stays in
	 * Blueprint because that enum is a user-defined Blueprint type native code cannot
	 * reference, but the trigonometry belongs here where it can be tested.
	 *
	 * Returns 0 below MovementDirectionSpeedThreshold: normalising a near-zero velocity
	 * produces a direction made of rounding error, which would flicker the strafe
	 * animations while standing still.
	 */
	UFUNCTION(BlueprintPure, Category = "Input State")
	float GetMovementDirectionAngle() const;

	/** Live values from Mover's shared settings, for the animation bridge's
	 *  CurrentMaxAcceleration / CurrentMaxDeceleration. Motion matching predicts the
	 *  trajectory from these, so leaving them at zero makes it guess badly. */
	UFUNCTION(BlueprintPure, Category = "Input State")
	float GetMaxAcceleration() const;

	UFUNCTION(BlueprintPure, Category = "Input State")
	float GetMaxDeceleration() const;

	/** Speed the current gait asks for. Sprint beats walk, matching how the animation
	 *  bridge picks E_Gait, so the pawn never moves at a speed its gait does not claim. */
	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetDesiredMaxSpeed() const;

	/** Acceleration the current gait asks for. */
	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetDesiredAcceleration() const;

protected:
	virtual void BeginPlay() override;

	/** Bound to the Mover's post-finalize delegate so the floor query runs once per
	 *  simulation frame, on the game thread, exactly where the sample refreshes it. */
	UFUNCTION()
	void OnMoverPostFinalize(const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState);

	UFUNCTION()
	void OnMoverBasedMovementApplied(const FTransform& TransformDelta, const FMoverTimeStep& TimeStep);
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/**
	 * A GameMode-spawned pawn runs BeginPlay before the controller possesses it, so
	 * anything that needs a controller has to be redone here. Without this the mapping
	 * context is never applied and the gameplay camera is never handed to the player.
	 */
	virtual void NotifyControllerChanged() override;

	/**
	 * IMoverInputProducerInterface: contributes this frame's movement parameters.
	 *
	 * Mover calls this once per simulation frame, which is not the same as once per
	 * render frame - it may be called zero or several times. So this reads the cached
	 * state below rather than consuming events, or inputs would be lost or replayed.
	 *
	 * Phase 3: still empty. Phase 4 fills FCharacterDefaultInputs.
	 */
	virtual void ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult) override;

	// --- Input assets. Assigned by the child Blueprint; the Game Animation Sample's
	//     /Game/Input/IA_* and IMC_Sandbox can be dropped straight in. ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	int32 MappingContextPriority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> WalkAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> StrafeAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> CrouchAction;

	/** IA_Traverse. Shares the jump key in IMC_Sandbox, so the Blueprint decides which
	 *  of the two actually happens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> TraverseAction;

	// --- Cached input state. The Blueprint animation bridge reads these to fill
	//     S_PlayerInputState. Writable, not read-only, because the sample's traversal
	//     system pushes a desired input state back through Set_CharacterInputState while
	//     a traversal is running - the pawn is not the only author of these. ---

	UPROPERTY(BlueprintReadWrite, Category = "Input State")
	bool bWantsToAim = false;

	UPROPERTY(BlueprintReadWrite, Category = "Input State")
	bool bWantsToSprint = false;

	UPROPERTY(BlueprintReadWrite, Category = "Input State")
	bool bWantsToWalk = false;

	UPROPERTY(BlueprintReadWrite, Category = "Input State")
	bool bWantsToStrafe = false;

	UPROPERTY(BlueprintReadWrite, Category = "Input State")
	bool bWantsToCrouch = false;

	// --- Orientation tuning ---

	/** Face the direction of travel. Aiming or strafing overrides this and faces the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bOrientRotationToMovement = true;

	/** Keep turning towards the last intended facing after input stops, so a quick stick
	 *  flick finishes its turn instead of freezing part-way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bMaintainLastInputOrientation = true;

	/** Re-express movement relative to whatever the pawn is standing on, so it does not
	 *  slide off moving platforms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bUseBaseRelativeMovement = true;

	/** Horizontal speed below which the pawn counts as standing still for the purpose of
	 *  choosing a movement direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0"))
	float MovementDirectionSpeedThreshold = 10.0f;

	// --- Movement settings pushed into Mover's shared settings.
	//
	//     The defaults below are the Game Animation Sample's own tuned values, read from
	//     SandboxCharacter_Mover at runtime, not Mover's defaults. Mover ships MaxSpeed
	//     800 and Acceleration 4000; the motion matching databases were authored around
	//     300 and 500. Running at the plugin defaults makes the pawn outpace every
	//     animation it owns, so the feet slide and starts and stops never get a chance
	//     to play. ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Speed", meta = (ClampMin = "0.0", ForceUnits = "cm/s"))
	float WalkSpeed = 165.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Speed", meta = (ClampMin = "0.0", ForceUnits = "cm/s"))
	float RunSpeed = 375.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Speed", meta = (ClampMin = "0.0", ForceUnits = "cm/s"))
	float SprintSpeed = 585.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Speed", meta = (ClampMin = "0.0", ForceUnits = "cm/s"))
	float CrouchSpeed = 165.0f;

	// Acceleration is per gait in the sample too. Sprint accelerates slowest, which is
	// what gives the sprint build-up its weight.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Acceleration", meta = (ClampMin = "0.0", ForceUnits = "cm/s2"))
	float WalkAcceleration = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Acceleration", meta = (ClampMin = "0.0", ForceUnits = "cm/s2"))
	float RunAcceleration = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Acceleration", meta = (ClampMin = "0.0", ForceUnits = "cm/s2"))
	float SprintAcceleration = 300.0f;

	/**
	 * Braking. The sample's own CommonLegacyMovementSettings carries 0 here with zero
	 * ground friction, but that is safe only because its custom BP_MovementMode_Walking
	 * brakes by itself using StoppingDeceleration = 1000. This pawn runs Mover's stock
	 * UWalkingMode, which brakes from these two values - copying the sample's zeroes
	 * would leave it sliding forever.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0", ForceUnits = "cm/s2"))
	float MovementDeceleration = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0"))
	float MovementGroundFriction = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0", ForceUnits = "degrees/s"))
	float MovementTurningRate = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float MovementMaxStepHeight = 30.0f;

private:
	/** Writes the tunables above into Mover's shared settings. Cannot run in the
	 *  constructor: UMoverComponent only populates SharedSettings in OnRegister. */
	void ApplyMovementSettings();

	/** Hands the view to the gameplay camera when one is assigned, or to the spring arm
	 *  camera when it is not. */
	void SetupCamera();

	/** Applies DefaultMappingContext. Safe to call more than once. */
	void SetupPlayerInputContext();

	/** Guards against re-adding the mapping context on a second possession. */
	bool bInputContextApplied = false;

	void OnMoveTriggered(const FInputActionValue& Value);
	void OnMoveCompleted(const FInputActionValue& Value);
	void OnLookTriggered(const FInputActionValue& Value);
	void OnJumpStarted(const FInputActionValue& Value);
	void OnJumpReleased(const FInputActionValue& Value);
	void OnSprintStarted(const FInputActionValue& Value);
	void OnSprintReleased(const FInputActionValue& Value);
	void OnWalkStarted(const FInputActionValue& Value);
	void OnWalkReleased(const FInputActionValue& Value);
	void OnAimStarted(const FInputActionValue& Value);
	void OnAimReleased(const FInputActionValue& Value);
	void OnStrafeToggled(const FInputActionValue& Value);
	void OnCrouchToggled(const FInputActionValue& Value);
	void OnTraverseStarted(const FInputActionValue& Value);

	/** Root, and therefore what Mover moves: UMoverComponent falls back to the actor's
	 *  root component when no updated component is set explicitly. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCapsuleComponent> CollisionCapsule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	/**
	 * Drives the sample's camera rigs. Assign CameraAsset_SandboxCharacter to its
	 * CameraReference in the child Blueprint and the existing CHT_CameraRig chooser
	 * handles aim, strafe, crouch and collision on its own - the aim rigs are already
	 * authored (CameraRig_Close_Aim and friends).
	 *
	 * Left unassigned, BeginPlay falls back to the spring arm above, so the pawn is
	 * still playable before the camera asset is hooked up.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGameplayCameraComponent> GameplayCamera;

	/** Registers the Walking / Falling / Flying modes and their transitions in its own
	 *  constructor, so creating it is enough to get working movement. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCharacterMoverComponent> MoverComponent;

	/** Lets the navigation system drive this pawn. Present so AI or nav-driven movement
	 *  can reuse the same pawn later; harmless while only a player possesses it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNavMoverComponent> NavMoverComponent;

	/**
	 * The same component the Crawler uses. AEnemyCharacter::PerformAttack looks for
	 * UHealthComponent specifically, so anything the enemy is meant to hurt must carry
	 * this one - the Combat template's ICombatDamageable interface is a separate system
	 * the enemy knows nothing about.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHealthComponent> HealthComponent;

	/** Drives the root-motion warping the traversal montages rely on to land exactly on
	 *  the ledge the trace found, instead of wherever the animation happens to end. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMotionWarpingComponent> MotionWarping;

	/** Control-space movement intent as (forward, right, 0), written by input and read by
	 *  ProduceInput. Stored in Unreal's axis order, not the input action's. */
	FVector CachedMoveInputIntent = FVector::ZeroVector;

	/** Set by the navigation system rather than by input. When non-zero it wins over
	 *  the intent above, because a nav request is an explicit velocity. */
	FVector CachedMoveInputVelocity = FVector::ZeroVector;

	/** The last facing the pawn actually wanted. Used to finish a turn after input stops. */
	FVector LastAffirmativeOrientationIntent = FVector::ZeroVector;

	/** Mirrors of what ProduceInput handed Mover, for the animation bridge to read. */
	FVector LastWorldMoveIntent = FVector::ZeroVector;

	/** @see GetFloorNormal. Defaults match a pawn standing on flat ground. */
	FVector FloorNormal = FVector::UpVector;
	FVector FloorLocation = FVector::ZeroVector;
	bool bHasFloor = false;
	FVector BasedMovementDelta = FVector::ZeroVector;
	FVector LastOrientationIntent = FVector::ZeroVector;

	/** Held state: survives across simulation frames until the key is released. */
	bool bIsJumpPressed = false;

	/** Edge state: true for exactly one simulation frame. ProduceInput clears it, which
	 *  is why it must not be cleared anywhere else - a jump would be swallowed if the
	 *  game thread ticks slower than the Mover simulation. */
	bool bIsJumpJustPressed = false;
};
