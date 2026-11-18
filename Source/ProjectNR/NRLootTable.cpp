// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRLootTable.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "NRWeaponPartData.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRLootTable, Log, All);

void UNRLootTable::Roll(TArray<FNRLootDrop>& Out, float ChanceScale) const
{
	for (const FNRLootRoll& Each : Rolls)
	{
		if (FMath::FRand() >= FMath::Min(Each.Chance * FMath::Max(ChanceScale, 0.0f), 1.0f))
		{
			continue;
		}
		for (int32 Pick = 0; Pick < Each.Picks; ++Pick)
		{
			const FNRLootEntry* Entry = NRItems::PickEntry(Each.Entries);
			if (!Entry)
			{
				continue;
			}
			FNRLootDrop Drop;
			Drop.ItemType = Entry->ItemType;
			Drop.Count = NRItems::RollCount(*Entry);
			if (Entry->ItemType == ENRItemType::WeaponPart)
			{
				Drop.Part = PickPart(Entry->PartPool);
				if (!Drop.Part)
				{
					UE_LOG(LogNRLootTable, Warning, TEXT("%s: a weapon part came up but there is no part to give"), *GetName());
					continue;
				}
				Drop.Level = NRItems::RollPartLevel(*Entry);
			}
			Out.Add(Drop);
		}
	}
}

UNRWeaponPartData* UNRLootTable::PickPart(const TArray<TObjectPtr<UNRWeaponPartData>>& Pool)
{
	TArray<UNRWeaponPartData*> Candidates;
	for (UNRWeaponPartData* Part : Pool)
	{
		if (Part)
		{
			Candidates.Add(Part);
		}
	}
	if (Pool.Num() == 0)
	{
		// Every part asset in the project, so a new part drops without touching the tables.
		TArray<FAssetData> Assets;
		IAssetRegistry::GetChecked().GetAssetsByClass(UNRWeaponPartData::StaticClass()->GetClassPathName(), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (UNRWeaponPartData* Part = Cast<UNRWeaponPartData>(Asset.GetAsset()))
			{
				Candidates.Add(Part);
			}
		}
	}
	return Candidates.Num() > 0 ? Candidates[FMath::RandRange(0, Candidates.Num() - 1)] : nullptr;
}
