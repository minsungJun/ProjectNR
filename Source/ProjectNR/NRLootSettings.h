// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NRItemTypes.h"
#include "NRLootSettings.generated.h"

class ANRItemPickup;

/**
 * What enemies drop and how often. Project Settings > Game > NR Loot; saved to DefaultGame.ini.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "NR Loot"))
class PROJECTNR_API UNRLootSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UNRLootSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** Chance that a killed enemy drops anything at all. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Enemy Drops", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EnemyDropChance = 0.3f;

	/** When a drop happens, one entry is picked from here by weight. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Enemy Drops")
	TArray<FNRLootEntry> EnemyDropTable;

	/** Spawned for every drop. Empty uses ANRItemPickup itself; set a Blueprint child to restyle it. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	TSoftClassPtr<ANRItemPickup> PickupClass;
};
