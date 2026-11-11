// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NRChestSpawnPoint.generated.h"

class ANRLootChest;
class UArrowComponent;
class UBoxComponent;
class UNRLootTable;

/**
 * A place a loot chest may stand in a room level.
 *
 * Put a few in a room's level (about three): when the map generator sets the rooms up, it picks
 * some of each room's points (UNRMapGenerationConfig: MinChestsPerRoom..MaxChestsPerRoom, by the
 * map's seed) and spawns a chest on each, its front the way the arrow points. The box is about the
 * crate's size, standing on the floor at the actor's origin; neither it nor the arrow is seen in
 * game. Hall cells ignore them.
 */
UCLASS()
class PROJECTNR_API ANRChestSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ANRChestSpawnPoint();

	/** Relative chance of being picked among the room's points. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest Spawn", meta = (ClampMin = "0.01"))
	float Weight = 1.0f;

	/** Spawn this chest here instead of the map's chest class. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest Spawn")
	TSubclassOf<ANRLootChest> ChestClassOverride;

	/** Give the chest here this loot table instead of its own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest Spawn")
	TObjectPtr<UNRLootTable> LootTableOverride;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Chest Spawn")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Chest Spawn")
	TObjectPtr<UBoxComponent> Shape;

	UPROPERTY(VisibleAnywhere, Category = "Chest Spawn")
	TObjectPtr<UArrowComponent> Arrow;
};
