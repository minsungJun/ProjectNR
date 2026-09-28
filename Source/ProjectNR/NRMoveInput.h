// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;

namespace NRMoveInput
{
	/**
	 * World-space movement input as the player is pressing it right now, flattened to the ground.
	 *
	 * Mover's GetMovementIntent looks like the obvious source but is not: it is what the movement
	 * mode recorded during the last simulation step, and while a root motion layered move owns
	 * the character (a melee lunge) that intent stops following the keys.
	 *
	 * The sample pawn computes the real thing in its Blueprint Get_MoveInput (IA_Move rotated by
	 * the control yaw), so that is called directly; Mover's intent is the fallback for pawns
	 * without it.
	 */
	PROJECTNR_API FVector Read(AActor* Owner);
}
