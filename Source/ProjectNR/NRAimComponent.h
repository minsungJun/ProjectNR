// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LayeredMoveBase.h"
#include "NRAimComponent.generated.h"

class ACameraActor;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UMeshComponent;
class FNumericProperty;
struct FInputActionValue;

UENUM(BlueprintType)
enum class ENRAimMode : uint8
{
	None,
	/** Shoulder aim: the sample's own over-the-shoulder aim camera and pose, while held. */
	Shoulder,
	/** Precision aim: first person, zoomed, tighter spread. Toggled by a click. */
	Precision
};

/**
 * A short, exact turn of the capsule to the aim direction.
 *
 * The sample's walking mode reaches the aim direction through a facing spring, and when
 * standing still only turns until it is within 60 degrees (the rest is left to turn-in-place
 * animation) - entering aim while facing away therefore took a long time to line up. This
 * move turns the remaining angle in TurnSeconds, re-reading the control rotation every step
 * so it follows the mouse, and hands the current velocity straight back so the character
 * keeps walking at the same speed while it turns.
 */
USTRUCT()
struct FNRAimTurnMove : public FLayeredMoveBase
{
	GENERATED_BODY()

	UPROPERTY()
	float TurnSeconds = 0.1f;

	virtual bool GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove) override;
	virtual FLayeredMoveBase* Clone() const override;
	virtual void NetSerialize(FArchive& Ar) override;
	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FString ToSimpleString() const override;
	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

template<>
struct TStructOpsTypeTraits<FNRAimTurnMove> : public TStructOpsTypeTraitsBase2<FNRAimTurnMove>
{
	enum
	{
		WithCopy = true
	};
};

/**
 * Right mouse button aiming for ranged weapons, in two kinds.
 *
 *  - Hold: shoulder aim (the sample's aim, which used to be the only one). It starts the
 *    moment the button goes down, so holding never waits on a tap timer.
 *  - Click (released within TapThreshold): precision aim - first person, narrower field of
 *    view, less spread. Stays on until the next press; any press while in it leaves it.
 *
 * The sample maps the right button to IA_Aim, which only sets PlayerInputState.WantsToAim
 * while held. This takes the button through its own mapping context above IMC_Sandbox and
 * sets WantsToAim itself, so the sample's aim camera, strafe and upper-body pose all follow
 * without touching its assets. The melee weapon keeps the right button for its heavy attack:
 * UNRWeaponComponent turns this off while melee is in hand.
 *
 * First person is a separate camera actor at eye height, made the view target with a short
 * blend, and the character's meshes are hidden while it is used. The sample's Gameplay Camera
 * drives the pawn as a view target, so handing the view back to the pawn restores it.
 */
UCLASS(ClassGroup = (NR), meta = (BlueprintSpawnableComponent))
class PROJECTNR_API UNRAimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNRAimComponent();

	/** Called by UNRWeaponComponent: aiming exists for ranged weapons only. */
	void SetAimAllowed(bool bAllowed);

	UFUNCTION(BlueprintPure, Category = "Aim")
	ENRAimMode GetAimMode() const { return Mode; }

	UFUNCTION(BlueprintPure, Category = "Aim")
	bool IsPrecisionAiming() const { return Mode == ENRAimMode::Precision; }

	/** Back to not aiming, from either mode. */
	UFUNCTION(BlueprintCallable, Category = "Aim")
	void ExitAim();

	/** Enters a mode directly, as if by input. Ignored while aiming is not allowed (melee). */
	UFUNCTION(BlueprintCallable, Category = "Aim")
	void SetAimMode(ENRAimMode NewMode);

	/** Crosshair bloom multiplier for the current mode; UNRWeaponComponent applies it. */
	float GetSpreadScale() const { return IsPrecisionAiming() ? PrecisionCrosshairSpreadScale : 1.0f; }

	/** Projectile cone multiplier for the current mode (shotgun pattern). */
	float GetConeScale() const { return IsPrecisionAiming() ? PrecisionConeScale : 1.0f; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnPawnRestarted(APawn* Pawn);

	/** A press shorter than this is a click (precision), longer is a hold (shoulder). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Input", meta = (ClampMin = "0.05"))
	float TapThreshold = 0.2f;

	/** Must be above IMC_Sandbox's 0 so the right button stops reaching IA_Aim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Input")
	int32 InputPriority = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Precision", meta = (ClampMin = "10.0", ClampMax = "120.0"))
	float PrecisionFieldOfView = 55.0f;

	/** Seconds to blend into and out of first person. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Precision", meta = (ClampMin = "0.0"))
	float CameraBlendTime = 0.15f;

	/**
	 * Eye height, measured down from the top of the capsule. Taken from the capsule rather than
	 * the head bone: the head bobs and turns with every animation, which in first person reads
	 * as the camera shaking, and the capsule shrinks correctly when crouching.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Precision", meta = (ClampMin = "0.0"))
	float EyeBelowCapsuleTop = 14.0f;

	/** Hides every mesh on the character in first person; there is no weapon model to show. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Precision")
	bool bHideCharacterInPrecision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Precision", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PrecisionCrosshairSpreadScale = 0.4f;

	/**
	 * Seconds the character takes to turn to the aim direction while aiming and moving.
	 * The sample's walking mode turns over walk/RunFacingTime (0.4 s) whether aiming or not,
	 * which is smooth for running but sluggish when swinging the aim round behind. While
	 * aiming this replaces it (and the sprint value); the originals come back on release.
	 * 0 leaves the sample's values alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Turning", meta = (ClampMin = "0.0"))
	float AimFacingTime = 0.12f;

	/**
	 * Seconds for the snap turn (FNRAimTurnMove) that lines the body up with the aim when
	 * aiming starts. Only then: while aiming, the sample deliberately offsets the body from
	 * the aim for diagonal movement (CHT_RotationOffsetCurve), and a repeated snap would fight
	 * it. 0 disables it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Turning", meta = (ClampMin = "0.0"))
	float AimTurnTime = 0.1f;

	/** Angle between body and aim, in degrees, past which entering aim starts a snap turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Turning", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float AimTurnTriggerAngle = 30.0f;

	/** Shotgun pattern half-angle multiplier in precision aim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aim|Precision", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PrecisionConeScale = 0.6f;

private:
	void BindInput();
	void UpdateMappingContext();

	void OnAimPressed(const FInputActionValue& Value);
	void OnAimReleased(const FInputActionValue& Value);

	void SetMode(ENRAimMode NewMode);

	/** Writes the sample pawn's PlayerInputState.WantsToAim (field name has a GUID suffix). */
	void SetSampleWantsToAim(bool bWantsToAim) const;

	void EnterFirstPerson();
	void ExitFirstPerson(bool bBlend);
	void UpdateFirstPersonCamera();
	FVector GetEyeLocation() const;

	void HideCharacter();
	void RestoreCharacter();

	/** Swaps the walking mode's facing times for AimFacingTime, or puts the originals back. */
	void ApplyAimFacing(bool bAiming);

	/** Queues FNRAimTurnMove if the body is far enough off the aim and on the ground. */
	void TryStartAimTurn();

	float NextAimTurnTime = 0.0f;

	APlayerController* GetPlayerController() const;

	ENRAimMode Mode = ENRAimMode::None;
	bool bAimAllowed = true;
	bool bInputBound = false;

	/** The press that left precision aim; its release must not start anything. */
	bool bIgnoreRelease = false;
	float PressTime = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> AimContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> FirstPersonCamera;

	/** Same reason as UNRMeleeComponent::ContextSubsystem: removal needs it after the controller is gone. */
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> ContextSubsystem;

	struct FHiddenMesh
	{
		TWeakObjectPtr<UMeshComponent> Mesh;
		/** Skeletal meshes only: kept ticking while hidden so the pose, montages and root motion carry on. */
		bool bSkeletal = false;
		uint8 PreviousAnimTickOption = 0;
	};
	TArray<FHiddenMesh> HiddenMeshes;

	struct FFacingOverride
	{
		TWeakObjectPtr<UObject> Mode;
		FNumericProperty* Property = nullptr;
		double Original = 0.0;
	};
	TArray<FFacingOverride> FacingOverrides;
};
