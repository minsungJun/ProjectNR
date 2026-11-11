// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Characters/EnemyCharacter.h"
#include "BossEnemyCharacter.generated.h"

/**
 * The boss: the rock golem, the same neural crawler at four times the size.
 *
 * It fights like a crawler - same sensor, brain and controller, close-in attacks through the
 * attack tokens - but is sized for its model (UEnemyVisualSettings::Boss, the 4x rock golem,
 * about 3.4 x 4 x 2.1 m): a large capsule, a long reach, more health, a slower walk, and it
 * keeps to the floor instead of climbing.
 *
 * Its attack is a slam with both front legs (A_RockGolem_Slam): the legs go up first and come
 * down AttackImpactDelay later, and that is when the damage lands, so the wind-up can be seen
 * and stepped out of. The boss room spawns it on its Boss point (UNREncounterData::BossClass).
 */
UCLASS()
class NEURALAI_API ABossEnemyCharacter : public AEnemyCharacter
{
	GENERATED_BODY()

public:
	ABossEnemyCharacter();

	/** Too big for walls: it walks round. */
	virtual bool CanClimb() const override { return false; }

	/** Starts the slam; the damage lands AttackImpactDelay later. Returns 1 when it started. */
	virtual int32 PerformAttack() override;

protected:
	virtual const FEnemyVisual& GetVisual() const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Seconds from the start of the attack to the front legs hitting the ground (the slam's impact frame). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Attack", meta = (ClampMin = "0.0"))
	float AttackImpactDelay = 0.6f;

private:
	FTimerHandle ImpactTimer;
};
