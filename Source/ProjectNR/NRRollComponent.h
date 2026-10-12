// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "LayeredMoveBase.h"
#include "NRRollComponent.generated.h"

class UAnimMontage;
class UAnimSequenceBase;

/** Which of the four roll animations to play, relative to the way the character faces. */
UENUM(BlueprintType)
enum class ENRRollDirection : uint8
{
	Forward,
	Backward,
	Left,
	Right
};

/**
 * The roll's horizontal push, as a Mover layered move.
 *
 * The last velocity a layered move proposes is what Mover hands on when it ends, and under this
 * mix mode FinishVelocitySettings cannot change that (Mover hits check(0)), so the move shapes
 * its own speed:
 *
 *   speed(t) = Exit + (Peak - Exit) * (1 - t^2),   t = 0..1 over the move
 *
 * It surges early, where a roll carries its weight, and settles on Exit - the speed the
 * character already had along the roll direction. Ending there is what matters:
 *  - a constant roll speed handed on at a ledge flung the character off it (falling has no
 *    braking);
 *  - ending at zero fixed that but dropped a running character to a standstill mid-stride, and
 *    made ledges drop straight down.
 * Ending at the entry speed hands running back to running, and a standing roll still stops.
 *
 * The (1 - t^2) part has area 2/3, so Peak = Exit + 1.5 * (Distance / Duration - Exit) keeps
 * the ground covered at exactly Distance.
 *
 * Off the ground the surge is cut to AirborneSpeedScale but never below Exit, so a roll off an
 * edge leaves it the way running off it would.
 */
USTRUCT()
struct FNRRollLayeredMove : public FLayeredMoveBase
{
	GENERATED_BODY()

	/** Horizontal unit direction, world space. */
	UPROPERTY()
	FVector Direction = FVector::ForwardVector;

	/** Ground covered over the whole move, in cm. */
	UPROPERTY()
	float Distance = 300.0f;

	/** Speed the move settles on at the end - the entry speed along Direction. */
	UPROPERTY()
	float ExitSpeed = 0.0f;

	/** Fraction of the push that remains while not on walkable ground. */
	UPROPERTY()
	float AirborneSpeedScale = 0.35f;

	/**
	 * Turn the body to face Direction during the roll. Used when the character is not strafing:
	 * the forward roll clip then plays along the input direction. The move overrides angular
	 * velocity too, so without this the character would keep its old facing for the whole roll.
	 */
	UPROPERTY()
	bool bTurnToDirection = false;

	/** Maximum turn speed while facing Direction, degrees per second. */
	UPROPERTY()
	float TurnRate = 1440.0f;

	virtual bool SupportsAsync() const override { return true; }

	virtual bool GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove) override;
	virtual bool GenerateMove_Async(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove) override;

	virtual FLayeredMoveBase* Clone() const override;
	virtual void NetSerialize(FArchive& Ar) override;
	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FString ToSimpleString() const override;
	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;

private:
	bool GenerateRollMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, bool bOnGround, FProposedMove& OutProposedMove) const;
};

template<>
struct TStructOpsTypeTraits<FNRRollLayeredMove> : public TStructOpsTypeTraitsBase2<FNRRollLayeredMove>
{
	enum
	{
		WithCopy = true
	};
};

/**
 * Dodge roll on a key press. The animation only poses the body; code moves the capsule.
 *
 * The Paragon roll clips are in place - their root motion measures 0cm - so the travel has to
 * come from somewhere else. It comes from a Mover layered move that pushes the character along
 * the roll direction for as long as the animation plays.
 *
 * That layered move overrides everything except vertical velocity. Root motion through
 * UPlayMoverMontageCallbackProxy would be locked to OverrideAll, which also takes over the up
 * axis: a roll started at a ledge would glide out over the drop and only fall once it ended.
 * Leaving vertical velocity to the movement mode means slopes, steps and ledges behave the way
 * walking and falling already handle them.
 *
 * Montages are built at BeginPlay from the plain sequences, so no montage assets have to be
 * authored or kept in sync with them.
 */
UCLASS(ClassGroup = (NR), meta = (BlueprintSpawnableComponent))
class PROJECTNR_API UNRRollComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNRRollComponent();

	/** Rolls in the direction of movement input, or forward with no input. Returns false
	 *  when a roll is not allowed right now. */
	UFUNCTION(BlueprintCallable, Category = "Roll")
	bool TryRoll();

	UFUNCTION(BlueprintPure, Category = "Roll")
	bool IsRolling() const { return bIsRolling; }

	/** The roll that input would pick this frame when strafing. Exposed for debugging the direction choice. */
	UFUNCTION(BlueprintPure, Category = "Roll")
	ENRRollDirection GetRollDirectionFromInput() const;

	/**
	 * True while the character holds its facing - strafing or aiming. Read from the sample
	 * pawn's PlayerInputState struct by field name, since it exposes no function for it.
	 * Decides between the two ways a roll can pick its direction:
	 *  - holding facing: the four directional clips, chosen relative to the facing;
	 *  - otherwise (the sample turns towards movement): roll along the exact input direction,
	 *    turning to face it, with the forward clip. Relative to a body that already faces its
	 *    movement, input is always "forward", which is why this case needs its own rule.
	 */
	UFUNCTION(BlueprintPure, Category = "Roll")
	bool IsHoldingFacing() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Same reason as UNRWeaponComponent: a GameMode-spawned pawn has no InputComponent
	 *  during BeginPlay, so binding waits for the restart. */
	UFUNCTION()
	void OnPawnRestarted(APawn* Pawn);

	void BindInput();

	void OnRollInput();

	void OnRollMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** Sequences per direction. Played in place; any root motion they have is not used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Animation")
	TObjectPtr<UAnimSequenceBase> RollForward;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Animation")
	TObjectPtr<UAnimSequenceBase> RollBackward;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Animation")
	TObjectPtr<UAnimSequenceBase> RollLeft;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Animation")
	TObjectPtr<UAnimSequenceBase> RollRight;

	/** Montage slot. DefaultSlot sits after the rifle layer in the sample ABP, so the roll
	 *  owns the whole body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Animation")
	FName SlotName = TEXT("DefaultSlot");

	/**
	 * The roll clips were cut to follow a dive landing, so their first frame is not a
	 * standing pose. A short blend-in hides that jump; too long and the capsule starts moving
	 * before the body looks like it is rolling.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Animation", meta = (ClampMin = "0.0"))
	float BlendInTime = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Animation", meta = (ClampMin = "0.0"))
	float BlendOutTime = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Animation", meta = (ClampMin = "0.1"))
	float PlayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Input")
	FKey RollKey = EKeys::LeftControl;

	/** Ground covered by one roll on flat ground, in cm. Speed is this over the clip's length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Movement", meta = (ClampMin = "0.0"))
	float RollDistance = 300.0f;

	/** How much of the roll's surge survives once it leaves the ground. Never drops below the
	 *  entry speed, so a roll off a ledge carries the run's momentum. @see FNRRollLayeredMove */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirborneSpeedScale = 0.35f;

	/** Stamina one roll costs, from the owner's UNRStaminaComponent. Without that component
	 *  rolls are free. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll", meta = (ClampMin = "0.0"))
	float StaminaCost = 15.0f;

	/** Seconds after a roll ends before the next may start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll", meta = (ClampMin = "0.0"))
	float Cooldown = 0.25f;

	/**
	 * Diagonal rolls while holding facing. There are only four clips, so a diagonal plays the
	 * forward or backward one and the mesh is turned by the leftover 45 degrees to line the clip
	 * up with the movement - the actor's facing does not change. This is how fast the mesh turns
	 * into that offset and back out of it, degrees per second.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Movement", meta = (ClampMin = "1.0"))
	float MeshTurnRate = 450.0f;

	/** When false, holding facing snaps to four directions as before. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Movement")
	bool bEightDirections = true;

	/** How fast the body turns to the roll direction when not strafing, degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll|Movement", meta = (ClampMin = "0.0"))
	float TurnRate = 1440.0f;

	/** Movement intent below this counts as no input, which rolls forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roll", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InputDeadzone = 0.2f;

private:
	UAnimMontage* GetMontage(ENRRollDirection Direction) const;

	/** Built from the sequences at BeginPlay, indexed by ENRRollDirection. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimMontage>> RollMontages;

	bool bIsRolling = false;
	float LastRollEndTime = -1000.0f;
	bool bInputBound = false;

	/** Mesh yaw offset for diagonal rolls. Target is set at roll start and cleared at its end;
	 *  the tick moves Current towards it and restores the base rotation exactly when done. */
	void SetMeshYawOffset(float TargetDegrees);

	FRotator MeshBaseRotation = FRotator::ZeroRotator;
	float MeshYawOffsetTarget = 0.0f;
	float MeshYawOffsetCurrent = 0.0f;
	bool bMeshOffsetActive = false;
};
