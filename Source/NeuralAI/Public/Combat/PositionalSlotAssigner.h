// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"

/**
 * Which positional slot around a target each enemy should stand in. Pure: positions in, indices
 * out, no world - UPositionalSlotSubsystem feeds it and acts on the answer, and the automation
 * test drives it directly.
 *
 * Every free, reachable slot gets a score for each enemy, and the enemy takes the best:
 *
 *   Score = Separation * SeparationWeight            spread out from the other enemies
 *         + Flank      * FlankWeight * (1 + Caution) to the target's sides and back
 *         + Distance   * DistanceWeight * (1 + Approach)   without walking too far
 *         + Strafe bonus for the neighbouring slot on the side the enemy is strafing to
 *
 *  - Separation: the smallest angle, seen from the target, between the slot and any slot another
 *    enemy holds, over 180 degrees: 1 when it faces them across the target, 1 when alone.
 *  - Flank: (1 - cos of the angle between the target's facing and the slot), over 2: 0 in front,
 *    0.5 at the side, 1 behind.
 *  - Distance: minus the walk to the slot (straight line plus the arc round the target) over
 *    DistanceScale, so a better spread is weighed against the walk it takes.
 *  - Unreachable slots are not scored at all.
 *
 * Order and holding, so slots do not flicker:
 *  1. An enemy keeps its slot while it has held it for less than HoldTime.
 *  2. Enemies already holding a slot choose before those without, then nearest the target first.
 *     A slot someone else holds is never taken from them.
 *  3. An enemy moves only when a slot beats its own (plus CurrentSlotBonus) by SwitchThreshold.
 *     Once it holds a slot the walk counts only as the difference: a slot further than its own
 *     costs the extra walk, one nearer earns back just ShorterWalkReward of the walk saved. On its
 *     way to a slot round the far side, an enemy would otherwise drop it for any free slot it
 *     happens to pass.
 *  4. Enemies beyond the free slots get none.
 */
class NEURALAI_API FPositionalSlotAssigner
{
public:
	struct FSlot
	{
		FVector Location = FVector::ZeroVector;
		bool bReachable = true;
	};

	/** What the enemy's brain wants, as far as where to stand goes. */
	struct FIntent
	{
		/** 0..1: favours slots that are near. */
		float Approach = 0.0f;
		/** Right minus left, -1..1: favours the neighbouring slot on that side. */
		float Strafe = 0.0f;
		/** 0..1: favours the target's sides and back over its front. */
		float Caution = 0.0f;
	};

	struct FClaimant
	{
		/** Stable identifier, used to break ties so the same input gives the same answer. */
		int32 Id = 0;
		FVector Location = FVector::ZeroVector;
		/** Slot index held now, or INDEX_NONE. */
		int32 CurrentSlot = INDEX_NONE;
		/** Seconds the current slot has been held. */
		float HeldFor = 0.0f;
		FIntent Intent;
	};

	struct FWeights
	{
		float Separation = 2.0f;
		float Flank = 1.5f;
		float Distance = 1.0f;
		/** Walk, in cm, that costs one point of Distance. */
		float DistanceScale = 1000.0f;
		/** Added to the score of the slot an enemy already holds. */
		float CurrentSlotBonus = 0.2f;
		/** How much more than its own slot (with the bonus) a new one must score to be worth the move. */
		float SwitchThreshold = 0.1f;
		/** Bonus for the neighbouring slot on the side the enemy strafes to, at full strafe. */
		float Strafe = 0.5f;
		/** Share of the walk saved that counts for a slot nearer than the one held. */
		float ShorterWalkReward = 0.25f;
	};

	struct FParams
	{
		FVector Target = FVector::ZeroVector;
		/** Where the target faces; only its horizontal direction matters. */
		FVector TargetForward = FVector::ForwardVector;
		float HoldTime = 1.0f;
		FWeights Weights;
	};

	/** One slot's score for one enemy, and the parts it is made of (already weighted). */
	struct FScore
	{
		float Separation = 0.0f;
		float Flank = 0.0f;
		float Distance = 0.0f;
		float Strafe = 0.0f;
		float Total = 0.0f;
	};

	struct FResult
	{
		int32 Slot = INDEX_NONE;
		FScore Score;
	};

	/** Count positions on a circle of Radius around Center, the first due +X, counter-clockwise. */
	static TArray<FVector> RingPositions(const FVector& Center, int32 Count, float Radius);

	/** The walk from From to Slot around Target: distance plus the arc past the target. */
	static float WalkCost(const FVector& Target, const FVector& From, const FVector& Slot);

	/** Bearing of Location from Target, 0..360 degrees, 0 due +X, counter-clockwise. */
	static float BearingDegrees(const FVector& Target, const FVector& Location);

	/** Smallest difference between two bearings, 0..180 degrees. */
	static float AngleBetween(float A, float B);

	/** Claimant's score for slot SlotIndex while the slots in OtherSlots are held by others. */
	static FScore ScoreSlot(const TArray<FSlot>& Slots, int32 SlotIndex, const FClaimant& Claimant,
		const TArray<int32>& OtherSlots, const FParams& Params);

	/** A slot and its score per claimant (same order as Claimants); Slot is INDEX_NONE when nothing is free. */
	static TArray<FResult> Assign(const TArray<FSlot>& Slots, const TArray<FClaimant>& Claimants, const FParams& Params);
};
