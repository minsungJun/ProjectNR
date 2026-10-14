// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NRGridTypes.h"
#include "NREncounterData.generated.h"

class AEnemyCharacter;

/** How a kind of room fills itself from its spawn points. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNREncounterRule
{
	GENERATED_BODY()

	/**
	 * Share of the room's points (boss points aside) that get an enemy: a random value between
	 * these two, rounded. 10 points at 0.6-0.8 give 6 to 8 enemies.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinUseRatio = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxUseRatio = 0.8f;

	/** Bounds on that count, boss aside; never more than there are usable points. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0"))
	int32 MinEnemies = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0"))
	int32 MaxEnemies = 99;

	/** Most enemies that shoot from afar in the room. Ranged and Any points past it get a close-in enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0"))
	int32 MaxRanged = 2;

	/** Chance an Any point gets a ranged enemy, while under MaxRanged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AnyRangedChance = 0.4f;
};

/**
 * What a fighting room spawns when the player walks in: the rules per kind of room and the enemy
 * classes. Referenced from the map generation config. @see UNRSpawnSubsystem, ANRSpawnPoint
 */
UCLASS(BlueprintType)
class PROJECTNR_API UNREncounterData : public UDataAsset
{
	GENERATED_BODY()

public:
	UNREncounterData();

	const FNREncounterRule& GetRule(ENRGridRoomType RoomType) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules")
	FNREncounterRule Combat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules")
	FNREncounterRule Elite;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rules")
	FNREncounterRule Boss;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemies")
	TSubclassOf<AEnemyCharacter> MeleeClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemies")
	TSubclassOf<AEnemyCharacter> RangedClass;

	/** The boss room's boss, on its Boss point. Empty until there is a boss: the room then holds its minions only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemies")
	TSubclassOf<AEnemyCharacter> BossClass;

	/** Points closer than this to the player when the room wakes are left out, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0"))
	float MinPlayerDistance = 600.0f;

	/** Picked points keep at least this far apart while there are others to choose, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0"))
	float MinPointSpacing = 300.0f;

	/** Seconds the picked places are marked before the enemies appear. 0 spawns at once. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0"))
	float TelegraphSeconds = 0.8f;
};
