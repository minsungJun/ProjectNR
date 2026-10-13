// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"

class AActor;

/**
 * One place around a target an enemy can stand. Slots are rebuilt around the target every
 * assignment, so Offset and WorldLocation follow it; who holds a slot is kept separately by
 * UPositionalSlotSubsystem and copied here for drawing and logging.
 */
struct NEURALAI_API FPositionalSlot
{
	int32 Index = 0;

	/** Ring the slot belongs to. Only ring 0 exists for now; inner/middle/outer rings would add more. */
	int32 Ring = 0;

	/** Bearing from the target in degrees, 0 due +X, counter-clockwise. Fixed in world space. */
	float AngleDegrees = 0.0f;

	/** From the target to the slot, as of the last refresh. */
	FVector Offset = FVector::ZeroVector;

	/** Where the slot was as of the last refresh (projected onto the navmesh when it could be). */
	FVector WorldLocation = FVector::ZeroVector;

	/** On the navmesh and in plain sight of the target. Unreachable slots are never handed out. */
	bool bReachable = true;

	/** Enemy that holds the slot, arrived or still on its way. */
	TWeakObjectPtr<AActor> ReservedEnemy;

	/** The holder is standing in it. */
	bool bOccupied = false;

	/** Game time the current holder got it. */
	float ReservationTime = 0.0f;

	/** Score of the holder for this slot at the last assignment, for the debug view. */
	float Score = 0.0f;

	bool IsFree() const { return !ReservedEnemy.IsValid(); }
};
