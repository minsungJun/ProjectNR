// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRWeaponPartComponent.h"

#include "NRWeaponComponent.h"
#include "NRWeaponPartData.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRWeaponPart, Log, All);

UNRWeaponPartComponent::UNRWeaponPartComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Stats must exist before any BeginPlay reads them - the weapon fills its magazines there.
	bWantsInitializeComponent = true;

	// Test inventory: each of the five common parts at every level.
	static const TCHAR* TestParts[] = { TEXT("Damage"), TEXT("Magazine"), TEXT("AttackSpeed"), TEXT("CritChance"), TEXT("CritDamage") };
	for (const TCHAR* Name : TestParts)
	{
		const FSoftObjectPath Path(FString::Printf(TEXT("/Game/DataAssets/WeaponParts/DA_WeaponPart_%s.DA_WeaponPart_%s"), Name, Name));
		for (ENRWeaponPartLevel Level : { ENRWeaponPartLevel::I, ENRWeaponPartLevel::II, ENRWeaponPartLevel::III })
		{
			FNRStartingWeaponPart& Entry = StartingParts.AddDefaulted_GetRef();
			Entry.PartData = TSoftObjectPtr<UNRWeaponPartData>(Path);
			Entry.Level = Level;
		}
	}
}

void UNRWeaponPartComponent::InitializeComponent()
{
	Super::InitializeComponent();

	Loadouts.SetNum(NRWeaponIdCount);
	RuntimeStats.SetNum(NRWeaponIdCount);
	Inventory.Reset();

	for (const FNRStartingWeaponPart& Entry : StartingParts)
	{
		if (UNRWeaponPartData* Data = Entry.PartData.LoadSynchronous())
		{
			FNRWeaponPartInstance& Instance = Inventory.AddDefaulted_GetRef();
			Instance.InstanceId = FGuid::NewGuid();
			Instance.PartData = Data;
			Instance.Level = Entry.Level;
		}
		else
		{
			UE_LOG(LogNRWeaponPart, Warning, TEXT("%s: starting part %s not found"),
				*GetNameSafe(GetOwner()), *Entry.PartData.ToString());
		}
	}

	RecalculateAllStats();
	UE_LOG(LogNRWeaponPart, Log, TEXT("%s starts with %d part(s)"), *GetNameSafe(GetOwner()), Inventory.Num());
}

// --- Queries -----------------------------------------------------------------------

const FNRWeaponPartInstance* UNRWeaponPartComponent::FindPart(const FGuid& InstanceId) const
{
	return Inventory.FindByPredicate([&InstanceId](const FNRWeaponPartInstance& Part) { return Part.InstanceId == InstanceId; });
}

FNRWeaponPartInstance* UNRWeaponPartComponent::FindPartMutable(const FGuid& InstanceId)
{
	return Inventory.FindByPredicate([&InstanceId](const FNRWeaponPartInstance& Part) { return Part.InstanceId == InstanceId; });
}

FNRWeaponPartLoadout& UNRWeaponPartComponent::GetLoadoutMutable(ENRWeaponId WeaponId)
{
	if (Loadouts.Num() < NRWeaponIdCount)
	{
		Loadouts.SetNum(NRWeaponIdCount);
	}
	return Loadouts[static_cast<int32>(WeaponId)];
}

bool UNRWeaponPartComponent::FindEquippedSlot(FGuid InstanceId, ENRWeaponId& OutWeaponId, int32& OutSlotIndex) const
{
	if (!InstanceId.IsValid())
	{
		return false;
	}
	for (int32 WeaponIndex = 0; WeaponIndex < Loadouts.Num(); ++WeaponIndex)
	{
		const int32 SlotIndex = Loadouts[WeaponIndex].Slots.IndexOfByKey(InstanceId);
		if (SlotIndex != INDEX_NONE)
		{
			OutWeaponId = static_cast<ENRWeaponId>(WeaponIndex);
			OutSlotIndex = SlotIndex;
			return true;
		}
	}
	return false;
}

bool UNRWeaponPartComponent::IsEquipped(FGuid InstanceId) const
{
	ENRWeaponId WeaponId;
	int32 SlotIndex;
	return FindEquippedSlot(InstanceId, WeaponId, SlotIndex);
}

FGuid UNRWeaponPartComponent::GetEquippedPart(ENRWeaponId WeaponId, int32 SlotIndex) const
{
	const int32 WeaponIndex = static_cast<int32>(WeaponId);
	return Loadouts.IsValidIndex(WeaponIndex) && Loadouts[WeaponIndex].Slots.IsValidIndex(SlotIndex)
		? Loadouts[WeaponIndex].Slots[SlotIndex]
		: FGuid();
}

FNRWeaponRuntimeStats UNRWeaponPartComponent::GetRuntimeStats(ENRWeaponId WeaponId) const
{
	const int32 WeaponIndex = static_cast<int32>(WeaponId);
	return RuntimeStats.IsValidIndex(WeaponIndex) ? RuntimeStats[WeaponIndex] : FNRWeaponRuntimeStats();
}

// --- Rules -------------------------------------------------------------------------

ENRWeaponPartEquipResult UNRWeaponPartComponent::CanEquipPart(FGuid InstanceId, ENRWeaponId WeaponId, int32 SlotIndex) const
{
	if (!CanEditLoadout())
	{
		return ENRWeaponPartEquipResult::EditingLocked;
	}

	const FNRWeaponPartInstance* Part = FindPart(InstanceId);
	if (!Part || !Part->IsValid())
	{
		return ENRWeaponPartEquipResult::UnknownPart;
	}

	if (SlotIndex < 0 || SlotIndex >= FNRWeaponPartLoadout::NumSlots)
	{
		return ENRWeaponPartEquipResult::InvalidSlot;
	}

	if (FNRWeaponPartLoadout::GetSlotType(SlotIndex) != Part->PartData->SlotType)
	{
		return ENRWeaponPartEquipResult::WrongSlotType;
	}

	if (!Part->PartData->IsCompatibleWith(WeaponId))
	{
		return ENRWeaponPartEquipResult::IncompatibleWeapon;
	}

	// A weapon the pawn does not carry has no loadout worth filling.
	if (const UNRWeaponComponent* Weapons = GetOwner() ? GetOwner()->FindComponentByClass<UNRWeaponComponent>() : nullptr)
	{
		if (!Weapons->FindWeaponById(WeaponId))
		{
			return ENRWeaponPartEquipResult::IncompatibleWeapon;
		}
	}

	return ENRWeaponPartEquipResult::Success;
}

FText UNRWeaponPartComponent::DescribeEquipResult(ENRWeaponPartEquipResult Result)
{
	switch (Result)
	{
	case ENRWeaponPartEquipResult::Success:            return NSLOCTEXT("NRWeaponPart", "Equipped", "Equipped");
	case ENRWeaponPartEquipResult::EditingLocked:      return NSLOCTEXT("NRWeaponPart", "Locked", "Parts can only be changed in a safe area");
	case ENRWeaponPartEquipResult::UnknownPart:        return NSLOCTEXT("NRWeaponPart", "Unknown", "That part is no longer owned");
	case ENRWeaponPartEquipResult::InvalidSlot:        return NSLOCTEXT("NRWeaponPart", "InvalidSlot", "No such slot");
	case ENRWeaponPartEquipResult::WrongSlotType:      return NSLOCTEXT("NRWeaponPart", "WrongSlot", "That part does not fit this slot type");
	case ENRWeaponPartEquipResult::IncompatibleWeapon: return NSLOCTEXT("NRWeaponPart", "Incompatible", "That part does not fit this weapon");
	default:                                           return FText::GetEmpty();
	}
}

// --- Changes -----------------------------------------------------------------------

FGuid UNRWeaponPartComponent::AddPart(UNRWeaponPartData* PartData, ENRWeaponPartLevel Level)
{
	if (!PartData)
	{
		return FGuid();
	}

	FNRWeaponPartInstance& Instance = Inventory.AddDefaulted_GetRef();
	Instance.InstanceId = FGuid::NewGuid();
	Instance.PartData = PartData;
	Instance.Level = Level;
	OnInventoryChanged.Broadcast();
	return Instance.InstanceId;
}

ENRWeaponPartEquipResult UNRWeaponPartComponent::EquipPart(FGuid InstanceId, ENRWeaponId WeaponId, int32 SlotIndex)
{
	const ENRWeaponPartEquipResult Result = CanEquipPart(InstanceId, WeaponId, SlotIndex);
	if (Result != ENRWeaponPartEquipResult::Success)
	{
		UE_LOG(LogNRWeaponPart, Log, TEXT("%s: equip refused (%s)"), *GetNameSafe(GetOwner()),
			*DescribeEquipResult(Result).ToString());
		return Result;
	}

	ENRWeaponId PreviousWeapon = WeaponId;
	int32 PreviousSlot = INDEX_NONE;
	const bool bWasEquipped = FindEquippedSlot(InstanceId, PreviousWeapon, PreviousSlot);
	if (bWasEquipped && PreviousWeapon == WeaponId && PreviousSlot == SlotIndex)
	{
		return ENRWeaponPartEquipResult::Success;
	}

	// Leaving the old slot first is what makes "one part, one slot" hold by construction.
	if (bWasEquipped)
	{
		GetLoadoutMutable(PreviousWeapon).Slots[PreviousSlot].Invalidate();
	}

	// Whatever sat in the target slot returns to the inventory rather than being lost.
	GetLoadoutMutable(WeaponId).Slots[SlotIndex] = InstanceId;

	const FNRWeaponPartInstance* Part = FindPart(InstanceId);
	UE_LOG(LogNRWeaponPart, Log, TEXT("%s: %s %s -> %s slot %d%s"), *GetNameSafe(GetOwner()),
		*Part->PartData->DisplayName.ToString(), *UNRWeaponPartData::GetLevelText(Part->Level).ToString(),
		*UEnum::GetValueAsString(WeaponId), SlotIndex,
		bWasEquipped ? *FString::Printf(TEXT(" (moved from %s slot %d)"), *UEnum::GetValueAsString(PreviousWeapon), PreviousSlot) : TEXT(""));

	RecalculateStats(WeaponId);
	OnLoadoutChanged.Broadcast(WeaponId);
	if (bWasEquipped && PreviousWeapon != WeaponId)
	{
		RecalculateStats(PreviousWeapon);
		OnLoadoutChanged.Broadcast(PreviousWeapon);
	}
	return ENRWeaponPartEquipResult::Success;
}

bool UNRWeaponPartComponent::UnequipSlot(ENRWeaponId WeaponId, int32 SlotIndex)
{
	if (!CanEditLoadout() || !GetEquippedPart(WeaponId, SlotIndex).IsValid())
	{
		return false;
	}

	GetLoadoutMutable(WeaponId).Slots[SlotIndex].Invalidate();
	UE_LOG(LogNRWeaponPart, Log, TEXT("%s: %s slot %d emptied"), *GetNameSafe(GetOwner()),
		*UEnum::GetValueAsString(WeaponId), SlotIndex);

	RecalculateStats(WeaponId);
	OnLoadoutChanged.Broadcast(WeaponId);
	return true;
}

bool UNRWeaponPartComponent::UnequipPart(FGuid InstanceId)
{
	ENRWeaponId WeaponId;
	int32 SlotIndex;
	return FindEquippedSlot(InstanceId, WeaponId, SlotIndex) && UnequipSlot(WeaponId, SlotIndex);
}

bool UNRWeaponPartComponent::SetPartLevel(FGuid InstanceId, ENRWeaponPartLevel NewLevel)
{
	FNRWeaponPartInstance* Part = FindPartMutable(InstanceId);
	if (!Part || Part->Level == NewLevel)
	{
		return false;
	}

	Part->Level = NewLevel;
	OnInventoryChanged.Broadcast();

	ENRWeaponId WeaponId;
	int32 SlotIndex;
	if (FindEquippedSlot(InstanceId, WeaponId, SlotIndex))
	{
		RecalculateStats(WeaponId);
		OnLoadoutChanged.Broadcast(WeaponId);
	}
	return true;
}

// --- Stats -------------------------------------------------------------------------

FNRWeaponRuntimeStats UNRWeaponPartComponent::ComputeRuntimeStats(const FNRWeaponDefinition& Weapon, const TArray<const FNRWeaponPartInstance*>& Parts)
{
	float DamageBonus = 0.0f;
	float MagazineBonus = 0.0f;
	float SpeedBonus = 0.0f;
	float CritChanceBonus = 0.0f;
	float CritDamageBonus = 0.0f;

	for (const FNRWeaponPartInstance* Part : Parts)
	{
		if (!Part || !Part->PartData)
		{
			continue;
		}

		const float Value = Part->PartData->GetValue(Part->Level);
		switch (Part->PartData->Stat)
		{
		case ENRWeaponPartStat::Damage:         DamageBonus += Value; break;
		case ENRWeaponPartStat::MagazineSize:   MagazineBonus += Value; break;
		case ENRWeaponPartStat::AttackSpeed:    SpeedBonus += Value; break;
		case ENRWeaponPartStat::CriticalChance: CritChanceBonus += Value; break;
		case ENRWeaponPartStat::CriticalDamage: CritDamageBonus += Value; break;
		}
	}

	FNRWeaponRuntimeStats Stats;
	Stats.DamageMultiplier = FMath::Max(0.0f, 1.0f + DamageBonus);
	Stats.MaxMagazine = Weapon.Kind == ENRWeaponKind::Ranged
		? FMath::Max(1, Weapon.BaseMagazine + FMath::RoundToInt(MagazineBonus))
		: 0;
	Stats.AttackSpeedMultiplier = FMath::Max(0.1f, 1.0f + SpeedBonus);
	Stats.CriticalChance = FMath::Clamp(Weapon.BaseCritChance + CritChanceBonus, 0.0f, 1.0f);
	Stats.CriticalMultiplier = FMath::Max(1.0f, Weapon.BaseCritMultiplier + CritDamageBonus);
	return Stats;
}

void UNRWeaponPartComponent::RecalculateStats(ENRWeaponId WeaponId)
{
	const int32 WeaponIndex = static_cast<int32>(WeaponId);
	if (RuntimeStats.Num() < NRWeaponIdCount)
	{
		RuntimeStats.SetNum(NRWeaponIdCount);
	}

	TArray<const FNRWeaponPartInstance*> Fitted;
	for (const FGuid& Id : GetLoadoutMutable(WeaponId).Slots)
	{
		if (const FNRWeaponPartInstance* Part = Id.IsValid() ? FindPart(Id) : nullptr)
		{
			Fitted.Add(Part);
		}
	}

	const UNRWeaponComponent* Weapons = GetOwner() ? GetOwner()->FindComponentByClass<UNRWeaponComponent>() : nullptr;
	const FNRWeaponDefinition* Definition = Weapons ? Weapons->FindWeaponById(WeaponId) : nullptr;
	FNRWeaponDefinition Fallback;
	Fallback.WeaponId = WeaponId;
	Fallback.Kind = WeaponId == ENRWeaponId::Melee ? ENRWeaponKind::Melee : ENRWeaponKind::Ranged;

	const FNRWeaponRuntimeStats Stats = ComputeRuntimeStats(Definition ? *Definition : Fallback, Fitted);
	RuntimeStats[WeaponIndex] = Stats;

	UE_LOG(LogNRWeaponPart, Log, TEXT("%s %s stats: dmg x%.2f, mag %d, speed x%.2f, crit %.0f%% x%.2f (%d part(s))"),
		*GetNameSafe(GetOwner()), *UEnum::GetValueAsString(WeaponId), Stats.DamageMultiplier, Stats.MaxMagazine,
		Stats.AttackSpeedMultiplier, Stats.CriticalChance * 100.0f, Stats.CriticalMultiplier, Fitted.Num());

	OnRuntimeStatsChanged.Broadcast(WeaponId, Stats);
}

void UNRWeaponPartComponent::RecalculateAllStats()
{
	for (int32 WeaponIndex = 0; WeaponIndex < NRWeaponIdCount; ++WeaponIndex)
	{
		RecalculateStats(static_cast<ENRWeaponId>(WeaponIndex));
	}
}
