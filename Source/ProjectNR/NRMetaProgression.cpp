// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRMetaProgression.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRMeta, Log, All);

// --- Settings ----------------------------------------------------------------------

UNRMetaProgressionSettings::UNRMetaProgressionSettings()
{
	CategoryName = TEXT("Game");
	// From the permanent growth spec (2026-11-16): cost rises 1, 2, 4, 6, 8 (21 points per stat).
	LevelCosts = { 1, 2, 4, 6, 8 };
	GainPerLevel.Init(0.0f, NRMetaStatCount);
	GainPerLevel[static_cast<int32>(ENRMetaStat::MaxHealth)] = 5.0f;
	GainPerLevel[static_cast<int32>(ENRMetaStat::MaxStamina)] = 5.0f;
	GainPerLevel[static_cast<int32>(ENRMetaStat::AmmoReserve)] = 1.0f;     // magazines: rifle 30, shotgun 6 rounds
	GainPerLevel[static_cast<int32>(ENRMetaStat::ItemDropRate)] = 0.04f;
	GainPerLevel[static_cast<int32>(ENRMetaStat::ExperienceGain)] = 0.04f;
	GainPerLevel[static_cast<int32>(ENRMetaStat::GoldGain)] = 0.04f;
	GainPerLevel[static_cast<int32>(ENRMetaStat::InventorySlots)] = 1.0f;
}

// --- Rules -------------------------------------------------------------------------

int32 NRMeta::CostOfNextLevel(const UNRMetaProgressionSettings& Settings, int32 Level)
{
	if (Level < 0 || Level >= Settings.MaxLevel)
	{
		return 0;
	}
	if (Settings.LevelCosts.IsValidIndex(Level))
	{
		return FMath::Max(Settings.LevelCosts[Level], 1);
	}
	// Past the list: keep the last step's growth.
	const int32 Last = Settings.LevelCosts.Num() > 0 ? Settings.LevelCosts.Last() : 1;
	return FMath::Max(Last + 2 * (Level - Settings.LevelCosts.Num() + 1), 1);
}

int32 NRMeta::PointsForRun(const UNRMetaProgressionSettings& Settings, int32 RoomsCleared, bool bWon)
{
	return FMath::Max(Settings.PointsPerRun, 0) + FMath::Max(RoomsCleared, 0) / FMath::Max(Settings.RoomsPerPoint, 1)
		+ (bWon ? FMath::Max(Settings.WinBonusPoints, 0) : 0);
}

float NRMeta::GainAt(const UNRMetaProgressionSettings& Settings, ENRMetaStat Stat, int32 Level)
{
	const int32 Index = static_cast<int32>(Stat);
	const float Per = Settings.GainPerLevel.IsValidIndex(Index) ? Settings.GainPerLevel[Index] : 0.0f;
	return Per * FMath::Clamp(Level, 0, Settings.MaxLevel);
}

FText NRMeta::GetStatName(ENRMetaStat Stat)
{
	switch (Stat)
	{
	case ENRMetaStat::MaxHealth: return NSLOCTEXT("NRMeta", "MaxHealth", "최대 체력");
	case ENRMetaStat::MaxStamina: return NSLOCTEXT("NRMeta", "MaxStamina", "최대 스테미나");
	case ENRMetaStat::AmmoReserve: return NSLOCTEXT("NRMeta", "AmmoReserve", "보유 탄약 상한");
	case ENRMetaStat::ItemDropRate: return NSLOCTEXT("NRMeta", "ItemDropRate", "아이템 드롭률");
	case ENRMetaStat::ExperienceGain: return NSLOCTEXT("NRMeta", "ExperienceGain", "경험치 획득량");
	case ENRMetaStat::GoldGain: return NSLOCTEXT("NRMeta", "GoldGain", "골드 획득량");
	case ENRMetaStat::InventorySlots: return NSLOCTEXT("NRMeta", "InventorySlots", "인벤토리 상한");
	default: return FText::GetEmpty();
	}
}

FText NRMeta::FormatGain(ENRMetaStat Stat, float Gain)
{
	switch (Stat)
	{
	case ENRMetaStat::AmmoReserve:
		// One magazine per gun: rifle +30, shotgun +6 rounds.
		return FText::Format(NSLOCTEXT("NRMeta", "GainAmmo", "+{0}탄창"), FMath::RoundToInt32(Gain));
	case ENRMetaStat::ItemDropRate:
	case ENRMetaStat::ExperienceGain:
	case ENRMetaStat::GoldGain:
		return FText::Format(NSLOCTEXT("NRMeta", "GainPercent", "+{0}%"), FMath::RoundToInt32(Gain * 100.0f));
	case ENRMetaStat::InventorySlots:
		return FText::Format(NSLOCTEXT("NRMeta", "GainSlots", "+{0}칸"), FMath::RoundToInt32(Gain));
	default:
		return FText::Format(NSLOCTEXT("NRMeta", "GainFlat", "+{0}"), FMath::RoundToInt32(Gain));
	}
}

// --- Subsystem ---------------------------------------------------------------------

void UNRMetaProgressionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const FString& Slot = GetDefault<UNRMetaProgressionSettings>()->SaveSlot;
	Save = UGameplayStatics::DoesSaveGameExist(Slot, 0) ? Cast<UNRMetaSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0)) : nullptr;
	if (!Save)
	{
		Save = Cast<UNRMetaSave>(UGameplayStatics::CreateSaveGameObject(UNRMetaSave::StaticClass()));
	}
	Normalize();
	UE_LOG(LogNRMeta, Log, TEXT("Meta progression loaded from '%s': %d point(s), %d run(s)"), *Slot, Save->Points, Save->RunsFinished);
}

UNRMetaProgressionSubsystem* UNRMetaProgressionSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UNRMetaProgressionSubsystem>() : nullptr;
}

void UNRMetaProgressionSubsystem::Normalize()
{
	const int32 Max = GetMaxLevel();
	Save->Levels.SetNumZeroed(NRMetaStatCount);
	for (int32& Level : Save->Levels)
	{
		Level = FMath::Clamp(Level, 0, Max);
	}
	Save->Points = FMath::Max(Save->Points, 0);
}

void UNRMetaProgressionSubsystem::SaveNow()
{
	const FString& Slot = GetDefault<UNRMetaProgressionSettings>()->SaveSlot;
	if (!UGameplayStatics::SaveGameToSlot(Save, Slot, 0))
	{
		UE_LOG(LogNRMeta, Warning, TEXT("Could not save meta progression to '%s'"), *Slot);
	}
	OnChanged.Broadcast();
}

int32 UNRMetaProgressionSubsystem::GetLevel(ENRMetaStat Stat) const
{
	const int32 Index = static_cast<int32>(Stat);
	return Save && Save->Levels.IsValidIndex(Index) ? Save->Levels[Index] : 0;
}

int32 UNRMetaProgressionSubsystem::GetMaxLevel() const
{
	return GetDefault<UNRMetaProgressionSettings>()->MaxLevel;
}

int32 UNRMetaProgressionSubsystem::GetNextCost(ENRMetaStat Stat) const
{
	return NRMeta::CostOfNextLevel(*GetDefault<UNRMetaProgressionSettings>(), GetLevel(Stat));
}

bool UNRMetaProgressionSubsystem::CanUpgrade(ENRMetaStat Stat) const
{
	const int32 Cost = GetNextCost(Stat);
	return Save && Cost > 0 && Save->Points >= Cost;
}

bool UNRMetaProgressionSubsystem::Upgrade(ENRMetaStat Stat)
{
	if (!CanUpgrade(Stat))
	{
		return false;
	}
	const int32 Cost = GetNextCost(Stat);
	Save->Points -= Cost;
	++Save->Levels[static_cast<int32>(Stat)];
	UE_LOG(LogNRMeta, Log, TEXT("%s -> level %d for %d point(s), %d left"), *NRMeta::GetStatName(Stat).ToString(), GetLevel(Stat), Cost, Save->Points);
	SaveNow();
	return true;
}

void UNRMetaProgressionSubsystem::AddPoints(int32 Amount)
{
	if (!Save || Amount == 0)
	{
		return;
	}
	Save->Points = FMath::Max(Save->Points + Amount, 0);
	if (Amount > 0)
	{
		Save->TotalEarned += Amount;
	}
	SaveNow();
}

int32 UNRMetaProgressionSubsystem::AwardRun(int32 RoomsCleared, bool bWon)
{
	if (!Save)
	{
		return 0;
	}
	LastRunAward = NRMeta::PointsForRun(*GetDefault<UNRMetaProgressionSettings>(), RoomsCleared, bWon);
	++Save->RunsFinished;
	UE_LOG(LogNRMeta, Log, TEXT("Run %s with %d room(s) cleared: +%d permanent point(s)"), bWon ? TEXT("won") : TEXT("lost"), RoomsCleared, LastRunAward);
	AddPoints(LastRunAward);
	if (LastRunAward == 0)
	{
		SaveNow();
	}
	return LastRunAward;
}

void UNRMetaProgressionSubsystem::Refund()
{
	if (!Save)
	{
		return;
	}
	const UNRMetaProgressionSettings& Settings = *GetDefault<UNRMetaProgressionSettings>();
	for (int32& Level : Save->Levels)
	{
		for (int32 Each = 0; Each < Level; ++Each)
		{
			Save->Points += NRMeta::CostOfNextLevel(Settings, Each);
		}
		Level = 0;
	}
	SaveNow();
}

void UNRMetaProgressionSubsystem::ResetAll()
{
	if (!Save)
	{
		return;
	}
	Save->Points = 0;
	Save->TotalEarned = 0;
	Save->RunsFinished = 0;
	Save->Levels.Init(0, NRMetaStatCount);
	SaveNow();
}

float UNRMetaProgressionSubsystem::GetGain(ENRMetaStat Stat) const
{
	return NRMeta::GainAt(*GetDefault<UNRMetaProgressionSettings>(), Stat, GetLevel(Stat));
}

float UNRMetaProgressionSubsystem::GetFlatBonus(const UObject* WorldContext, ENRMetaStat Stat)
{
	const UNRMetaProgressionSubsystem* Meta = Get(WorldContext);
	return Meta ? Meta->GetGain(Stat) : 0.0f;
}

float UNRMetaProgressionSubsystem::GetMultiplier(const UObject* WorldContext, ENRMetaStat Stat)
{
	return 1.0f + GetFlatBonus(WorldContext, Stat);
}

int32 UNRMetaProgressionSubsystem::GetInventorySlots(const UObject* WorldContext)
{
	return GetDefault<UNRMetaProgressionSettings>()->BaseInventorySlots
		+ FMath::RoundToInt32(GetFlatBonus(WorldContext, ENRMetaStat::InventorySlots));
}

// --- Console -----------------------------------------------------------------------

namespace
{
	UNRMetaProgressionSubsystem* ConsoleMeta(UWorld* World)
	{
		UNRMetaProgressionSubsystem* Meta = UNRMetaProgressionSubsystem::Get(World);
		if (!Meta)
		{
			UE_LOG(LogNRMeta, Warning, TEXT("No meta progression here (needs PIE or a game)"));
		}
		return Meta;
	}

	void LogStatus(const UNRMetaProgressionSubsystem* Meta)
	{
		UE_LOG(LogNRMeta, Log, TEXT("Permanent points: %d"), Meta->GetPoints());
		for (int32 Index = 0; Index < NRMetaStatCount; ++Index)
		{
			const ENRMetaStat Stat = static_cast<ENRMetaStat>(Index);
			UE_LOG(LogNRMeta, Log, TEXT("  %d %s  Lv %d/%d  %s  next %d"), Index, *NRMeta::GetStatName(Stat).ToString(), Meta->GetLevel(Stat),
				Meta->GetMaxLevel(), *NRMeta::FormatGain(Stat, Meta->GetGain(Stat)).ToString(), Meta->GetNextCost(Stat));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRMetaStatusCommand(TEXT("NR.Meta.Status"),
	TEXT("Logs the permanent points and every growth stat."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UNRMetaProgressionSubsystem* Meta = ConsoleMeta(World))
		{
			LogStatus(Meta);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GNRMetaAddCommand(TEXT("NR.Meta.AddPoints"),
	TEXT("NR.Meta.AddPoints <amount> - adds permanent points (saved)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UNRMetaProgressionSubsystem* Meta = ConsoleMeta(World))
		{
			Meta->AddPoints(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 10);
			LogStatus(Meta);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GNRMetaUpgradeCommand(TEXT("NR.Meta.Upgrade"),
	TEXT("NR.Meta.Upgrade <stat 0-6> - spends points on the next level of a growth stat."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		UNRMetaProgressionSubsystem* Meta = ConsoleMeta(World);
		const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : -1;
		if (Meta && Index >= 0 && Index < NRMetaStatCount)
		{
			const bool bOk = Meta->Upgrade(static_cast<ENRMetaStat>(Index));
			UE_LOG(LogNRMeta, Log, TEXT("NR.Meta.Upgrade %d: %s"), Index, bOk ? TEXT("done") : TEXT("refused (max level or not enough points)"));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GNRMetaRefundCommand(TEXT("NR.Meta.Refund"),
	TEXT("Returns every spent permanent point and sets all growth stats to level 0."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UNRMetaProgressionSubsystem* Meta = ConsoleMeta(World))
		{
			Meta->Refund();
			LogStatus(Meta);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GNRMetaResetCommand(TEXT("NR.Meta.Reset"),
	TEXT("Wipes all permanent progress, points included."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UNRMetaProgressionSubsystem* Meta = ConsoleMeta(World))
		{
			Meta->ResetAll();
			LogStatus(Meta);
		}
	}));
