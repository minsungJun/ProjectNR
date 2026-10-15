// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NRItemTypes.generated.h"

/**
 * Things the player can pick up. Potion and Shield are consumables (UNRConsumableComponent); Ammo
 * is a box of rounds for the guns (UNRWeaponComponent's reserves); Money is gold for the run
 * (UNRRunSubsystem). Count is how many - charges, boxes or gold.
 */
UENUM(BlueprintType)
enum class ENRItemType : uint8
{
	Potion,
	Shield,
	Ammo,
	Money
};

/** One line of a drop table: what drops, how many, and how likely it is against the other lines. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRLootEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot")
	ENRItemType ItemType = ENRItemType::Potion;

	/** Relative to the other entries' weights; 0 never drops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "1"))
	int32 Count = 1;
};

namespace NRItems
{
	/** Short upper-case name for the HUD and logs. */
	inline const TCHAR* GetDisplayName(ENRItemType Type)
	{
		switch (Type)
		{
		case ENRItemType::Shield: return TEXT("SHIELD");
		case ENRItemType::Ammo: return TEXT("AMMO");
		case ENRItemType::Money: return TEXT("GOLD");
		case ENRItemType::Potion:
		default: return TEXT("HEAL");
		}
	}

	/** Weighted pick from Table, or null when it is empty or every weight is 0. */
	inline const FNRLootEntry* PickEntry(const TArray<FNRLootEntry>& Table)
	{
		float Total = 0.0f;
		for (const FNRLootEntry& Entry : Table)
		{
			Total += FMath::Max(Entry.Weight, 0.0f);
		}
		if (Total <= 0.0f)
		{
			return nullptr;
		}

		float Roll = FMath::FRand() * Total;
		for (const FNRLootEntry& Entry : Table)
		{
			const float Weight = FMath::Max(Entry.Weight, 0.0f);
			if (Weight > 0.0f && Roll < Weight)
			{
				return &Entry;
			}
			Roll -= Weight;
		}
		// Float rounding can leave Roll a hair past the last weight.
		for (int32 Index = Table.Num() - 1; Index >= 0; --Index)
		{
			if (Table[Index].Weight > 0.0f)
			{
				return &Table[Index];
			}
		}
		return nullptr;
	}
}
