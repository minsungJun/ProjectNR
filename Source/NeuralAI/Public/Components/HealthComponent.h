// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

/** Fired whenever damage is actually applied. HitDirection points from the damage source towards the owner. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHealthDamaged, float, Damage, AActor*, DamageInstigator, const FVector&, HitDirection);

/** Fired once, the first time health reaches zero. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthDeath, AActor*, Killer);

/**
 * Minimal health container shared by the test player and the enemy.
 *
 * Deliberately not a damage *system*: it stores health, clamps it, and broadcasts.
 * Whoever deals damage calls ApplyDamage directly, which keeps the hit direction
 * available for the enemy's pain sensing.
 */
UCLASS(ClassGroup = (NeuralAI), meta = (BlueprintSpawnableComponent))
class NEURALAI_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	/** Applies damage and broadcasts. Returns the amount actually removed. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	float ApplyDamage(float Amount, AActor* DamageInstigator, const FVector& HitDirection);

	/** Restores health to MaxHealth and clears the dead flag. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void ResetHealth();

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return MaxHealth; }

	/** Health as 0..1. Used directly as a neural input, so it must stay normalized. */
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthNormalized() const;

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bDead; }

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealthDamaged OnDamaged;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealthDeath OnDeath;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Health", Transient)
	float CurrentHealth = 0.0f;

	/** Latches so OnDeath can only ever broadcast once per life. */
	bool bDead = false;
};
