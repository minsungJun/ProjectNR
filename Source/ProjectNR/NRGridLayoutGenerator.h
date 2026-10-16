// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NRGridTypes.h"
#include "NRMapGenerationConfig.h"

/** What the generator needs to know about one room template. */
struct PROJECTNR_API FNRRoomTemplateDesc
{
	/** Cells covered along the template's own X and Y. */
	FIntPoint Footprint = FIntPoint(1, 1);
	/** Doorways, in the template's own frame. */
	TArray<FNRRoomPort> Ports;
	/** One bit per ENRGridRoomType it may stand for. */
	uint8 TypeMask = 0;
	/** One bit per quarter turn it may be placed at. */
	uint8 RotationMask = 0b1111;
	float Weight = 1.0f;

	/** Every side on the footprint's outline facing one of the directions in PortMask. */
	static TArray<FNRRoomPort> OutlinePorts(const FIntPoint& Footprint, uint8 PortMask);

	/** A template from footprint, doorway directions and room types (one bit per ENRGridRoomType). */
	static FNRRoomTemplateDesc Make(const FIntPoint& Footprint, uint8 PortMask, uint8 TypeMask, float Weight = 1.0f);
};

/**
 * Builds the layout of a run map on a square grid: which rooms there are, which cells each one
 * covers, which template stands in each and how it is turned, and the corridors between them.
 * Pure: no world, no actors, no level loading - ANRMapGenerator turns the result into level
 * instances.
 *
 * Rooms may cover several cells (a rectangle per template). A corridor joins two rooms across
 * the side two of their cells share, and only where both templates have a doorway there.
 *
 * Hall cells (ENRGridRoomType::Hall, MinHalls-MaxHalls) are placed like rooms but are not counted
 * as rooms: some stand between rooms on the main path (at most MaxHallsInRow in a row, never
 * right before the boss), the rest grow on side branches, each with a room beyond it. Side
 * branches lean towards free sides of hall cells (HallBranchBias), which makes T junctions and
 * crossings. A hall cell joins at least two corridors and becomes a template with exactly the
 * doorways it uses, so no hall has a blocked stub. Hall cells do not count towards branch depth.
 *
 *  1. Main path: rooms placed one after another from a random start, each against a doorway of
 *     the one before (depth-first with a search budget). Start first, boss last; safe in the
 *     middle, elite right before the boss. The boss is joined to the elite only, and stands at
 *     least MinStartBossDistance cells from the start room.
 *  2. Branches: side rooms placed against free doorways of any room but the boss, each joined to
 *     the room it grew from, until the room count is reached. The start room takes branches
 *     and loops only up to MaxStartConnections corridors in all.
 *  3. Loops (Loop and Mixed): extra corridors between rooms that face each other doorway to
 *     doorway but are not yet joined, never to the boss, and never one that would let the start
 *     reach the boss without passing through the safe room and the elite.
 *  4. Templates: every room becomes a turn of a template that allows its type, covers its cells
 *     and has every doorway it uses, weighted against the others that do.
 *  5. Every result is checked by Validate before it is returned.
 *
 * Steps 1-3 place rooms by shape: a size, with every doorway place some template of that size
 * and type has at some allowed turn. A doorway is only used while some template still has all
 * the room's doorways, so step 4 always finds one. Without templates every room is a one-cell
 * room with four doorways and TemplateIndex INDEX_NONE.
 *
 * Deterministic: everything is drawn from one FRandomStream in a fixed order over arrays, so
 * the same seed, settings, templates and Version give the same layout. A failed attempt retries
 * with a seed derived from the seed and the attempt number; after MaxAttempts it fails with a
 * reason rather than returning a simpler shape than the one asked for.
 */
class PROJECTNR_API FNRGridLayoutGenerator
{
public:
	/** Bump whenever a change would make a seed produce a different layout. */
	static constexpr int32 Version = 5;

	/** Whether the settings and templates can be met at all. False with the reason when they cannot. */
	static bool CheckSettings(const FNRMapGenSettings& Settings, const TArray<FNRRoomTemplateDesc>& Templates, FString& OutError);

	/** Generates a layout for Seed. False with the reason when no valid layout was found. */
	static bool Generate(const FNRMapGenSettings& Settings, const TArray<FNRRoomTemplateDesc>& Templates, int32 Seed,
		FNRGeneratedLayout& OutLayout, FString& OutError);

	/** One-cell rooms only, no templates. */
	static bool Generate(const FNRMapGenSettings& Settings, int32 Seed, FNRGeneratedLayout& OutLayout, FString& OutError)
	{
		return Generate(Settings, TArray<FNRRoomTemplateDesc>(), Seed, OutLayout, OutError);
	}

	/** Everything wrong with Layout under Settings and Templates; empty when it is valid. */
	static TArray<FString> Validate(const FNRMapGenSettings& Settings, const TArray<FNRRoomTemplateDesc>& Templates, const FNRGeneratedLayout& Layout);

	/** Everything wrong with one template; empty when it can be used. */
	static TArray<FString> CheckTemplate(const FNRRoomTemplateDesc& Template);

	/** Doorway mask as letters, e.g. "NE-W". */
	static FString MaskToString(uint8 Mask);

	/** Cells between two rooms' nearest cells, across plus down: 1 when they touch side by side. */
	static int32 CellDistance(const FNRGeneratedRoom& A, const FNRGeneratedRoom& B);

	/** Independent loops: corridors - rooms + 1 for a connected layout. */
	static int32 CountLoops(const FNRGeneratedLayout& Layout);

	/** Rooms off the main path (hall cells not counted). */
	static int32 CountBranchRooms(const FNRGeneratedLayout& Layout);

	/** Hall cells. */
	static int32 CountHalls(const FNRGeneratedLayout& Layout);

	/** The layout as a text grid for logs: S start, F safe, E elite, B boss, C combat on the
	 *  main path, b a branch room, + a hall cell, in every cell a room covers; = inside a room; - and | are
	 *  corridors. North is up. */
	static FString ToAscii(const FNRMapGenSettings& Settings, const FNRGeneratedLayout& Layout);

	/** Same rooms (cells, footprints, types, templates, turns) and corridors in the same order. */
	static bool Identical(const FNRGeneratedLayout& A, const FNRGeneratedLayout& B);

private:
	static bool TryGenerate(const FNRMapGenSettings& Settings, const TArray<FNRRoomTemplateDesc>& Templates, int32 AttemptSeed,
		FNRGeneratedLayout& OutLayout, FString& OutReason);
};
