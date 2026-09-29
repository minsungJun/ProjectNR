// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NRWeaponPartTypes.generated.h"

class UNRWeaponPartData;

/**
 * The three weapons the player always carries. Also the bit index in
 * UNRWeaponPartData::CompatibleWeapons, so the order is part of saved data.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "false"))
enum class ENRWeaponId : uint8
{
	Rifle,
	Shotgun,
	Melee
};

constexpr int32 NRWeaponIdCount = 3;

/** What a part changes. One stat per part; a part that changes two things is two parts. */
UENUM(BlueprintType)
enum class ENRWeaponPartStat : uint8
{
	/** Fraction added to the final damage multiplier. 0.1 = +10%. */
	Damage,
	/** Rounds added to the magazine. Ranged weapons only. */
	MagazineSize,
	/** Fraction added to the attack rate. 0.15 = 15% faster fire / swings. */
	AttackSpeed,
	/** Added to the critical chance, 0..1. */
	CriticalChance,
	/** Added to the critical multiplier. 0.3 turns x1.5 into x1.8. */
	CriticalDamage
};

UENUM(BlueprintType)
enum class ENRWeaponPartSlotType : uint8
{
	/** The one weapon-specific slot. */
	Exclusive,
	/** The five shared slots. */
	Common
};

UENUM(BlueprintType)
enum class ENRWeaponPartLevel : uint8
{
	I,
	II,
	III
};

/** One part the player owns. The same data asset can be owned many times, each its own instance. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRWeaponPartInstance
{
	GENERATED_BODY()

	/** Stable identity: slots refer to parts by this, never by index. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon Part")
	FGuid InstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Part")
	TObjectPtr<UNRWeaponPartData> PartData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Part")
	ENRWeaponPartLevel Level = ENRWeaponPartLevel::I;

	bool IsValid() const { return InstanceId.IsValid() && PartData != nullptr; }
};

/**
 * The six slots of one weapon: index 0 is the exclusive slot, 1..5 the common ones.
 * An invalid guid is an empty slot.
 */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRWeaponPartLoadout
{
	GENERATED_BODY()

	static constexpr int32 ExclusiveSlotIndex = 0;
	static constexpr int32 NumCommonSlots = 5;
	static constexpr int32 NumSlots = 1 + NumCommonSlots;

	FNRWeaponPartLoadout() { Slots.SetNum(NumSlots); }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon Part")
	TArray<FGuid> Slots;

	static ENRWeaponPartSlotType GetSlotType(int32 SlotIndex)
	{
		return SlotIndex == ExclusiveSlotIndex ? ENRWeaponPartSlotType::Exclusive : ENRWeaponPartSlotType::Common;
	}
};

/** A starting part to hand out, for test inventories. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRStartingWeaponPart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Part")
	TSoftObjectPtr<UNRWeaponPartData> PartData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Part")
	ENRWeaponPartLevel Level = ENRWeaponPartLevel::I;
};

/**
 * What a weapon actually does after its parts, recomputed only when the loadout changes.
 *
 *   DamageMultiplier      = 1 + sum(Damage)
 *   MaxMagazine           = max(1, BaseMagazine + sum(MagazineSize))   (0 for melee)
 *   AttackSpeedMultiplier = max(0.1, 1 + sum(AttackSpeed))
 *   CriticalChance        = clamp(BaseCritChance + sum(CriticalChance), 0, 1)
 *   CriticalMultiplier    = max(1, BaseCritMultiplier + sum(CriticalDamage))
 *
 * Damage stays a multiplier because the base damage belongs to the projectile or the swing,
 * not to the weapon.
 */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRWeaponRuntimeStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon Stats")
	float DamageMultiplier = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon Stats")
	int32 MaxMagazine = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon Stats")
	float AttackSpeedMultiplier = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon Stats")
	float CriticalChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon Stats")
	float CriticalMultiplier = 1.5f;

	/** Base interval shortened by the attack speed. */
	float GetAttackInterval(float BaseInterval) const
	{
		return BaseInterval / FMath::Max(AttackSpeedMultiplier, 0.1f);
	}

	/** Rolls a critical and returns BaseDamage with both multipliers applied. */
	float RollDamage(float BaseDamage, bool& bOutCritical) const
	{
		bOutCritical = CriticalChance > 0.0f && FMath::FRand() < CriticalChance;
		return BaseDamage * DamageMultiplier * (bOutCritical ? CriticalMultiplier : 1.0f);
	}
};

/** Result of an equip request, so UI can say why it was refused. */
UENUM(BlueprintType)
enum class ENRWeaponPartEquipResult : uint8
{
	Success,
	EditingLocked,
	UnknownPart,
	InvalidSlot,
	WrongSlotType,
	IncompatibleWeapon
};
