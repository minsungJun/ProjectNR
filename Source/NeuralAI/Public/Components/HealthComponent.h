// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/NeuralTypes.h"
#include "HealthComponent.generated.h"

class UHealthComponent;

/** Fired whenever damage is actually applied. HitDirection points from the damage source towards the owner. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHealthDamaged, float, Damage, AActor*, DamageInstigator, const FVector&, HitDirection);

/** Fired once, the first time health reaches zero. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthDeath, AActor*, Killer);

/** Native, class-wide twin of OnDamaged: tells listeners *which* component was hit. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAnyHealthDamaged, UHealthComponent* /*Health*/, float /*Damage*/);

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

	/** Applies damage and broadcasts. Resistance to the player weapon it came from (Source) comes
	 *  off first, then armor takes a flat amount, then the shield soaks what is left before health
	 *  does. Returns the amount actually removed from both. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	float ApplyDamage(float Amount, AActor* DamageInstigator, const FVector& HitDirection,
		EAdaptationWeapon Source = EAdaptationWeapon::None);

	/** Share of damage from Weapon ignored, 0..0.9. None is never resisted. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetWeaponResistance(EAdaptationWeapon Weapon, float Resistance);

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetWeaponResistance(EAdaptationWeapon Weapon) const;

	/** Player weapon of the hit being broadcast (or the last one taken): OnAnyDamaged listeners
	 *  read it to tell which weapon did the damage. */
	UFUNCTION(BlueprintPure, Category = "Health")
	EAdaptationWeapon GetLastDamageSource() const { return LastDamageSource; }

	/**
	 * Regenerates Fraction of max health per second once RegenDelay seconds have passed since the
	 * last hit. 0 turns it off. Runs on a timer, so a component without regen costs nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetRegeneration(float FractionPerSecond, float DelaySeconds = 3.0f);

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetRegenPerSecond() const { return RegenPerSecond; }

	/** Ignores all damage for the next Seconds of game time. A shorter window never cuts a
	 *  longer one already running short. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetInvulnerableFor(float Seconds);

	/** Ends any invulnerability now, for damage that must land (clearing a room). */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void ClearInvulnerability() { InvulnerableUntil = -1.0f; }

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsInvulnerable() const;

	/** Restores up to Amount health, never past MaxHealth. Returns the amount restored; 0 once dead. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	float Heal(float Amount);

	/** Adds temporary health that soaks damage before health does, up to MaxShield. Returns
	 *  the amount added; 0 once dead. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	float AddShield(float Amount);

	/** Changes the cap. Raising it adds the difference to current health, so a bigger pool
	 *  arrives filled; lowering it only clamps. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetMaxHealth(float NewMaxHealth);

	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetArmor(float NewArmor) { Armor = FMath::Max(NewArmor, 0.0f); }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetArmor() const { return Armor; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentShield() const { return CurrentShield; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxShield() const { return MaxShield; }

	/** Restores health to MaxHealth, clears the shield and the dead flag. */
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

	/**
	 * Broadcast by every component after its own OnDamaged. Lets a single observer (the
	 * HUD's damage numbers) hear about hits on actors it never bound to.
	 */
	static FOnAnyHealthDamaged OnAnyDamaged;

	/** Every component that has begun play and not yet ended it, across all worlds. */
	static const TArray<TWeakObjectPtr<UHealthComponent>>& GetActiveComponents() { return ActiveComponents; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	/** Most shield the owner can hold at once. Shield is only ever added by AddShield. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "0.0"))
	float MaxShield = 100.0f;

	/** Taken off every hit before anything else. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "0.0"))
	float Armor = 0.0f;

	/** Least a hit does once armor is taken off, so armor never makes the owner untouchable.
	 *  A hit already smaller than this is not raised to it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "0.0"))
	float MinDamageAfterArmor = 1.0f;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Health", Transient)
	float CurrentHealth = 0.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Health", Transient)
	float CurrentShield = 0.0f;

	/** Latches so OnDeath can only ever broadcast once per life. */
	bool bDead = false;

	/** Game time until which ApplyDamage does nothing. */
	float InvulnerableUntil = -1.0f;

	/** Resistance per EAdaptationWeapon value. */
	float WeaponResistance[static_cast<int32>(EAdaptationWeapon::Num)] = {};

	EAdaptationWeapon LastDamageSource = EAdaptationWeapon::None;

	float RegenPerSecond = 0.0f;
	float RegenDelay = 3.0f;
	float LastDamageTime = -1000.0f;
	FTimerHandle RegenTimer;

	void TickRegeneration();

	static TArray<TWeakObjectPtr<UHealthComponent>> ActiveComponents;
};
