// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NREncounterData.h"
#include "NRSpawnPoint.h"

/**
 * Which of a room's spawn points get an enemy, and which kind. Pure: points in, picks out, no
 * world - UNRSpawnSubsystem feeds it and spawns the answer, and the automation test drives it.
 *
 *  1. Boss points are always used while there is a boss class, and never count towards the rest.
 *  2. Count: the room's other points x a random share between the rule's MinUseRatio and
 *     MaxUseRatio, rounded, then held within MinEnemies..MaxEnemies and the usable points.
 *  3. Required points go first; the rest are drawn by weight, skipping points within MinSpacing
 *     of one already drawn while any farther point is left.
 *  4. Kinds: Ranged and Any points give ranged enemies up to the rule's MaxRanged (Any only by
 *     AnyRangedChance), close-in ones past it. RangedBias (enemy adaptation) moves the cap and the
 *     chance, and may turn a Melee point ranged (bias above 0) or a Ranged point close-in (below 0).
 *  5. Unusable points (off the navmesh, too near the player) are never drawn, but still count
 *     in step 2, so a room keeps its size wherever the player comes in.
 *
 * The same points and the same random stream always give the same picks.
 */
class PROJECTNR_API FNRSpawnPicker
{
public:
	struct FCandidate
	{
		FVector Location = FVector::ZeroVector;
		ENRSpawnKind Kind = ENRSpawnKind::Melee;
		float Weight = 1.0f;
		bool bRequired = false;
		/** On the navmesh and far enough from the player. */
		bool bUsable = true;
	};

	struct FParams
	{
		FNREncounterRule Rule;
		/** A boss class is set, so Boss points are used. */
		bool bHasBoss = false;
		float MinSpacing = 300.0f;
		/**
		 * Enemy adaptation's shift of the mix, -1..1: above 0 more shooters (a higher ranged cap,
		 * Any points ranged more often, Melee points sometimes ranged), below 0 more close-in enemies.
		 */
		float RangedBias = 0.0f;
		/** At a bias of +-1: ranged cap change, AnyRangedChance change, chance a point changes kind. */
		int32 MaxRangedShift = 2;
		float AnyRangedChanceShift = 0.4f;
		float ConvertChance = 0.4f;
	};

	struct FPick
	{
		/** Index into the candidates. */
		int32 Candidate = INDEX_NONE;
		/** Melee, Ranged or Boss: what to spawn there. */
		ENRSpawnKind Kind = ENRSpawnKind::Melee;
	};

	static TArray<FPick> Pick(const TArray<FCandidate>& Candidates, const FParams& Params, FRandomStream& Random);
};
