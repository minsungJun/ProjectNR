// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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
 * room, so the same map always fills the same way - and spawns a matching enemy on each, awake,
 * into the room's own level (it goes when the level does). Each enemy is handed to the room, which
 * counts it like one placed by hand, and the room is told when spawning is over so it cannot clear
 * before its enemies exist. @see UNREncounterData, ANRSpawnPoint
 */
UCLASS()
class PROJECTNR_API UNRSpawnSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Rules and seed for the rooms to come. Without rules the class defaults apply. */
	void Configure(const UNREncounterData* InData, int32 InSeed);

	const UNREncounterData* GetEncounterData() const;

	/** Room has woken: pick its points and spawn. False when it has no spawn points. */
	bool SpawnForRoom(ANRRoom* Room);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** The class to spawn on Point for the kind picked, or null when there is none (no boss yet). */
	UClass* ClassFor(const ANRSpawnPoint* Point, uint8 PickedKind) const;

	/** Spawns Class standing on Point, in Room's level. */
	AEnemyCharacter* SpawnAt(ANRRoom* Room, const ANRSpawnPoint* Point, UClass* Class) const;

	UPROPERTY()
	TObjectPtr<const UNREncounterData> EncounterData;

	int32 Seed = 0;
};
