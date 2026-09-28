// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Projectiles/AITestProjectile.h"
#include "NRProjectiles.generated.h"

/**
 * Player weapon projectiles.
 *
 * Both derive from AAITestProjectile rather than being new actors, because that class is
 * what the enemy sensor watches: its static registry is how enemies see incoming fire and
 * dodge it. A projectile outside that hierarchy would hit enemies that never saw it coming.
 *
 * The two differ only in their numbers - range above all - so each is just a constructor.
 */

/** Weapon 1. Long range, one bullet per shot. */
UCLASS()
class PROJECTNR_API ANRRifleBullet : public AAITestProjectile
{
	GENERATED_BODY()

public:
	ANRRifleBullet();
};

/** Weapon 2. One of several fired together; short range, small and individually weak. */
UCLASS()
class PROJECTNR_API ANRShotgunPellet : public AAITestProjectile
{
	GENERATED_BODY()

public:
	ANRShotgunPellet();
};
