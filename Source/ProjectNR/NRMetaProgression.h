// Copyright Epic Games, Inc. All Rights Reserved.

// Permanent growth between runs (영구 성장). Every finished run hands out points; points buy levels
// (0..5) of seven stats that make every later run a little easier: max health, max stamina, ammo the
// guns can carry, item drop rate, experience, gold and the parts inventory's size.
//
// The levels and points live in a save game (UNRMetaSave, slot "NRMeta") behind a game instance
// subsystem, so they outlive runs and levels; the stat systems ask the subsystem for their bonus.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NRMetaProgression.generated.h"

class UObject;

/** The stats permanent points go into. Order is the order on the growth screen. */
UENUM(BlueprintType)
enum class ENRMetaStat : uint8
{
	MaxHealth,
	MaxStamina,
	AmmoReserve,
	ItemDropRate,
	ExperienceGain,
	GoldGain,
	InventorySlots,
	Count UMETA(Hidden)
};

constexpr int32 NRMetaStatCount = static_cast<int32>(ENRMetaStat::Count);

/** What is saved between runs. */
UCLASS()
class PROJECTNR_API UNRMetaSave : public USaveGame
{
	GENERATED_BODY()

public:
	/** Points not spent yet. */
	UPROPERTY()
	int32 Points = 0;

	/** Every point ever earned, for the record. */
	UPROPERTY()
	int32 TotalEarned = 0;

	UPROPERTY()
	int32 RunsFinished = 0;

	/** Level per ENRMetaStat, 0..MaxLevel. */
	UPROPERTY()
	TArray<int32> Levels;
};

/** The numbers, in Project Settings > Game > NR Meta Progression. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "NR Meta Progression"))
class PROJECTNR_API UNRMetaProgressionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UNRMetaProgressionSettings();

	/** Levels per stat. */
	UPROPERTY(config, EditAnywhere, Category = "Levels", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxLevel = 5;

	/** Points to reach level 1, 2, 3... (the cost rises with the level). */
	UPROPERTY(config, EditAnywhere, Category = "Levels")
	TArray<int32> LevelCosts;

	/** Points every finished run gives, won or lost. */
	UPROPERTY(config, EditAnywhere, Category = "Points", meta = (ClampMin = "0"))
	int32 PointsPerRun = 1;

	/** One more point for every this many rooms cleared in the run. */
	UPROPERTY(config, EditAnywhere, Category = "Points", meta = (ClampMin = "1"))
	int32 RoomsPerPoint = 3;

	/** Extra points for a won run. */
	UPROPERTY(config, EditAnywhere, Category = "Points", meta = (ClampMin = "0"))
	int32 WinBonusPoints = 3;

	/** Gain per level: max health +5, max stamina +5, ammo +1 magazine per gun (rifle 30 / shotgun 6
	 *  rounds), drop rate / experience / gold +4 %, parts inventory +1 slot. */
	UPROPERTY(config, EditAnywhere, Category = "Gains", EditFixedSize)
	TArray<float> GainPerLevel;

	/** Parts inventory slots before any growth (unequipped parts). */
	UPROPERTY(config, EditAnywhere, Category = "Gains", meta = (ClampMin = "1"))
	int32 BaseInventorySlots = 30;

	/** Save slot the progress is kept in. */
	UPROPERTY(config, EditAnywhere, Category = "Save")
	FString SaveSlot = TEXT("NRMeta");
};

/** The rules on their own, testable without a game (ProjectNR.Meta.Progression). */
namespace NRMeta
{
	/** Points to go from Level to Level + 1; 0 at or above the max. */
	PROJECTNR_API int32 CostOfNextLevel(const UNRMetaProgressionSettings& Settings, int32 Level);

	/** Points a finished run gives. */
	PROJECTNR_API int32 PointsForRun(const UNRMetaProgressionSettings& Settings, int32 RoomsCleared, bool bWon);

	/** Total gain of Stat at Level (health points, magazines, a share like 0.08 for 8 %, slots). */
	PROJECTNR_API float GainAt(const UNRMetaProgressionSettings& Settings, ENRMetaStat Stat, int32 Level);

	PROJECTNR_API FText GetStatName(ENRMetaStat Stat);

	/** "+10", "+2 탄창", "+8%", "+2칸": the gain as the growth screen shows it. */
	PROJECTNR_API FText FormatGain(ENRMetaStat Stat, float Gain);
}

DECLARE_MULTICAST_DELEGATE(FNROnMetaProgressChanged);

/**
 * Holds the permanent progress for the whole game session and saves it on every change. Gameplay
 * code reads bonuses through the static helpers, which fall back to "no bonus" when there is no game
 * instance (editor previews, automation without a world).
 */
UCLASS()
class PROJECTNR_API UNRMetaProgressionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static UNRMetaProgressionSubsystem* Get(const UObject* WorldContext);

	int32 GetPoints() const { return Save ? Save->Points : 0; }
	int32 GetLevel(ENRMetaStat Stat) const;
	int32 GetMaxLevel() const;
	/** 0 when maxed. */
	int32 GetNextCost(ENRMetaStat Stat) const;
	bool CanUpgrade(ENRMetaStat Stat) const;

	/** Spends the next level's cost. False when maxed or short of points. Saves. */
	bool Upgrade(ENRMetaStat Stat);

	/** Adds points (run rewards, console). Saves. */
	void AddPoints(int32 Amount);

	/** Gives a finished run's points once and returns how many. Saves. */
	int32 AwardRun(int32 RoomsCleared, bool bWon);

	/** Points the last finished run gave (0 before any). For the run-end screen. */
	int32 GetLastRunAward() const { return LastRunAward; }

	/** All levels back to 0 with every spent point returned. Saves. */
	void Refund();

	/** Wipes everything (points too). Saves. */
	void ResetAll();

	float GetGain(ENRMetaStat Stat) const;

	// Bonuses for gameplay code; neutral without a subsystem.
	static float GetFlatBonus(const UObject* WorldContext, ENRMetaStat Stat);
	/** 1 + the stat's share, e.g. 1.08 at +8 %. */
	static float GetMultiplier(const UObject* WorldContext, ENRMetaStat Stat);
	static int32 GetInventorySlots(const UObject* WorldContext);

	FNROnMetaProgressChanged OnChanged;

private:
	void SaveNow();
	void Normalize();

	UPROPERTY()
	TObjectPtr<UNRMetaSave> Save;

	int32 LastRunAward = 0;
};
