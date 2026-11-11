// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRAdaptationConfig.h"

UNRAdaptationConfig::UNRAdaptationConfig()
{
	// Behaviour per weapon, from the design: up-up = 1, up = 0.5, down = -0.5, down-down = -1.
	// Rifle: dodge++, strafe++, caution+, approach+. More close-in enemies.
	Rifle.Direction.Dodge = 1.0f;
	Rifle.Direction.Strafe = 1.0f;
	Rifle.Direction.Caution = 0.5f;
	Rifle.Direction.Approach = 0.5f;
	Rifle.RangedBias = -1.0f;
	Rifle.MoveSpeedWeight = 1.0f;
	Rifle.AttackSpeedWeight = 0.5f;

	// Shotgun: retreat++, caution+, strafe+, approach--. More shooters.
	Shotgun.Direction.Retreat = 1.0f;
	Shotgun.Direction.Caution = 0.5f;
	Shotgun.Direction.Strafe = 0.5f;
	Shotgun.Direction.Approach = -1.0f;
	Shotgun.RangedBias = 1.0f;
	Shotgun.MoveSpeedWeight = 0.5f;
	Shotgun.RegenWeight = 1.0f;

	// Melee: retreat+, strafe+, approach-. More shooters.
	Melee.Direction.Retreat = 0.5f;
	Melee.Direction.Strafe = 0.5f;
	Melee.Direction.Approach = -0.5f;
	Melee.RangedBias = 1.0f;
	Melee.RegenWeight = 0.5f;
	Melee.AttackSpeedWeight = 1.0f;

	// Crawlers lean into closing and striking; shooters into keeping away and slipping shots.
	MeleeFamily.Caution = 0.8f;
	MeleeFamily.Approach = 1.2f;
	MeleeFamily.Retreat = 0.8f;
	MeleeFamily.Aggression = 1.2f;

	RangedFamily.Dodge = 1.2f;
	RangedFamily.Strafe = 1.2f;
	RangedFamily.Approach = 0.8f;
	RangedFamily.Retreat = 1.2f;
	RangedFamily.Aggression = 0.8f;

	// The boss is set in its ways: half as adaptable on every axis.
	BossFamily.Dodge = 0.5f;
	BossFamily.Strafe = 0.5f;
	BossFamily.Caution = 0.5f;
	BossFamily.Approach = 0.5f;
	BossFamily.Retreat = 0.5f;
	BossFamily.Aggression = 0.5f;
}

const FNRWeaponAdaptationRule& UNRAdaptationConfig::GetWeaponRule(EAdaptationWeapon Weapon) const
{
	switch (Weapon)
	{
	case EAdaptationWeapon::Shotgun: return Shotgun;
	case EAdaptationWeapon::Melee:   return Melee;
	default:                         return Rifle;
	}
}

const FNRAdaptationAxisScale& UNRAdaptationConfig::GetFamilyScale(ENREnemyFamily Family) const
{
	switch (Family)
	{
	case ENREnemyFamily::Ranged: return RangedFamily;
	case ENREnemyFamily::Boss:   return BossFamily;
	default:                     return MeleeFamily;
	}
}

bool FNRRoomCombatRecord::HasData() const
{
	for (int32 Index = 0; Index < NRAdaptWeaponCount; ++Index)
	{
		if (Damage[Index] > 0.0f || Kills[Index] > 0 || Magazines[Index] > 0.0f)
		{
			return true;
		}
	}
	return false;
}

FString FNRRoomCombatRecord::ToString() const
{
	return FString::Printf(TEXT("rifle %.0f dmg/%d kills/%.1f mag, shotgun %.0f/%d/%.1f, melee %.0f/%d"),
		Damage[0], Kills[0], Magazines[0], Damage[1], Kills[1], Magazines[1], Damage[2], Kills[2]);
}

void FNRAdaptationModel::Reset()
{
	*this = FNRAdaptationModel();
}

void FNRAdaptationModel::ComputeUsageShare(const FNRRoomCombatRecord& Record, float OutShare[NRAdaptWeaponCount])
{
	// Damage dealt is what the enemies felt. Magazines only decide when nothing was hit.
	float Total = 0.0f;
	for (int32 Index = 0; Index < NRAdaptWeaponCount; ++Index)
	{
		Total += Record.Damage[Index];
	}
	const bool bUseDamage = Total > 0.0f;
	if (!bUseDamage)
	{
		for (int32 Index = 0; Index < NRAdaptWeaponCount; ++Index)
		{
			Total += Record.Magazines[Index];
		}
	}
	for (int32 Index = 0; Index < NRAdaptWeaponCount; ++Index)
	{
		const float Value = bUseDamage ? Record.Damage[Index] : Record.Magazines[Index];
		OutShare[Index] = Total > 0.0f ? Value / Total : 0.0f;
	}
}

void FNRAdaptationModel::CommitRoom(const FNRRoomCombatRecord& Record, const UNRAdaptationConfig& Config, bool bCountsAsStage)
{
	Stage += bCountsAsStage ? 1 : 0;
	if (!Record.HasData())
	{
		return;
	}
	++RoomsLearned;

	float Share[NRAdaptWeaponCount];
	ComputeUsageShare(Record, Share);

	// Behaviour: what was used moves each kind of enemy along that weapon's direction, scaled by
	// how readily that kind takes up each axis. Old adaptation fades by Retention.
	for (int32 Family = 0; Family < NREnemyFamilyCount; ++Family)
	{
		const FNRAdaptationAxisScale& Scale = Config.GetFamilyScale(static_cast<ENREnemyFamily>(Family));
		FEnemyAdaptationProfile& Profile = Profiles[Family];
		for (int32 Axis = 0; Axis < static_cast<int32>(EAdaptationAxis::Num); ++Axis)
		{
			const EAdaptationAxis AxisId = static_cast<EAdaptationAxis>(Axis);
			float Push = 0.0f;
			for (int32 Weapon = 0; Weapon < NRAdaptWeaponCount; ++Weapon)
			{
				Push += Share[Weapon] * Config.GetWeaponRule(NRAdaptWeaponFromIndex(Weapon)).Direction.Get(AxisId);
			}
			Profile.Set(AxisId, Profile.Get(AxisId) * Config.Retention + Config.LearnRate * Push * Scale.Get(AxisId));
		}
	}

	// Weapon adaptation: from the share of the kills, counted in full from KillsForFullWeaponGain.
	int32 TotalKills = 0;
	for (int32 Weapon = 0; Weapon < NRAdaptWeaponCount; ++Weapon)
	{
		TotalKills += Record.Kills[Weapon];
	}
	const float Amount = FMath::Min(static_cast<float>(TotalKills) / FMath::Max(Config.KillsForFullWeaponGain, 1), 1.0f);
	for (int32 Weapon = 0; Weapon < NRAdaptWeaponCount; ++Weapon)
	{
		const float KillShare = TotalKills > 0 ? static_cast<float>(Record.Kills[Weapon]) / TotalKills : 0.0f;
		WeaponLevels[Weapon] = FMath::Clamp(WeaponLevels[Weapon] * Config.Retention + Config.LearnRate * KillShare * Amount, 0.0f, 1.0f);
	}
}

float FNRAdaptationModel::GetWeaponLevel(EAdaptationWeapon Weapon) const
{
	const int32 Index = NRAdaptWeaponIndex(Weapon);
	return Index >= 0 && Index < NRAdaptWeaponCount ? WeaponLevels[Index] : 0.0f;
}

float FNRAdaptationModel::GetRangedBias(const UNRAdaptationConfig& Config) const
{
	float Bias = 0.0f;
	for (int32 Weapon = 0; Weapon < NRAdaptWeaponCount; ++Weapon)
	{
		Bias += WeaponLevels[Weapon] * Config.GetWeaponRule(NRAdaptWeaponFromIndex(Weapon)).RangedBias;
	}
	return FMath::Clamp(Bias, -1.0f, 1.0f);
}

FEnemyCombatModifiers FNRAdaptationModel::BuildModifiers(ENREnemyFamily Family, int32 PlayerLevel, const UNRAdaptationConfig& Config) const
{
	FEnemyCombatModifiers Modifiers;

	// Difficulty, apart from adaptation: player level and stage only.
	const int32 Levels = FMath::Max(PlayerLevel - 1, 0);
	Modifiers.HealthMultiplier = 1.0f + Config.HealthPerLevel * Levels + Config.HealthPerStage * Stage;
	Modifiers.DamageMultiplier = 1.0f + Config.DamagePerLevel * Levels + Config.DamagePerStage * Stage;

	// Weapon adaptation.
	float Move = 0.0f;
	float Regen = 0.0f;
	float AttackSpeed = 0.0f;
	for (int32 Weapon = 0; Weapon < NRAdaptWeaponCount; ++Weapon)
	{
		const FNRWeaponAdaptationRule& Rule = Config.GetWeaponRule(NRAdaptWeaponFromIndex(Weapon));
		Move += WeaponLevels[Weapon] * Rule.MoveSpeedWeight;
		Regen += WeaponLevels[Weapon] * Rule.RegenWeight;
		AttackSpeed += WeaponLevels[Weapon] * Rule.AttackSpeedWeight;
	}
	Modifiers.MoveSpeedMultiplier = 1.0f + Config.MaxMoveSpeedBonus * FMath::Clamp(Move, 0.0f, 1.0f);
	Modifiers.AttackSpeedMultiplier = 1.0f + Config.MaxAttackSpeedBonus * FMath::Clamp(AttackSpeed, 0.0f, 1.0f);
	Modifiers.RegenPerSecond = Config.MaxRegenPerSecond * FMath::Clamp(Regen, 0.0f, 1.0f);
	Modifiers.RifleResistance = Config.MaxResistance * WeaponLevels[0];
	Modifiers.ShotgunResistance = Config.MaxResistance * WeaponLevels[1];
	Modifiers.MeleeResistance = Config.MaxResistance * WeaponLevels[2];
	return Modifiers;
}
