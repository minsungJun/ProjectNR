// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NRLootSettings.generated.h"

class AActor;
class ANRItemPickup;
class UNRLootTable;

/**
 * Which loot tables drop where. Project Settings > Game > NR Loot; saved to DefaultGame.ini.
 * The tables themselves are UNRLootTable data assets (/Game/DataAssets/Loot).
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "NR Loot"))
class PROJECTNR_API UNRLootSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UNRLootSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** What a killed enemy drops when its class has no table of its own below. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Enemy Drops")
	TSoftObjectPtr<UNRLootTable> EnemyLoot;

	/** A table per enemy class (Blueprint classes too). The closest class an enemy is wins. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Enemy Drops")
	TMap<TSoftClassPtr<AActor>, TSoftObjectPtr<UNRLootTable>> EnemyLootByClass;

	/** What a chest drops when it has no table of its own. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Chests")
	TSoftObjectPtr<UNRLootTable> ChestLoot;

	/** Magazines' worth one ammo box puts into each gun's reserve. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (ClampMin = "0.1"))
	float AmmoBoxMagazines = 1.0f;

	/** Spawned for every drop. Empty uses ANRItemPickup itself; set a Blueprint child to restyle it. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	TSoftClassPtr<ANRItemPickup> PickupClass;

	/** The table for a killed Enemy: its closest class in EnemyLootByClass, else EnemyLoot. May be null. */
	const UNRLootTable* FindEnemyLoot(const AActor* Enemy) const;
};
