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
	/** Going straight up the face. */
	Climb,
	/** Pulling over the edge to stand on top. */
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

	/** Starts climbing Ledge. False if already climbing or the owner cannot climb. */
	bool StartClimb(const FEnemyClimbLedge& Ledge);

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
	float PhaseStartTime = 0.0f;
	float ClimbStartTime = 0.0f;

	/** Where the body and its heading were when the phase began. */
	FVector PhaseStartLocation = FVector::ZeroVector;
	FRotator PhaseStartRotation = FRotator::ZeroRotator;

	/** Nearest the phase's goal so far, and when it last got nearer. */
	float BestRemaining = 0.0f;
	float LastProgressTime = 0.0f;

	FString LastBlocker;

	/** The last wall given up on, and when. */
	TWeakObjectPtr<AActor> FailedWall;
	float FailedWallTime = -1000.0f;
};
