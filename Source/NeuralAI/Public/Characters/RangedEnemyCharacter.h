// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Characters/EnemyCharacter.h"
#include "RangedEnemyCharacter.generated.h"

class AEnemyProjectile;

/**
 * The Spitter: the same neural crawler, but it keeps its distance and shoots.
 *
 * Only the attack and where it likes to stand differ: it fires an AEnemyProjectile at its target
 * from up to AttackRange (30 m), once every AttackInterval (2 s), stands in the outer, ranged
 * ring of positional slots, and does not take turns through the attack tokens - its own
 * interval paces it. It climbs like a crawler, but only when it has no place to shoot from: with
 * the target up high and every ranged slot out of its sight (since 2026-10-15; it did not climb
 * before).
 */
UCLASS()
class NEURALAI_API ARangedEnemyCharacter : public AEnemyCharacter
{
	GENERATED_BODY()

public:
	ARangedEnemyCharacter();

	/** Fires one shot at the current target. Returns 1 when a shot left, 0 otherwise. */
	virtual int32 PerformAttack() override;

	virtual int32 GetPreferredSlotRing() const override { return 1; }
	virtual bool UsesAttackTokens() const override { return false; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Ranged")
	TSubclassOf<AEnemyProjectile> ProjectileClass;

	/** cm/s. At 2000 a shot takes 1.5 s over the full 30 m, so it can be stepped out of. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Ranged", meta = (ClampMin = "100.0"))
	float ProjectileSpeed = 2000.0f;

	/** Where shots leave from, relative to the body: forward and up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Ranged")
	FVector MuzzleOffset = FVector(60.0f, 0.0f, 30.0f);

	/** Body tint, so it stands out from the crawlers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Ranged")
	FLinearColor BodyColor = FLinearColor(0.45f, 0.1f, 0.75f);

	/** Material tinted with BodyColor: BasicShapeMaterial, whose "Color" parameter the cube's own material lacks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Ranged")
	TObjectPtr<UMaterialInterface> TintMaterial;
};
