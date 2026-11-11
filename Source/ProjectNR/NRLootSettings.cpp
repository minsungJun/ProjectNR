// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRLootSettings.h"

#include "GameFramework/Actor.h"
#include "NRLootTable.h"

UNRLootSettings::UNRLootSettings()
{
	// Used until they are changed in Project Settings; the ini then takes over.
	EnemyLoot = TSoftObjectPtr<UNRLootTable>(FSoftObjectPath(TEXT("/Game/DataAssets/Loot/DA_NRLoot_Enemy.DA_NRLoot_Enemy")));
	ChestLoot = TSoftObjectPtr<UNRLootTable>(FSoftObjectPath(TEXT("/Game/DataAssets/Loot/DA_NRLoot_Chest.DA_NRLoot_Chest")));
}

const UNRLootTable* UNRLootSettings::FindEnemyLoot(const AActor* Enemy) const
{
	if (!Enemy)
	{
		return nullptr;
	}

	// The most derived listed class the enemy is: a Blueprint's own table beats its parent's.
	const UClass* Best = nullptr;
	const TSoftObjectPtr<UNRLootTable>* BestTable = nullptr;
	for (const TPair<TSoftClassPtr<AActor>, TSoftObjectPtr<UNRLootTable>>& Pair : EnemyLootByClass)
	{
		const UClass* Class = Pair.Key.LoadSynchronous();
		if (Class && Enemy->IsA(Class) && (!Best || Class->IsChildOf(Best)))
		{
			Best = Class;
			BestTable = &Pair.Value;
		}
	}
	return BestTable ? BestTable->LoadSynchronous() : EnemyLoot.LoadSynchronous();
}
