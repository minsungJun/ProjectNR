// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Characters/EnemyCharacter.h"
#include "BossEnemyCharacter.generated.h"

class UAnimSequence;

/** The boss's attacks. */
UENUM(BlueprintType)
enum class EBossPattern : uint8
{
	None,
	/** Roars in place facing the target, then runs at it in a straight line; the first body it meets is hit once. */
	Charge,
	/** Advances on the target swinging its front legs left and right; each swing hits what is in front. */
	Sweep,
	/** Rears up and slams both front legs down; everything within SlamRadius all round is hit. */
	Slam,
};

/**
 * The boss: the rock golem, the same neural crawler at four times the size.
 *
 * It senses and thinks like a crawler, but fights in three patterns (EBossPattern):
 *  - Slam  (A_RockGolem_Slam):  close in. Lands AttackImpactDelay (0.6 s) in, all round, SlamRadius.
 *  - Sweep (A_RockGolem_Sweep): middle range. Walks at the target for SweepDuration, four swings.
 *  - Charge (A_RockGolem_Roar, then A_RockGolem_Charge): far away. A roar to read it by, then a run
 *    in the direction the target was in when the roar ended; it stops on a hit, a wall or the end
 *    of ChargeMaxTime and stands open for ChargeRecoverTime.
 *
 * The controller still decides when a close-in attack may go (the brain's Attack drive, the
 * attack tokens, AttackRange = SweepMaxDistance); PerformAttack picks Slam or Sweep by distance.
 * The charge is the boss's own: started in Tick when the target is seen and far, on its cooldown.
 * While a pattern runs the body is the pattern's (IsAttackLocked), not the controller's.
 *
 * Damage is AttackDamage times the pattern's scale, so the run's difficulty modifiers apply.
 * The boss room spawns it on its Boss point (UNREncounterData::BossClass).
 */
UCLASS()
class NEURALAI_API ABossEnemyCharacter : public AEnemyCharacter
{
	GENERATED_BODY()

public:
	ABossEnemyCharacter();

	/** Too big for walls: it walks round. */
	virtual bool CanClimb() const override { return false; }

	/** Starts a Slam or a Sweep, by the distance to the target. Returns 1 when one started. */
	virtual int32 PerformAttack() override;

	virtual bool IsAttackLocked() const override { return Pattern != EBossPattern::None; }

	/** It never backs away: the brain's Retreat drive does not move it. */
	virtual bool CanRetreat() const override { return false; }

	virtual void Tick(float DeltaSeconds) override;

	/** Starts a pattern now, whatever the distance and cooldowns (tests, NR.Boss.Pattern). False when it cannot (dead, stunned, busy, no target for a charge). */
	UFUNCTION(BlueprintCallable, Category = "Boss")
	bool StartPattern(EBossPattern NewPattern);

	UFUNCTION(BlueprintPure, Category = "Boss")
	EBossPattern GetPattern() const { return Pattern; }

	/** The pattern for a close-in attack at Distance: Slam when in reach and ready, else Sweep. */
	static EBossPattern ChooseCloseInPattern(float Distance, float SlamMaxDistance, bool bSlamReady);

	/** Whether a charge may start: the target seen, between ChargeMinDistance and ChargeMaxStartDistance, cooldown over. */
	static bool ShouldCharge(float Distance, bool bTargetInSight, float MinDistance, float MaxDistance, bool bCooldownOver);

protected:
	virtual void BeginPlay() override;
	virtual const FEnemyVisual& GetVisual() const override;

	/** The slam lands (AttackImpactDelay, 0.6 s, into A_RockGolem_Slam): all round, SlamRadius. */
	virtual int32 ReleaseAttack() override;

	// --- Slam ---

	/** Closer than this a close-in attack is a slam (when SlamCooldown allows). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Slam", meta = (ClampMin = "0.0"))
	float SlamMaxDistance = 380.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Slam", meta = (ClampMin = "0.0"))
	float SlamRadius = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Slam", meta = (ClampMin = "0.0"))
	float SlamDamageScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Slam", meta = (ClampMin = "0.0"))
	float SlamCooldown = 5.0f;

	/** Seconds the slam holds the body: A_RockGolem_Slam's length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Slam", meta = (ClampMin = "0.1"))
	float SlamDuration = 1.2f;

	// --- Sweep ---

	/** Seconds the sweep lasts: A_RockGolem_Sweep (1.2 s, two swings) twice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Sweep", meta = (ClampMin = "0.1"))
	float SweepDuration = 2.4f;

	/** When the swings land, in seconds from the start of A_RockGolem_Sweep; repeats every SweepCycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Sweep")
	TArray<float> SweepHitTimes = { 0.32f, 0.92f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Sweep", meta = (ClampMin = "0.1"))
	float SweepCycle = 1.2f;

	/** cm/s it walks forward while sweeping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Sweep", meta = (ClampMin = "0.0"))
	float SweepSpeed = 350.0f;

	/** Degrees per second it turns after the target while sweeping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Sweep", meta = (ClampMin = "0.0"))
	float SweepTurnRate = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Sweep", meta = (ClampMin = "0.0"))
	float SweepRange = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Sweep", meta = (ClampMin = "5.0", ClampMax = "180.0"))
	float SweepHalfAngle = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Sweep", meta = (ClampMin = "0.0"))
	float SweepDamageScale = 0.5f;

	// --- Charge ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Charge", meta = (ClampMin = "0.0"))
	float ChargeMinDistance = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Charge", meta = (ClampMin = "0.0"))
	float ChargeMaxStartDistance = 2000.0f;

	/** Seconds of roar before it runs (A_RockGolem_Roar is 1.0 s; its end is the run's first pose). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Charge", meta = (ClampMin = "0.0"))
	float ChargeRoarTime = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Charge", meta = (ClampMin = "100.0"))
	float ChargeSpeed = 1600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Charge", meta = (ClampMin = "0.1"))
	float ChargeMaxTime = 2.0f;

	/** Seconds it stands after the run, open to attack. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Charge", meta = (ClampMin = "0.0"))
	float ChargeRecoverTime = 0.8f;

	/** Reach in front of its middle that the run hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Charge", meta = (ClampMin = "0.0"))
	float ChargeHitRadius = 230.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Charge", meta = (ClampMin = "0.0"))
	float ChargeDamageScale = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Charge", meta = (ClampMin = "0.0"))
	float ChargeCooldown = 8.0f;

	/** Seconds after it comes into play before the first charge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Charge", meta = (ClampMin = "0.0"))
	float FirstChargeDelay = 2.0f;

	/** Seconds after any pattern before the next may start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss", meta = (ClampMin = "0.0"))
	float PatternRecovery = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Animation")
	TSoftObjectPtr<UAnimSequence> RoarAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Animation")
	TSoftObjectPtr<UAnimSequence> ChargeAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Animation")
	TSoftObjectPtr<UAnimSequence> SweepAnimation;

private:
	void TickCharge(float Now, float DeltaSeconds);
	void TickSweep(float Now, float DeltaSeconds);
	void EndPattern(float Now);
	float GetTargetDistance2D() const;

	EBossPattern Pattern = EBossPattern::None;
	float PatternStartTime = 0.0f;
	float PatternEndTime = 0.0f;

	/** Charge: 0 roar, 1 run, 2 recover. */
	int32 ChargePhase = 0;
	float ChargePhaseTime = 0.0f;
	FVector ChargeDirection = FVector::ForwardVector;
	TSet<AActor*> ChargeHit;

	int32 SweepSwingsDone = 0;

	float NextPatternTime = 0.0f;
	float NextSlamTime = 0.0f;
	float NextChargeTime = 0.0f;
	float SavedMaxWalkSpeed = 0.0f;
	float SavedMaxAcceleration = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LoadedRoar;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LoadedCharge;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LoadedSweep;
};
