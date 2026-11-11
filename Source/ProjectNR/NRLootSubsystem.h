// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NRItemTypes.h"
#include "NRLootTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "NRLootSubsystem.generated.h"

class ANRItemPickup;
class UHealthComponent;

/**
 * Drops items where enemies die.
 *
 * Listens to UHealthComponent::OnAnyDamaged like the run subsystem, so no enemy class or
 * Blueprint has to opt in: any AEnemyCharacter that takes its killing hit rolls its loot table
 * (UNRLootSettings::FindEnemyLoot) and spawns what comes up on the floor round it.
 */
UCLASS()
class PROJECTNR_API UNRLootSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Spawns a pickup for Drop (a weapon part with its part and level) on the floor at or below Location. */
	ANRItemPickup* SpawnDrop(const FNRLootDrop& Drop, const FVector& Location);

	/** The same, a little off to a random side: for several drops from one body. */
	ANRItemPickup* SpawnDropNear(const FNRLootDrop& Drop, const FVector& Location);

	/** A plain item (not a weapon part). Also used by the test command. */
	ANRItemPickup* SpawnPickup(ENRItemType ItemType, int32 Count, const FVector& Location);

private:
	void HandleAnyDamaged(UHealthComponent* Health, float Damage);

	FDelegateHandle AnyDamagedHandle;
};
