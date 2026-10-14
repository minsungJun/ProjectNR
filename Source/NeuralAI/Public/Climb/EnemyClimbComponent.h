// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Climb/EnemyClimbProbe.h"
#include "EnemyClimbComponent.generated.h"

class ACharacter;

UENUM(BlueprintType)
enum class EEnemyClimbPhase : uint8
{
	None,
	/** Walking to the foot of the wall. */
	Approach,
	/** At the foot: turning to the wall and settling against it. */
	Align,
	/** Going straight up the face (or down it). */
	Climb,
	/** Pulling over the edge to stand on top (or back over it to start down). */
	Mantle
};

/**
 * Carries out one climb found by FEnemyClimbProbe: walk to the foot of the wall, turn to it, go
 * up its face at ClimbSpeed, pull over the edge, walk on.
 *
 * A climb, once started, is seen through: the controller leaves the body alone while IsClimbing()
 * - no strikes, no dodges, no walking. It only ends early when the way is blocked for StuckTime
 * (the enemy drops down) or when the enemy dies (it falls). On the wall the movement component is
 * switched off and the body is moved here, swept against the world; the body mesh is laid
 * against the wall through AEnemyCharacter::SetClimbPose.
 */
UCLASS(ClassGroup = (NeuralAI), meta = (BlueprintSpawnableComponent))
class NEURALAI_API UEnemyClimbComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyClimbComponent();

	/**
	 * Starts climbing Ledge, or the nearest free spot along the same wall when someone else is
	 * climbing there. False if already climbing, the owner cannot climb, or every spot near is taken
	 * (looked for again after ClimbSpotRetryInterval).
	 */
	bool StartClimb(const FEnemyClimbLedge& Ledge);

	/**
	 * Starts down Ledge from its top: walk to where one stands on top, turn to face back the way
	 * a climber up would, over the edge backwards, down the face, onto the floor. The same spot
	 * rules as StartClimb.
	 */
	bool StartClimbDown(const FEnemyClimbLedge& Ledge);

	/**
	 * Drops off Ledge's top, for one too low to climb down: walk to where one stands on top, step
	 * out past the edge and fall. Left to the movement component, an enemy pushed at an edge by its
	 * path and its strafe hangs there instead of going over.
	 */
	bool StartDrop(const FEnemyClimbLedge& Ledge);

	/** On the way down the wall rather than up it. */
	UFUNCTION(BlueprintPure, Category = "Climb")
	bool IsClimbingDown() const { return IsClimbing() && bDown && !bDrop; }

	/** On the way to dropping off an edge. */
	UFUNCTION(BlueprintPure, Category = "Climb")
	bool IsDropping() const { return IsClimbing() && bDrop; }

	/** Gives up: off the wall, falling straight down. Nothing happens when not climbing. */
	void Cancel(const TCHAR* Reason);

	/** The owner died mid-climb: it lets go and falls, still landing on the floor. */
	void DropDead();

	/** On the way to a wall, on it, or over its edge. */
	UFUNCTION(BlueprintPure, Category = "Climb")
	bool IsClimbing() const { return Phase != EEnemyClimbPhase::None; }

	/** Off the floor, on the wall or its edge. */
	UFUNCTION(BlueprintPure, Category = "Climb")
	bool IsOnWall() const { return Phase == EEnemyClimbPhase::Align || Phase == EEnemyClimbPhase::Climb || Phase == EEnemyClimbPhase::Mantle; }

	UFUNCTION(BlueprintPure, Category = "Climb")
	EEnemyClimbPhase GetPhase() const { return Phase; }

	const FEnemyClimbLedge& GetLedge() const { return Ledge; }

	/** Wall was given up on less than FailedWallCooldown ago. */
	bool IsWallExcluded(const AActor* Wall) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	ACharacter* GetCharacter() const;
	bool Begin(const FEnemyClimbLedge& Ledge, bool bInDown, bool bInDrop);
	void EnterPhase(EEnemyClimbPhase NewPhase);
	void Finish();

	/** Ends the climb, the body upright again. bFall: off the wall, falling - the movement component takes over. */
	void LetGo(bool bFall);

	/** Sweeps the body to Location. Gives up (false, the climb cancelled) after StuckTime without getting any nearer Goal. */
	bool MoveBody(const FVector& Location, const FVector& Goal);
	bool IsStuck(float Remaining);

	void SetPose(float Alpha) const;
	float Now() const;

	FEnemyClimbLedge Ledge;
	EEnemyClimbPhase Phase = EEnemyClimbPhase::None;

	/** Going down: the phases run Approach (to the top), Align, Mantle (back over the edge), Climb (down). */
	bool bDown = false;

	/** Dropping off: Approach (to the top), Align (facing out), Mantle (out past the edge), then a fall. */
	bool bDrop = false;
	float PhaseStartTime = 0.0f;
	float ClimbStartTime = 0.0f;

	/** Where the body and its heading were when the phase began. */
	FVector PhaseStartLocation = FVector::ZeroVector;
	FRotator PhaseStartRotation = FRotator::ZeroRotator;

	/** Nearest the phase's goal so far, and when it last got nearer. */
	float BestRemaining = 0.0f;
	float LastProgressTime = 0.0f;

	FString LastBlocker;

	/** When every spot near was last found taken. */
	float LastSpotTakenTime = -1000.0f;

	/** The last wall given up on, and when. */
	TWeakObjectPtr<AActor> FailedWall;
	float FailedWallTime = -1000.0f;
};
