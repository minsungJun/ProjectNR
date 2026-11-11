// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NRItemTypes.h"
#include "NRLootTable.generated.h"

/**
 * One roll of a loot table: with Chance it happens at all, and then Picks entries are drawn from
 * Entries by weight (the same entry can come up more than once).
 */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRLootRoll
{
	GENERATED_BODY()

	/** Shown in the editor list only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	FName Label;

	/** Chance that this roll gives anything. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Chance = 1.0f;

	/** How many entries are drawn when it does. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 Picks = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (TitleProperty = "ItemType"))
	TArray<FNRLootEntry> Entries;
};

/** One item that came up: what, and how many. A weapon part also has which part and its level. */
struct FNRLootDrop
{
	ENRItemType ItemType = ENRItemType::Potion;
	int32 Count = 1;
	UNRWeaponPartData* Part = nullptr;
	ENRWeaponPartLevel Level = ENRWeaponPartLevel::I;
};

/**
 * What an enemy or a chest drops: every roll in Rolls is made on its own. An enemy's table comes
 * from Project Settings > NR Loot (a default, or one per enemy class); a chest's is set on it.
 */
UCLASS(BlueprintType)
class PROJECTNR_API UNRLootTable : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (TitleProperty = "Label"))
	TArray<FNRLootRoll> Rolls;

	/** Makes every roll and adds what comes up to Out. */
	void Roll(TArray<FNRLootDrop>& Out) const;

	/** A part from Pool at random; from every weapon part asset when Pool is empty. Null if there is none. */
	static UNRWeaponPartData* PickPart(const TArray<TObjectPtr<UNRWeaponPartData>>& Pool);
};
