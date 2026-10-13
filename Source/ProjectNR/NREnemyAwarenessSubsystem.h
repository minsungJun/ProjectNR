// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NREnemyAwarenessSubsystem.generated.h"

class APawn;
class UEnemySensorComponent;

/**
 * What enemies notice of the player's gun, before any bullet reaches them. Fast rounds leave too
 * little flight time to dodge, so enemies read the shot itself the moment it is fired:
 *
 *  - Shot ray: each time the player fires a ranged weapon, a ray is cast from the muzzle along
 *    the shot, up to the round's range or the first wall. Every enemy within ShotSenseRadius of
 *    it (wider for a spread weapon, by the cone at that distance) is told a shot passed by, and
 *    its sensor flinches away from the line. Aiming alone does nothing.
 *  - Gunshots: a shot is heard by the enemies of the room it was fired in, and nobody else -
 *    not the next room, not a room still asleep. Fired outside every room (a corridor), it is
 *    heard by no one. Only a world with no rooms at all (a sandbox) falls back to a radius.
 *
 * The enemy side (UEnemySensorComponent) knows nothing of rooms, weapons or cameras; this is
 * where the game decides who is told what.
 */
UCLASS()
class PROJECTNR_API UNREnemyAwarenessSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Shooter fired. Tells the enemies that can hear it. */
	void NotifyGunshot(AActor* Shooter);

	/**
	 * Shooter fired from Origin along Direction (a unit vector); the round flies Range cm and its
	 * pellets scatter within SpreadRadians of Direction. Tells the enemies the shot passes near.
	 */
	void NotifyShotRay(AActor* Shooter, const FVector& Origin, const FVector& Direction, float SpreadRadians, float Range);

	/** Test: keep the player's view on the nearest enemy that sees the player, for Seconds. */
	void TrackNearestEnemy(float Seconds);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Distance from the shot line, in cm, within which an enemy notices the shot (plus the
	 *  spread cone's radius at that distance). */
	float ShotSenseRadius = 250.0f;

	/** Hearing radius for a shot in a world without rooms, in cm. */
	float GunshotRadiusWithoutRooms = 4000.0f;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Sensors of the enemies that hear a shot fired at Location. */
	TArray<UEnemySensorComponent*> FindListeners(const FVector& Location, FString& OutWhere) const;

	float LastShotLogTime = -100.0f;

	TWeakObjectPtr<AActor> TrackedEnemy;
	float TrackUntil = -1.0f;
};
