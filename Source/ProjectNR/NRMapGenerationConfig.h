// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NRMapGenerationConfig.generated.h"

class UNRCorridorTemplateData;
class UNREncounterData;
class UNRRoomTemplateData;

/** The overall shape a generated map must have. */
UENUM(BlueprintType)
enum class ENRGridLayoutShape : uint8
{
	/** One chain of rooms from start to boss: no branches, no loops. */
	Linear,
	/** A tree: side branches off the main path, no loops. */
	Branching,
	/** At least one loop; branches allowed. */
	Loop,
	/** At least one branch and at least one loop. */
	Mixed
};

/**
 * Everything the layout generator needs, as a plain struct so it can be built in code (tests)
 * as well as read from a UNRMapGenerationConfig asset.
 *
 * Room counts include the start and boss rooms. The main path runs start -> ... -> safe -> ...
 * -> elite -> boss; the boss hangs off the elite only.
 */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRMapGenSettings
{
	GENERATED_BODY()

	/** Logical grid the rooms are placed in, in cells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "2"))
	FIntPoint GridSize = FIntPoint(6, 6);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation")
	ENRGridLayoutShape Shape = ENRGridLayoutShape::Mixed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "2"))
	int32 MinRooms = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "2"))
	int32 MaxRooms = 14;

	/** Rooms on the path from start to boss, both included. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "2"))
	int32 MinMainPathLength = 6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "2"))
	int32 MaxMainPathLength = 8;

	/** Loops for the Loop and Mixed shapes. Ignored by the others, which have none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "0"))
	int32 MinLoops = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "0"))
	int32 MaxLoops = 2;

	/** Most corridors the start room may have: its main path corridor plus branches and loops.
	 *  The boss always has exactly one, to the elite. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "1", ClampMax = "4"))
	int32 MaxStartConnections = 2;

	/** Fewest cells between the start room and the boss room, counted across and down between
	 *  their nearest cells: 1 is side by side, 3 leaves at least two cells between them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "1"))
	int32 MinStartBossDistance = 3;

	/** How many rooms deep a side branch may go from the main path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "1"))
	int32 MaxBranchDepth = 2;

	/** A Safe room in the middle of the main path, which the boss cannot be reached around. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation")
	bool bRequireSafe = true;

	/** An Elite room right before the boss, which the boss cannot be reached around. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation")
	bool bRequireElite = true;

	/** Retries of one seed, each with its own derived seed, before giving up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "1"))
	int32 MaxAttempts = 50;

	/** Cells the main path walk may try in one attempt before giving up on it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "1"))
	int32 MainPathSearchBudget = 400;
};

/** Map generation settings and the levels to build from, as an asset, so different runs or
 *  zones can use different ones. */
UCLASS(BlueprintType)
class PROJECTNR_API UNRMapGenerationConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ShowOnlyInnerProperties))
	FNRMapGenSettings Settings;

	/** Room levels to choose from. Every room type the layout uses must be covered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Templates")
	TArray<TObjectPtr<UNRRoomTemplateData>> RoomTemplates;

	/** The one straight corridor every connection uses, turned for north-south links. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Templates")
	TObjectPtr<UNRCorridorTemplateData> CorridorTemplate;

	/** What fighting rooms spawn from their spawn points. Empty: the class defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounters")
	TObjectPtr<UNREncounterData> Encounters;
};
