// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NRSpawnPoint.h"
#include "Subsystems/WorldSubsystem.h"
#include "NRSpawnSubsystem.generated.h"

class AEnemyCharacter;
class ANRRoom;
class ANRSpawnPoint;
class UNREncounterData;

DECLARE_LOG_CATEGORY_EXTERN(LogNRSpawn, Log, All);

/**
 * Spawns a fighting room's enemies when the player walks in, instead of the room holding them
 * from the start.
 *
 * A room with spawn points calls SpawnForRoom as it wakes. The subsystem asks FNRSpawnPicker which
 * points to use - by the encounter rules for that kind of room, seeded by the run's seed and the
 * room, so the same map always fills the same way - marks each with an ANRSpawnMarker for
 * TelegraphSeconds, then spawns a matching enemy on each, awake, into the room's own level (it goes
 * when the level does). A place the player is standing on by then is swapped for a spare point
 * clear of them. Each enemy is handed to the room, which counts it like one placed by hand, and the
 * room is told when spawning is over so it cannot clear before its enemies exist.
 *
 * NR.Spawn.Debug 1 draws every room's points (green melee, purple ranged, yellow any, red boss)
 * and the places about to spawn; NR.Spawn.Status logs them. @see UNREncounterData, ANRSpawnPoint
 */
UCLASS()
class PROJECTNR_API UNRSpawnSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Rules and seed for the rooms to come. Without rules the class defaults apply. */
	void Configure(const UNREncounterData* InData, int32 InSeed);

	const UNREncounterData* GetEncounterData() const;

	/** Room has woken: pick its points, mark them and spawn after the telegraph. False when it has no spawn points. */
	bool SpawnForRoom(ANRRoom* Room);

	/** Logs every room's spawn points and what is waiting to spawn. */
	void LogStatus() const;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** One enemy about to appear. */
	struct FPlanned
	{
		TWeakObjectPtr<ANRSpawnPoint> Point;
		TWeakObjectPtr<UClass> Class;
		ENRSpawnKind Kind = ENRSpawnKind::Melee;
	};

	/** A room's enemies, marked and waiting for the telegraph to end. */
	struct FPending
	{
		TWeakObjectPtr<ANRRoom> Room;
		TArray<FPlanned> Planned;
		/** Usable points not picked, in drawing order: stand-ins for a place the player is on. */
		TArray<TWeakObjectPtr<ANRSpawnPoint>> Spares;
		float SpawnTime = 0.0f;
	};

	/** The class to spawn on Point for the kind picked, or null when there is none (no boss yet). */
	UClass* ClassFor(const ANRSpawnPoint* Point, ENRSpawnKind PickedKind) const;

	/** Where an enemy on Point stands: the navmesh under it. */
	FVector FloorUnder(const ANRSpawnPoint* Point) const;

	/** Spawns Class standing on Point, in Room's level. */
	AEnemyCharacter* SpawnAt(ANRRoom* Room, const ANRSpawnPoint* Point, UClass* Class) const;

	/** The telegraph is over: spawns what was planned and lets the room clear. */
	void Release(FPending& Entry);

	void DrawDebug() const;

	TArray<FPending> Pending;

	UPROPERTY()
	TObjectPtr<const UNREncounterData> EncounterData;

	int32 Seed = 0;
};
