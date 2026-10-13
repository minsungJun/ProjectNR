// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"

/**
 * Which slot around a target each enemy should stand in. Pure: positions in, indices out, no
 * world - UEnemyCombatSlotSubsystem feeds it and acts on the answer, and the automation test
 * drives it directly.
 *
 * Two rings around the target: the attack ring, close enough to strike from, and the wait ring
 * further out for whoever finds the attack ring full. Slots sit at fixed world bearings (the
 * first due +X, then counter-clockwise), so they do not swing round when the target turns.
 *
 * Assignment, nearest enemy first:
 *  1. An enemy keeps its slot while it has held it for less than HoldTime, so slots do not
 *     flicker between enemies every update.
 *  2. Otherwise it takes the cheapest free slot of the attack ring, or of the wait ring when the
 *     attack ring is full - but stays where it is unless that is cheaper by SwitchGain, or it
 *     stands in the wait ring and an attack slot is free.
 *  3. Blocked slots (off the navmesh, behind a wall) are never handed out.
 *
 * Cost is the walk to the slot: straight-line distance plus the arc around the target when the
 * slot is on its far side, so an enemy approaching from the east is not sent round to the west.
 */
class NEURALAI_API FEnemySlotAssigner
{
public:
	struct FSlot
	{
		FVector Location = FVector::ZeroVector;
		/** 0 for the attack ring, 1 for the wait ring. */
		int32 Ring = 0;
		bool bUsable = true;
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
	};

	struct FParams
	{
		FVector Target = FVector::ZeroVector;
		float HoldTime = 1.0f;
		/** Fraction by which a new slot must be cheaper to be worth moving to. */
		float SwitchGain = 0.3f;
	};

	/** Count slots on a circle of Radius around Center, the first due +X, counter-clockwise. */
	static TArray<FVector> RingPositions(const FVector& Center, int32 Count, float Radius);

	/** The walk from From to Slot around Target: distance plus the arc past the target. */
	static float Cost(const FVector& Target, const FVector& From, const FVector& Slot);

	/** A slot index per claimant (same order as Claimants), INDEX_NONE when nothing is free. */
	static TArray<int32> Assign(const TArray<FSlot>& Slots, const TArray<FClaimant>& Claimants, const FParams& Params);
};
