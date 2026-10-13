// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Core/EnemyActionTypes.h"
#include "EnemyAIController.generated.h"

class AEnemyCharacter;
class UStateTreeAIComponent;

/**
 * Turns neural drives into actual behaviour.
 *
 * Strict division of labour: the brain produces competing desires, and this class owns
 * every rule about acting on them - thresholds, cooldowns, range checks, and how long
 * an action must be seen through before another may replace it. The brain holds no
 * thresholds; this class holds no opinions about danger.
 */
UCLASS()
class NEURALAI_API AEnemyAIController : public AAIController
{
	GENERATED_BODY()

public:
	AEnemyAIController();

	virtual void Tick(float DeltaTime) override;

	/**
	 * One full sense-think-act pass.
	 *
	 * Public because the StateTree Combat task calls it: the state tree decides WHEN the
	 * enemy is allowed to fight, and this runs what fighting consists of. Splitting it
	 * out is what keeps the two layers from bleeding into each other.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy AI")
	void RunNeuralPipeline(float DeltaTime);

	/**
	 * Hands control of the pipeline to the state tree.
	 *
	 * While set, this controller stops running the pipeline from its own Tick. Left
	 * unset when no state tree asset is assigned, so the enemy still behaves exactly as
	 * it did in Phase 8 rather than going inert.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy AI")
	void SetDrivenByStateTree(bool bDriven) { bDrivenByStateTree = bDriven; }

	UFUNCTION(BlueprintPure, Category = "Enemy AI")
	bool IsDrivenByStateTree() const { return bDrivenByStateTree; }

	/** Clears momentum and any in-flight commitment, for entering Idle or Stunned. */
	UFUNCTION(BlueprintCallable, Category = "Enemy AI")
	void HaltActions();

	UFUNCTION(BlueprintPure, Category = "Enemy AI")
	AEnemyCharacter* GetEnemyCharacter() const { return ControlledEnemy; }

	UFUNCTION(BlueprintPure, Category = "Enemy AI")
	FVector GetDesiredMovement() const { return DesiredMovement; }

	UFUNCTION(BlueprintPure, Category = "Enemy AI")
	EEnemyActionType GetCurrentAction() const { return CurrentAction; }

	/** Seconds since the current action began. */
	UFUNCTION(BlueprintPure, Category = "Enemy AI")
	float GetActionElapsed() const;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	void TryDodge(float Now, float ImmediateThreat);
	void TryAttack(float Now, float ImmediateThreat);

	/**
	 * Blends the motor drives into one world-space direction and feeds it to the pawn:
	 *
	 *   Desired = TargetDir * (Approach - Retreat) + EnemyRight * (StrafeRight - StrafeLeft)
	 *
	 * Opposing drives are subtracted rather than resolved with a winner-takes-all test,
	 * so a brain that half wants to close and half wants to circle produces a genuine
	 * diagonal instead of stuttering between two choices.
	 */
	void ApplyNeuralMovement(float Now);

	/** Minimum time the given action must run before anything else may start. */
	float GetCommitTime(EEnemyActionType Action) const;

	/** True while the current action still owns the pawn. */
	bool IsCommitted(float Now) const;

	void BeginAction(EEnemyActionType Action, float Now);

	/**
	 * Refreshes the cached navigation path to the target.
	 *
	 * Runs far slower than the brain: a route is not a reflex, and recomputing it
	 * every step would cost more than it changes.
	 */
	void UpdatePathToTarget(float Now);

	/**
	 * The axis the Approach drive is spent along.
	 *
	 * This is the whole of the pathfinding integration. The brain still decides how
	 * badly to approach; this only answers which way is closer, following the navmesh
	 * around obstacles instead of pushing into them. Falls back to the straight-line
	 * bearing when no path exists, so a missing navmesh degrades to the previous
	 * behaviour rather than stopping the enemy.
	 */
	FVector GetApproachDirection() const;

	bool IsDirectionNavigable(const FVector& Direction) const;
	bool FindNavigableDirection(const FVector& Desired, FVector& OutDirection) const;

	/** Asks for a combat slot while a target is sensed, gives it back otherwise. */
	void UpdateCombatSlot();

	/** The combat slot to walk to, when slots drive movement and one is held. */
	bool GetDrivingSlot(FVector& OutLocation, bool& bOutAttackRing) const;

	/** Within the arrival radius of the slot held. */
	bool IsAtSlot(const FVector& SlotLocation) const;

	/** Turns towards the target while standing in a slot: orient-to-movement does nothing at rest. */
	void FaceTargetAtSlot(float DeltaTime);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Movement", meta = (ClampMin = "10.0"))
	float NavProbeDistance = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MovementDeadzone = 0.05f;

	/** Seconds between path recomputations. 4Hz is plenty for a route that rarely changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Movement", meta = (ClampMin = "0.05"))
	float PathUpdateInterval = 0.25f;

	/**
	 * Path points closer than this are treated as already reached.
	 *
	 * Without it the enemy aims at a corner it is standing on and jitters; with it the
	 * heading always points at somewhere it still has to go.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Movement", meta = (ClampMin = "10.0"))
	float PathPointAcceptance = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Dodge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DodgeThreshold = 0.55f;

	/** Seconds from the start of one dodge until the next may start. 1.2 until 2026-10-13, then
	 *  briefly 0.4. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Dodge", meta = (ClampMin = "0.0"))
	float DodgeCooldown = 0.6f;

	/** Sideways launch speed of a dodge, cm/s. The enemy flies for about 0.05 s and then brakes
	 *  on the ground, so distance is not proportional: 700 carried it about 64 cm, 1000 about
	 *  88 cm (2026-10-13). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Dodge", meta = (ClampMin = "0.0"))
	float DodgeImpulse = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Dodge", meta = (ClampMin = "0.0"))
	float DodgeCommitTime = 0.3f;

	/** Seconds from the start of a dodge in which the enemy takes no damage. 0 turns it off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Dodge", meta = (ClampMin = "0.0"))
	float DodgeInvulnerableTime = 0.2f;

	/**
	 * Live projectile threat required before a dodge is worth spending.
	 *
	 * The same kind of feasibility check as the attack's range test: the brain is free to
	 * want a dodge, and this decides whether acting on it makes sense. Without it a dodge
	 * released by its cooldown fires on whatever drive is left over, which in practice
	 * meant dodging shots that had already flown past.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Dodge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinLiveThreatForDodge = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Attack", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AttackThreshold = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Attack", meta = (ClampMin = "0.0"))
	float AttackCooldown = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Attack", meta = (ClampMin = "0.0"))
	float AttackCommitTime = 0.4f;

	/**
	 * Immediate projectile threat that may cut a commitment short.
	 *
	 * Without this, committing to a 0.4s attack would mean standing still through an
	 * incoming shot. Only dodges are allowed to interrupt, and only above this level.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy AI|Commitment", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InterruptThreatThreshold = 0.6f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStateTreeAIComponent> StateTreeComponent;

	UPROPERTY(Transient)
	TObjectPtr<AEnemyCharacter> ControlledEnemy;

	bool bDrivenByStateTree = false;

	FVector DesiredMovement = FVector::ZeroVector;

	EEnemyActionType CurrentAction = EEnemyActionType::None;
	float ActionStartTime = -1000.0f;

	/** Corner points of the route to the target, nearest first. Empty means no path. */
	TArray<FVector> CachedPathPoints;
	float LastPathUpdateTime = -1000.0f;

	float LastDodgeTime = -1000.0f;

	/** Where the current dodge started, to log how far it carried the enemy. */
	FVector DodgeStartLocation = FVector::ZeroVector;
	float LastAttackTime = -1000.0f;
	float LastDiagnosticTime = -1000.0f;
	float LastTickDiagnosticTime = -1000.0f;
};
