// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NRGridTypes.h"
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

/** Which environments a generated map has. Independent of its shape. */
UENUM(BlueprintType)
enum class ENRMapEnvironmentMode : uint8
{
	/** Every room and hall cell indoors. */
	IndoorOnly,
	/** Every room and hall cell outdoors. */
	OutdoorOnly,
	/** Both: the start side is one, the boss side the other, and the main path crosses between them
	 *  through Transition rooms. */
	Hybrid
};

/** How a Hybrid map decides which rooms are Indoor, Outdoor and Transition. */
UENUM(BlueprintType)
enum class ENRHybridAssignment : uint8
{
	/** Before placement: the main path is cut into the start's side, a run of Transition rooms and
	 *  the boss's side; branches keep the side they grow from. */
	PathSplit,
	/** After placement: every room takes the side it is nearer to, counted in corridors from the
	 *  start and from the boss (weighed by HybridSplit); wherever the two sides meet, one of the two
	 *  rooms becomes Transition. */
	DistanceFlood
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

	/**
	 * Indoor, outdoor or both. Hybrid: the start room is HybridStartEnvironment, the boss the other,
	 * with Transition rooms or hall cells wherever the two sides meet - placed by HybridAssignment.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Environment")
	ENRMapEnvironmentMode EnvironmentMode = ENRMapEnvironmentMode::IndoorOnly;

	/** Hybrid: the start room's environment, Indoor or Outdoor. Ignored with bRandomizeHybridStart. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Environment")
	ENRRoomEnvironment HybridStartEnvironment = ENRRoomEnvironment::Indoor;

	/** Hybrid: draw the start environment from the seed instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Environment")
	bool bRandomizeHybridStart = false;

	/** Hybrid: how environments are given out. PathSplit uses MinTransitionRooms-MaxTransitionRooms,
	 *  DistanceFlood HybridSplit and MinTransitionCells-MaxTransitionCells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Environment")
	ENRHybridAssignment HybridAssignment = ENRHybridAssignment::PathSplit;

	/**
	 * DistanceFlood: where the two sides meet, as a fraction of the way from the start to the boss.
	 * 0.5 halfway; lower gives the boss's side more of the map, higher the start's side.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Environment", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float HybridSplit = 0.5f;

	/** DistanceFlood: Transition rooms and hall cells in the whole map. More places where the two
	 *  sides touch (through branches and loops) need more; a layout outside the range is retried. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Environment", meta = (ClampMin = "1"))
	int32 MinTransitionCells = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Environment", meta = (ClampMin = "1"))
	int32 MaxTransitionCells = 3;

	/** Hybrid PathSplit: Transition rooms in a row on the main path. Hall cells between them do not count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Environment", meta = (ClampMin = "1"))
	int32 MinTransitionRooms = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Environment", meta = (ClampMin = "1"))
	int32 MaxTransitionRooms = 1;

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

	/** How many rooms deep a side branch may go from the main path. Hall cells do not count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation", meta = (ClampMin = "1"))
	int32 MaxBranchDepth = 2;

	/**
	 * Hall cells: a cell given over to a corridor instead of a room - straight, a corner, a T or a
	 * crossing, by the corridors it joins. Not counted in MinRooms/MaxRooms. Some stand between
	 * rooms on the main path, the rest on side branches with a room beyond each. 0 and 0 for none.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Halls", meta = (ClampMin = "0"))
	int32 MinHalls = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Halls", meta = (ClampMin = "0"))
	int32 MaxHalls = 0;

	/** Most hall cells in a row between two rooms of the main path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Halls", meta = (ClampMin = "1"))
	int32 MaxHallsInRow = 2;

	/** Chance that a side branch grows off a free side of a hall cell, when one has one, rather
	 *  than off any room: what turns hall cells into T junctions and crossings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Halls", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HallBranchBias = 0.6f;

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

	/** Corridors to choose from by the environments of the two rooms they join. Every pair the map
	 *  can have must be covered (all five in a Hybrid map). Empty: CorridorTemplate for every link. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Templates")
	TArray<TObjectPtr<UNRCorridorTemplateData>> CorridorTemplates;

	/** Fallback when CorridorTemplates is empty: the one straight corridor every connection uses,
	 *  turned for north-south links. Its environments are not looked at. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Templates")
	TObjectPtr<UNRCorridorTemplateData> CorridorTemplate;

	/** What fighting rooms spawn from their spawn points. Empty: the class defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounters")
	TObjectPtr<UNREncounterData> Encounters;
};
