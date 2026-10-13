// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRProjectiles.h"

ANRRifleBullet::ANRRifleBullet()
{
	// 80 m/s, so a shot lands within about half a second across a 36 m room; it flies 100 m
	// before it is gone.
	Damage = 20.0f;
	InitialSpeed = 8000.0f;
	CollisionRadius = 12.0f;
	MaxRange = 10000.0f;

	ApplyProjectileSettings();
}

ANRShotgunPellet::ANRShotgunPellet()
{
	// Twelve of these per shot, so damage is per pellet: a point-blank shot that lands all of
	// them deals 12 x 8 = 96, and the spread thins that out quickly with distance.
	Damage = 8.0f;

	// Faster than the rifle so the whole pattern arrives at once, and gone after 15m, which
	// is what makes this a close-range weapon.
	InitialSpeed = 3000.0f;
	CollisionRadius = 5.0f;
	MaxRange = 1500.0f;

	ApplyProjectileSettings();
}
