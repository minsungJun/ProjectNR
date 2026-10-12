// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NRGridTypes.h"
#include "NRMapGenerationConfig.h"

/** What the template chooser needs to know about one room template. */
struct FNRRoomTemplateDesc
{
	/** Doorways the template has, in its own frame. */
	uint8 PortMask = 0b1111;
	/** One bit per ENRGridRoomType it may stand for. */
	uint8 TypeMask = 0;
	/** One bit per quarter turn it may be placed at. */
	uint8 RotationMask = 0b1111;
	float Weight = 1.0f;
};

/**
 * Builds the connection graph of a run map on a square grid. Pure: no world, no actors, no
 * level loading - ANRMapGenerator turns the result into level instances.
 *
 * Steps, per docs/CLAUDE_GRID_LEVEL_INSTANCE_PLAN.md section 7:
 *  1. Main path: a self-avoiding walk of the chosen length from a random start cell (depth-first
 *     with a search budget). Start first, boss last; safe in the middle, elite right before the
 *     boss. The boss is joined to the elite only.
 *  2. Branches: short side rooms grown off any room but the boss into free neighbouring cells,
 *     each joined to the room it grew from, until the room count is reached.
 *  3. Loops (Loop and Mixed): extra corridors between neighbouring rooms that are not yet
 *     joined, never to the boss, and never one that would let the start reach the boss
 *     without passing through the safe room and the elite.
 *  4. Every result is checked by Validate before it is returned.
 *
 * Deterministic: everything is drawn from one FRandomStream in a fixed order over arrays, so
 * the same seed, settings and Version give the same layout. A failed attempt retries with a
 * seed derived from the seed and the attempt number; after MaxAttempts it fails with a reason
 * rather than returning a simpler shape than the one asked for.
 */
class PROJECTNR_API FNRGridLayoutGenerator
{
public:
	/** Bump whenever a change would make a seed produce a different layout. */
	static constexpr int32 Version = 1;

	/** Whether the settings can be met at all. False with the reason when they cannot. */
	static bool CheckSettings(const FNRMapGenSettings& Settings, FString& OutError);

	/** Generates a layout for Seed. False with the reason when no valid layout was found. */
	static bool Generate(const FNRMapGenSettings& Settings, int32 Seed, FNRGeneratedLayout& OutLayout, FString& OutError);

	/** Everything wrong with Layout under Settings; empty when it is valid. */
	static TArray<FString> Validate(const FNRMapGenSettings& Settings, const FNRGeneratedLayout& Layout);

	/**
	 * Picks a template and rotation for every room: the template must allow the room's type
	 * and the rotation, and its turned doorways must include every doorway the room uses
	 * (the rest stay blocked). Weighted, from a stream seeded by the layout's seed and attempt,
	 * so it is as reproducible as the layout. False, naming the room, when nothing fits.
	 */
	static bool AssignTemplates(const TArray<FNRRoomTemplateDesc>& Templates, FNRGeneratedLayout& Layout, FString& OutError);

	/** Everything wrong with the layout's template choices; empty when they all fit. */
	static TArray<FString> ValidateTemplates(const TArray<FNRRoomTemplateDesc>& Templates, const FNRGeneratedLayout& Layout);

	/** Doorway mask as letters, e.g. "NE-W". */
	static FString MaskToString(uint8 Mask);

	/** Independent loops: corridors - rooms + 1 for a connected layout. */
	static int32 CountLoops(const FNRGeneratedLayout& Layout);

	/** Rooms off the main path. */
	static int32 CountBranchRooms(const FNRGeneratedLayout& Layout);

	/** The layout as a text grid for logs: S start, F safe, E elite, B boss, C combat on the
	 *  main path, b a branch room; - and | are corridors. North is up. */
	static FString ToAscii(const FNRMapGenSettings& Settings, const FNRGeneratedLayout& Layout);

	/** Same rooms, cells, types and corridors in the same order. */
	static bool Identical(const FNRGeneratedLayout& A, const FNRGeneratedLayout& B);

private:
	static bool TryGenerate(const FNRMapGenSettings& Settings, int32 AttemptSeed, FNRGeneratedLayout& OutLayout, FString& OutReason);
};
