// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/NeuralTypes.h"
#include "Engine/DataAsset.h"
#include "NRAdaptationConfig.generated.h"

/** The kinds of enemy that adapt each in their own way. */
UENUM(BlueprintType)
enum class ENREnemyFamily : uint8
{
	/** Close-in crawlers (AEnemyCharacter). */
	Melee,
	/** Shooters (ARangedEnemyCharacter). */
	Ranged,
	/** The boss (ABossEnemyCharacter). */
	Boss,

	Num UMETA(Hidden)
};

constexpr int32 NREnemyFamilyCount = static_cast<int32>(ENREnemyFamily::Num);

/** The three player weapons, as indices 0..2 (EAdaptationWeapon minus None). */
constexpr int32 NRAdaptWeaponCount = 3;

inline EAdaptationWeapon NRAdaptWeaponFromIndex(int32 Index) { return static_cast<EAdaptationWeapon>(Index + 1); }
inline int32 NRAdaptWeaponIndex(EAdaptationWeapon Weapon) { return static_cast<int32>(Weapon) - 1; }

/** One multiplier per adaptation axis: how strongly a kind of enemy takes up each axis. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRAdaptationAxisScale
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Axes", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float Dodge = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Axes", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float Strafe = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Axes", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float Caution = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Axes", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float Approach = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Axes", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float Retreat = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Axes", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float Aggression = 1.0f;

	float Get(EAdaptationAxis Axis) const
	{
		switch (Axis)
		{
		case EAdaptationAxis::Dodge:      return Dodge;
		case EAdaptationAxis::Strafe:     return Strafe;
		case EAdaptationAxis::Caution:    return Caution;
		case EAdaptationAxis::Approach:   return Approach;
		case EAdaptationAxis::Retreat:    return Retreat;
		case EAdaptationAxis::Aggression: return Aggression;
		default:                          return 0.0f;
		}
	}
};

/** What fighting with one weapon teaches the enemies. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRWeaponAdaptationRule
{
	GENERATED_BODY()

	/** Direction the six axes move per room fought entirely with this weapon (before the learn rate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	FEnemyAdaptationProfile Direction;

	/**
	 * Which way spawns shift as enemies learn this weapon: -1 more close-in enemies, +1 more
	 * shooters.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawns", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float RangedBias = 0.0f;

	/** Share of the speed, regeneration and attack rate maxima this weapon's adaptation unlocks (0..1 each). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MoveSpeedWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RegenWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AttackSpeedWeight = 0.0f;
};

/**
 * Numbers for enemy adaptation: how the player's weapon use in one room changes the next
 * room's enemies. Referenced from UNREncounterData (Adaptation); empty uses these defaults.
 *
 * Two separate tracks, by design (AGENTS.md: behaviour adaptation stays apart from stats):
 *  - Behaviour: per kind of enemy, six axes in -1..1, moved by each weapon's share of the
 *    damage dealt in the room, kept with Retention and learned at LearnRate.
 *  - Weapon adaptation: per weapon a level 0..1 from its share of the kills, which gives that
 *    weapon's resistance (up to MaxResistance), the spawn shift and the stat bonuses.
 * Health and damage grow with player level and stage (combat rooms cleared) on their own.
 *
 * @see FNRAdaptationModel, UNRAdaptationSubsystem
 */
UCLASS(BlueprintType)
class PROJECTNR_API UNRAdaptationConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UNRAdaptationConfig();

	const FNRWeaponAdaptationRule& GetWeaponRule(EAdaptationWeapon Weapon) const;
	const FNRAdaptationAxisScale& GetFamilyScale(ENREnemyFamily Family) const;

	// --- Learning

	/** Share of the previous adaptation kept each room: 0.7 lets a weapon switch show in 2-3 rooms. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Learning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Retention = 0.7f;

	/** How far one room moves the adaptation, before Retention. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Learning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LearnRate = 0.5f;

	/** Kills in a room for its weapon adaptation to count in full; fewer count in proportion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Learning", meta = (ClampMin = "1"))
	int32 KillsForFullWeaponGain = 4;

	// --- Weapons

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	FNRWeaponAdaptationRule Rifle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	FNRWeaponAdaptationRule Shotgun;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	FNRWeaponAdaptationRule Melee;

	// --- Kinds of enemy

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Families")
	FNRAdaptationAxisScale MeleeFamily;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Families")
	FNRAdaptationAxisScale RangedFamily;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Families")
	FNRAdaptationAxisScale BossFamily;

	// --- Stat limits (at weapon level 1)

	/** Damage from a fully learned weapon is cut by this much. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float MaxResistance = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxMoveSpeedBonus = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxAttackSpeedBonus = 0.2f;

	/** Share of max health regenerated per second, once 3 s have passed without a hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float MaxRegenPerSecond = 0.015f;

	// --- Spawns (at a full spawn shift of +-1)

	/** Ranged enemies a room may hold beyond (or short of) its rule's MaxRanged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawns", meta = (ClampMin = "0"))
	int32 MaxRangedShift = 2;

	/** Added to (or taken from) the rule's AnyRangedChance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawns", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AnyRangedChanceShift = 0.4f;

	/** Chance a point of the other kind changes over (a melee point to a shooter, or back). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawns", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ConvertChance = 0.4f;

	// --- Difficulty (apart from adaptation)

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0.0"))
	float HealthPerLevel = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0.0"))
	float HealthPerStage = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0.0"))
	float DamagePerLevel = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0.0"))
	float DamagePerStage = 0.03f;
};

/** What the player did in one fighting room, per weapon (indices 0..2: rifle, shotgun, melee). */
struct PROJECTNR_API FNRRoomCombatRecord
{
	float Damage[NRAdaptWeaponCount] = {};
	int32 Kills[NRAdaptWeaponCount] = {};
	/** Magazines fired (ranged only): the fallback for which weapon was used when nothing was hit. */
	float Magazines[NRAdaptWeaponCount] = {};

	bool HasData() const;
	FString ToString() const;
};

/**
 * The adaptation itself, with no world: room records in, profiles and modifiers out. The
 * subsystem feeds it from play; the automation test drives it directly.
 */
class PROJECTNR_API FNRAdaptationModel
{
public:
	/** Learns from one cleared room. bCountsAsStage adds one to the stage (combat rooms). */
	void CommitRoom(const FNRRoomCombatRecord& Record, const UNRAdaptationConfig& Config, bool bCountsAsStage = true);

	void Reset();

	const FEnemyAdaptationProfile& GetProfile(ENREnemyFamily Family) const { return Profiles[static_cast<int32>(Family)]; }

	/** How far the enemies have learned a weapon, 0..1. */
	float GetWeaponLevel(EAdaptationWeapon Weapon) const;

	/** -1 (more close-in enemies) .. +1 (more shooters). */
	float GetRangedBias(const UNRAdaptationConfig& Config) const;

	int32 GetStage() const { return Stage; }
	int32 GetRoomsLearned() const { return RoomsLearned; }

	/** Stats for an enemy of Family joining a room with the player at PlayerLevel. */
	FEnemyCombatModifiers BuildModifiers(ENREnemyFamily Family, int32 PlayerLevel, const UNRAdaptationConfig& Config) const;

	/** Share of the room each weapon was used for, 0..1, summing to 1 (or all 0 with no data). */
	static void ComputeUsageShare(const FNRRoomCombatRecord& Record, float OutShare[NRAdaptWeaponCount]);

	/** Test and console hooks. */
	void SetStage(int32 InStage) { Stage = FMath::Max(InStage, 0); }

private:
	FEnemyAdaptationProfile Profiles[NREnemyFamilyCount];
	float WeaponLevels[NRAdaptWeaponCount] = {};
	int32 Stage = 0;
	int32 RoomsLearned = 0;
};
