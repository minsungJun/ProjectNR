// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The arithmetic of rounds carried: a magazine in the gun and a reserve behind it, both finite.
 * Pure, so the rules are tested on their own (ProjectNR.Ammo.Math); UNRWeaponComponent keeps the
 * numbers and calls these.
 */
namespace NRAmmo
{
	/**
	 * Fills Magazine towards MagazineSize from Reserve, as far as the reserve goes. Returns the
	 * rounds moved; 0 when the magazine is full or the reserve empty.
	 */
	inline int32 Reload(int32& Magazine, int32& Reserve, int32 MagazineSize)
	{
		const int32 Moved = FMath::Clamp(MagazineSize - Magazine, 0, FMath::Max(Reserve, 0));
		Magazine += Moved;
		Reserve -= Moved;
		return Moved;
	}

	/** Adds Rounds to Reserve up to MaxReserve. Returns the rounds actually taken. */
	inline int32 AddToReserve(int32& Reserve, int32 Rounds, int32 MaxReserve)
	{
		const int32 Taken = FMath::Clamp(MaxReserve - Reserve, 0, FMath::Max(Rounds, 0));
		Reserve += Taken;
		return Taken;
	}

	/** Whether a reload could put anything in the magazine. */
	inline bool CanReload(int32 Magazine, int32 Reserve, int32 MagazineSize)
	{
		return Magazine < MagazineSize && Reserve > 0;
	}

	/** Nothing left to shoot: an empty magazine and an empty reserve. */
	inline bool IsOutOfAmmo(int32 Magazine, int32 Reserve)
	{
		return Magazine <= 0 && Reserve <= 0;
	}
}
