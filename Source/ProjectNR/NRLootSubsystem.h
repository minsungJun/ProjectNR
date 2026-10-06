// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NRItemTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "NRLootSubsystem.generated.h"

class ANRItemPickup;
class UHealthComponent;

/**
 * Drops items where enemies die.
 *
 * Listens to UHealthComponent::OnAnyDamaged like the run subsystem, so no enemy class or
 * Blueprint has to opt in: any AEnemyCharacter that takes its killing hit rolls
 * UNRLootSettings::EnemyDropChance and, on a hit, spawns one pickup from the drop table on the
 * floor below it.
 */
UCLASS()
class PROJECTNR_API UNRLootSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Spawns a pickup on the floor at or below Location. Also used by the test command. */
	ANRItemPickup* SpawnPickup(ENRItemType ItemType, int32 Count, const FVector& Location);

private:
	void HandleAnyDamaged(UHealthComponent* Health, float Damage);

	FDelegateHandle AnyDamagedHandle;
};
