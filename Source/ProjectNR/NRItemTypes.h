// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NRWeaponPartTypes.h"
#include "NRItemTypes.generated.h"

class UNRWeaponPartData;

/**
 * Things the player can pick up. Potion and Shield are consumables (UNRConsumableComponent); Ammo
 * is a box of rounds for the guns (UNRWeaponComponent's reserves); Money is gold for the run
 * (UNRRunSubsystem); WeaponPart is one part at one level for the parts inventory
 * (UNRWeaponPartComponent). Count is how many - charges, boxes or gold.
 */
UENUM(BlueprintType)
enum class ENRItemType : uint8
{
	Potion,
	Shield,
	Ammo,
	Money,
	WeaponPart UMETA(DisplayName = "Weapon Part")
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

	/** How many: charges, boxes or gold. With MaxCount above it, anywhere from Count to MaxCount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "1"))
	int32 Count = 1;

	/** 0 (or not above Count) gives exactly Count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot", meta = (ClampMin = "0"))
	int32 MaxCount = 0;

	/** Weapon parts: which ones may come up, at random. Empty: every weapon part asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Weapon Part", meta = (EditCondition = "ItemType == ENRItemType::WeaponPart", EditConditionHides))
	TArray<TObjectPtr<UNRWeaponPartData>> PartPool;

	/** How often a dropped part is level I, II or III, as weights against each other. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Weapon Part", meta = (ClampMin = "0.0", EditCondition = "ItemType == ENRItemType::WeaponPart", EditConditionHides))
	float LevelIWeight = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Weapon Part", meta = (ClampMin = "0.0", EditCondition = "ItemType == ENRItemType::WeaponPart", EditConditionHides))
	float LevelIIWeight = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loot|Weapon Part", meta = (ClampMin = "0.0", EditCondition = "ItemType == ENRItemType::WeaponPart", EditConditionHides))
	float LevelIIIWeight = 5.0f;
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
		case ENRItemType::WeaponPart: return TEXT("PART");
		case ENRItemType::Potion:
		default: return TEXT("HEAL");
		}
	}

	/** How many of Entry one drop gives. A weapon part is always one. */
	inline int32 RollCount(const FNRLootEntry& Entry)
	{
		if (Entry.ItemType == ENRItemType::WeaponPart)
		{
			return 1;
		}
		return Entry.MaxCount > Entry.Count ? FMath::RandRange(Entry.Count, Entry.MaxCount) : Entry.Count;
	}

	/** A level for a dropped part by Entry's level weights; level I when they are all 0. */
	inline ENRWeaponPartLevel RollPartLevel(const FNRLootEntry& Entry)
	{
		const float Weights[] = { FMath::Max(Entry.LevelIWeight, 0.0f), FMath::Max(Entry.LevelIIWeight, 0.0f), FMath::Max(Entry.LevelIIIWeight, 0.0f) };
		float Roll = FMath::FRand() * (Weights[0] + Weights[1] + Weights[2]);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (Weights[Index] > 0.0f && Roll < Weights[Index])
			{
				return static_cast<ENRWeaponPartLevel>(Index);
			}
			Roll -= Weights[Index];
		}
		return Weights[2] > 0.0f ? ENRWeaponPartLevel::III : Weights[1] > 0.0f ? ENRWeaponPartLevel::II : ENRWeaponPartLevel::I;
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
