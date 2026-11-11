// Copyright Epic Games, Inc. All Rights Reserved.

// Loot table rolls, no world: a part roll at 15% comes up about 15% of the time, every part that
// comes up has a part asset, and its levels follow the weights (70 / 25 / 5). Then the shipped
// tables: the enemy and chest tables each carry a weapon part roll. Automation test
// ProjectNR.Loot.Table.

#include "Misc/AutomationTest.h"
#include "NRLootSettings.h"
#include "NRLootTable.h"
#include "NRWeaponPartData.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace NRLootTableTest
{
	/** The chance of the first weapon part roll in Table, or -1 without one. */
	float PartChance(const UNRLootTable* Table)
	{
		if (Table)
		{
			for (const FNRLootRoll& Roll : Table->Rolls)
			{
				if (Roll.Entries.ContainsByPredicate([](const FNRLootEntry& Entry) { return Entry.ItemType == ENRItemType::WeaponPart; }))
				{
					return Roll.Chance;
				}
			}
		}
		return -1.0f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNRLootTableTest, "ProjectNR.Loot.Table",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNRLootTableTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UNRLootTable> Table(NewObject<UNRLootTable>(GetTransientPackage()));
	FNRLootRoll& Roll = Table->Rolls.AddDefaulted_GetRef();
	Roll.Chance = 0.15f;
	FNRLootEntry& Part = Roll.Entries.AddDefaulted_GetRef();
	Part.ItemType = ENRItemType::WeaponPart;

	constexpr int32 Tries = 40000;
	int32 Parts = 0;
	int32 Missing = 0;
	int32 Levels[3] = { 0, 0, 0 };
	TSet<const UNRWeaponPartData*> Seen;
	for (int32 Try = 0; Try < Tries; ++Try)
	{
		TArray<FNRLootDrop> Drops;
		Table->Roll(Drops);
		for (const FNRLootDrop& Drop : Drops)
		{
			++Parts;
			Missing += Drop.Part ? 0 : 1;
			Seen.Add(Drop.Part);
			++Levels[static_cast<int32>(Drop.Level)];
		}
	}

	const float Rate = float(Parts) / Tries;
	AddInfo(FString::Printf(TEXT("%d parts in %d rolls (%.3f), %d kinds; levels I %d, II %d, III %d"),
		Parts, Tries, Rate, Seen.Num(), Levels[0], Levels[1], Levels[2]));
	TestTrue(TEXT("a 15% part roll comes up about 15% of the time"), FMath::Abs(Rate - 0.15f) < 0.01f);
	TestEqual(TEXT("every part that comes up is a part asset"), Missing, 0);
	TestTrue(TEXT("an empty part list draws from more than one part"), Seen.Num() > 1);
	if (Parts > 0)
	{
		TestTrue(TEXT("level I about 70%"), FMath::Abs(float(Levels[0]) / Parts - 0.70f) < 0.03f);
		TestTrue(TEXT("level II about 25%"), FMath::Abs(float(Levels[1]) / Parts - 0.25f) < 0.03f);
		TestTrue(TEXT("level III about 5%"), FMath::Abs(float(Levels[2]) / Parts - 0.05f) < 0.015f);
	}

	const UNRLootSettings* Settings = GetDefault<UNRLootSettings>();
	TestEqual(TEXT("the enemy table drops a part 15% of the time"), NRLootTableTest::PartChance(Settings->EnemyLoot.LoadSynchronous()), 0.15f, 0.001f);
	TestEqual(TEXT("the chest table drops a part 70% of the time"), NRLootTableTest::PartChance(Settings->ChestLoot.LoadSynchronous()), 0.70f, 0.001f);
	return true;
}

#endif
