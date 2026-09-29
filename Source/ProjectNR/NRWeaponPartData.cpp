// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRWeaponPartData.h"

float UNRWeaponPartData::GetValue(ENRWeaponPartLevel Level) const
{
	switch (Level)
	{
	case ENRWeaponPartLevel::II:  return ValueLevelII;
	case ENRWeaponPartLevel::III: return ValueLevelIII;
	default:                      return ValueLevelI;
	}
}

bool UNRWeaponPartData::IsCompatibleWith(ENRWeaponId Weapon) const
{
	if ((CompatibleWeapons & (1 << static_cast<int32>(Weapon))) == 0)
	{
		return false;
	}

	// A magazine on an axe means nothing. Enforced here as well as in the asset's mask, so
	// a mis-ticked mask cannot let one through.
	return !(Stat == ENRWeaponPartStat::MagazineSize && Weapon == ENRWeaponId::Melee);
}

FText UNRWeaponPartData::GetLevelText(ENRWeaponPartLevel Level)
{
	switch (Level)
	{
	case ENRWeaponPartLevel::II:  return NSLOCTEXT("NRWeaponPart", "LevelII", "II");
	case ENRWeaponPartLevel::III: return NSLOCTEXT("NRWeaponPart", "LevelIII", "III");
	default:                      return NSLOCTEXT("NRWeaponPart", "LevelI", "I");
	}
}

FText UNRWeaponPartData::DescribeEffect(ENRWeaponPartLevel Level) const
{
	const float Value = GetValue(Level);
	const FString Percent = FString::Printf(TEXT("%+.0f%%"), Value * 100.0f);

	switch (Stat)
	{
	case ENRWeaponPartStat::Damage:
		return FText::FromString(FString::Printf(TEXT("Damage %s"), *Percent));
	case ENRWeaponPartStat::MagazineSize:
		return FText::FromString(FString::Printf(TEXT("Magazine %+d"), FMath::RoundToInt(Value)));
	case ENRWeaponPartStat::AttackSpeed:
		return FText::FromString(FString::Printf(TEXT("Attack speed %s"), *Percent));
	case ENRWeaponPartStat::CriticalChance:
		return FText::FromString(FString::Printf(TEXT("Critical chance %s"), *Percent));
	case ENRWeaponPartStat::CriticalDamage:
		return FText::FromString(FString::Printf(TEXT("Critical damage %+.2fx"), Value));
	default:
		return FText::GetEmpty();
	}
}

FPrimaryAssetId UNRWeaponPartData::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("NRWeaponPart"), GetFName());
}
