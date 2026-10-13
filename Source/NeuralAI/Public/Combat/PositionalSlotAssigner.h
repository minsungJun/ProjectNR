// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"

/**
 * Which positional slot around a target each enemy should stand in. Pure: positions in, indices
 * out, no world - UPositionalSlotSubsystem feeds it and acts on the answer, and the automation
 * test drives it directly.
 *
 * Slots sit at fixed world bearings (the first due +X, then counter-clockwise), so they do not
 * swing round when the target turns.
 *
 * Assignment, nearest enemy first:
 *  1. An enemy keeps its slot while it has held it for less than HoldTime.
 *  2. Otherwise it takes the cheapest free slot, but stays where it is unless that is cheaper
 *     by SwitchGain.
 *  3. Unreachable slots are never handed out; enemies beyond the free slots get none.
 *
 * Cost is the walk to the slot: straight-line distance plus the arc around the target when the
 * slot is on its far side.
 */
class NEURALAI_API FPositionalSlotAssigner
{
public:
	struct FSlot
	{
		FVector Location = FVector::ZeroVector;
		bool bReachable = true;
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

	/** Count positions on a circle of Radius around Center, the first due +X, counter-clockwise. */
	static TArray<FVector> RingPositions(const FVector& Center, int32 Count, float Radius);

	/** The walk from From to Slot around Target: distance plus the arc past the target. */
	static float WalkCost(const FVector& Target, const FVector& From, const FVector& Slot);

	/** A slot index per claimant (same order as Claimants), INDEX_NONE when nothing is free. */
	static TArray<int32> Assign(const TArray<FSlot>& Slots, const TArray<FClaimant>& Claimants, const FParams& Params);
};
