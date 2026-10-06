// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "NRItemTypes.h"
#include "NRConsumableComponent.generated.h"

class UHealthComponent;

/**
 * The player's consumables: a healing potion and a shield, each on its own key and each with a
 * number of charges.
 *
 * Both act on the owner's UHealthComponent, which is where health and shield live - the shield
 * is temporary health that soaks damage first, so every damage source respects it without
 * knowing this component exists.
 *
 * A use that would do nothing (healing at full health, a shield that is already full) is
 * refused and keeps its charge.
 */
UCLASS(ClassGroup = (ProjectNR), meta = (BlueprintSpawnableComponent))
class PROJECTNR_API UNRConsumableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNRConsumableComponent();

	/** Heals PotionHealFraction of max health. False when refused or out of potions. */
	UFUNCTION(BlueprintCallable, Category = "Consumables")
	bool UsePotion();

	/** Adds ShieldAmount of shield. False when refused or out of shields. */
	UFUNCTION(BlueprintCallable, Category = "Consumables")
	bool UseShield();

	/** Adds Count of an item, as a pickup or reward gives it. True when it was taken. */
	UFUNCTION(BlueprintCallable, Category = "Consumables")
	bool AddItem(ENRItemType ItemType, int32 Count);

	/** For rewards and pickups. Negative counts are ignored. */
	UFUNCTION(BlueprintCallable, Category = "Consumables")
	void AddCharges(int32 Potions, int32 Shields);

	UFUNCTION(BlueprintPure, Category = "Consumables")
	int32 GetPotionCount() const { return PotionCount; }

	UFUNCTION(BlueprintPure, Category = "Consumables")
	int32 GetShieldCount() const { return ShieldCount; }

	const FKey& GetPotionKey() const { return PotionKey; }
	const FKey& GetShieldKey() const { return ShieldKey; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Same reason as UNRWeaponComponent: a GameMode-spawned pawn has no InputComponent
	 *  during BeginPlay, so binding waits for the restart. */
	UFUNCTION()
	void OnPawnRestarted(APawn* Pawn);

	void BindInput();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumables|Input")
	FKey PotionKey = EKeys::Four;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumables|Input")
	FKey ShieldKey = EKeys::Five;

	/** Share of max health one potion restores. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumables", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PotionHealFraction = 0.5f;

	/** Shield one use grants, capped by the health component's MaxShield. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumables", meta = (ClampMin = "0.0"))
	float ShieldAmount = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumables", meta = (ClampMin = "0"))
	int32 StartingPotions = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumables", meta = (ClampMin = "0"))
	int32 StartingShields = 3;

private:
	UHealthComponent* FindHealth() const;

	void OnPotionInput() { UsePotion(); }
	void OnShieldInput() { UseShield(); }

	int32 PotionCount = 0;
	int32 ShieldCount = 0;
	bool bInputBound = false;
};
