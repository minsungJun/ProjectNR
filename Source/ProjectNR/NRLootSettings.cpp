// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRLootSettings.h"

UNRLootSettings::UNRLootSettings()
{
	// Used until the table is edited in Project Settings; the ini then takes over.
	FNRLootEntry Potion;
	Potion.ItemType = ENRItemType::Potion;
	Potion.Weight = 1.0f;
	EnemyDropTable.Add(Potion);

	FNRLootEntry Shield;
	Shield.ItemType = ENRItemType::Shield;
	Shield.Weight = 1.0f;
	EnemyDropTable.Add(Shield);
}
