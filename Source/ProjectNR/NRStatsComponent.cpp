// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRStatsComponent.h"

#include "Characters/EnemyCharacter.h"
#include "Components/HealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "MoverComponent.h"
#include "MovementMode.h"
#include "NRStaminaComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRStats, Log, All);

UNRStatsComponent::UNRStatsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	auto Define = [this](ENRPlayerStat Stat, const FText& Name, const FText& Description, float Base, float PerPoint,
		int32 Decimals, bool bPercent)
	{
		FNRStatDefinition& Definition = Definitions[static_cast<int32>(Stat)];
		Definition.DisplayName = Name;
		Definition.Description = Description;
		Definition.BaseValue = Base;
		Definition.PerPoint = PerPoint;
		Definition.Decimals = Decimals;
		Definition.bPercent = bPercent;
	};

	// From the character growth spec (2026-10-07).
	Definitions.SetNum(NRPlayerStatCount);
	Define(ENRPlayerStat::MaxHealth,
		NSLOCTEXT("NRStats", "MaxHealth", "최대 체력"),
		NSLOCTEXT("NRStats", "MaxHealthDesc", "플레이어가 가질 수 있는 체력의 상한"),
		200.0f, 5.0f, 0, false);
	Define(ENRPlayerStat::KillHeal,
		NSLOCTEXT("NRStats", "KillHeal", "처치 시 체력 재생"),
		NSLOCTEXT("NRStats", "KillHealDesc", "몬스터를 처치하면 체력을 일부 회복"),
		0.1f, 0.1f, 1, false);
	Define(ENRPlayerStat::Armor,
		NSLOCTEXT("NRStats", "Armor", "방어력"),
		NSLOCTEXT("NRStats", "ArmorDesc", "방어력의 수치만큼 받는 피해를 고정 감소"),
		0.0f, 1.0f, 0, false);
	Define(ENRPlayerStat::MoveSpeed,
		NSLOCTEXT("NRStats", "MoveSpeed", "이동 속도"),
		NSLOCTEXT("NRStats", "MoveSpeedDesc", "플레이어의 이동 속도"),
		100.0f, 1.0f, 0, true);
	Define(ENRPlayerStat::MaxStamina,
		NSLOCTEXT("NRStats", "MaxStamina", "최대 스테미나"),
		NSLOCTEXT("NRStats", "MaxStaminaDesc", "구르기에 사용하는 게이지의 최대치"),
		50.0f, 5.0f, 0, false);
}

void UNRStatsComponent::BeginPlay()
{
	Super::BeginPlay();

	Invested.Init(0, NRPlayerStatCount);
	AnyDamagedHandle = UHealthComponent::OnAnyDamaged.AddUObject(this, &UNRStatsComponent::HandleAnyDamaged);
	ApplyStats();

	// Movement modes may finish setting up after this component's BeginPlay; try speed again
	// next frame so the base values are read from the live mode.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { ApplyMoveSpeed(); }));
	}
}

void UNRStatsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UHealthComponent::OnAnyDamaged.Remove(AnyDamagedHandle);
	AnyDamagedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

const FNRStatDefinition& UNRStatsComponent::GetDefinition(ENRPlayerStat Stat) const
{
	static const FNRStatDefinition Empty;
	const int32 Index = static_cast<int32>(Stat);
	return Definitions.IsValidIndex(Index) ? Definitions[Index] : Empty;
}

int32 UNRStatsComponent::GetInvestedPoints(ENRPlayerStat Stat) const
{
	const int32 Index = static_cast<int32>(Stat);
	return Invested.IsValidIndex(Index) ? Invested[Index] : 0;
}

float UNRStatsComponent::GetStatValueWithPoints(ENRPlayerStat Stat, int32 Points) const
{
	const FNRStatDefinition& Definition = GetDefinition(Stat);
	return Definition.BaseValue + Definition.PerPoint * FMath::Max(Points, 0);
}

FText UNRStatsComponent::FormatValue(ENRPlayerStat Stat, float Value) const
{
	const FNRStatDefinition& Definition = GetDefinition(Stat);
	FNumberFormattingOptions Options;
	Options.MinimumFractionalDigits = Definition.Decimals;
	Options.MaximumFractionalDigits = Definition.Decimals;
	Options.UseGrouping = false;
	const FText Number = FText::AsNumber(Value, &Options);
	return Definition.bPercent ? FText::Format(NSLOCTEXT("NRStats", "Percent", "{0}%"), Number) : Number;
}

int32 UNRStatsComponent::GetExperienceToNextLevel() const
{
	return XPForFirstLevel + XPPerLevel * FMath::Max(Level - 1, 0);
}

void UNRStatsComponent::AddExperience(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}

	Experience += Amount;
	while (Experience >= GetExperienceToNextLevel())
	{
		Experience -= GetExperienceToNextLevel();
		++Level;
		UnspentPoints += PointsPerLevel;
		UE_LOG(LogNRStats, Log, TEXT("%s reached level %d, %d point(s) to spend"), *GetNameSafe(GetOwner()), Level, UnspentPoints);
	}
	OnStatsChanged.Broadcast();
}

void UNRStatsComponent::AddStatPoints(int32 Amount)
{
	if (Amount > 0)
	{
		UnspentPoints += Amount;
		OnStatsChanged.Broadcast();
	}
}

bool UNRStatsComponent::SpendPoints(const TArray<int32>& Points)
{
	int32 Total = 0;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (Points[Index] < 0 || (Points[Index] > 0 && !Invested.IsValidIndex(Index)))
		{
			return false;
		}
		Total += Points[Index];
	}
	if (Total <= 0 || Total > UnspentPoints)
	{
		return false;
	}

	for (int32 Index = 0; Index < Points.Num() && Index < Invested.Num(); ++Index)
	{
		Invested[Index] += Points[Index];
	}
	UnspentPoints -= Total;
	ApplyStats();

	UE_LOG(LogNRStats, Log, TEXT("%s spent %d point(s): health %.0f, kill heal %.1f, armor %.0f, speed %.0f%%, stamina %.0f"),
		*GetNameSafe(GetOwner()), Total,
		GetStatValue(ENRPlayerStat::MaxHealth), GetStatValue(ENRPlayerStat::KillHeal), GetStatValue(ENRPlayerStat::Armor),
		GetStatValue(ENRPlayerStat::MoveSpeed), GetStatValue(ENRPlayerStat::MaxStamina));
	OnStatsChanged.Broadcast();
	return true;
}

void UNRStatsComponent::HandleAnyDamaged(UHealthComponent* Health, float Damage)
{
	if (!Health || Health->GetWorld() != GetWorld())
	{
		return;
	}

	// OnAnyDamaged fires before the dead flag latches: no health left means this was the kill.
	const AActor* Victim = Health->GetOwner();
	if (!Victim || !Victim->IsA<AEnemyCharacter>() || Health->GetCurrentHealth() > 0.0f)
	{
		return;
	}

	if (UHealthComponent* Own = GetOwner()->FindComponentByClass<UHealthComponent>())
	{
		Own->Heal(GetStatValue(ENRPlayerStat::KillHeal));
	}
	AddExperience(XPPerKill);
}

void UNRStatsComponent::ApplyStats()
{
	AActor* Owner = GetOwner();
	if (UHealthComponent* Health = Owner->FindComponentByClass<UHealthComponent>())
	{
		Health->SetMaxHealth(GetStatValue(ENRPlayerStat::MaxHealth));
		Health->SetArmor(GetStatValue(ENRPlayerStat::Armor));
	}
	if (UNRStaminaComponent* Stamina = Owner->FindComponentByClass<UNRStaminaComponent>())
	{
		Stamina->SetMaxStamina(GetStatValue(ENRPlayerStat::MaxStamina));
	}
	ApplyMoveSpeed();
}

void UNRStatsComponent::ApplyMoveSpeed()
{
	const UMoverComponent* Mover = GetOwner()->FindComponentByClass<UMoverComponent>();
	UBaseMovementMode* Walking = Mover ? Mover->FindMovementModeByName(TEXT("walking")) : nullptr;
	if (!Walking)
	{
		return;
	}

	// The sample's BP_MovementMode_Walking picks its max speed from these by gait every step.
	// Matched on the authored name; the class is a Blueprint and offers nothing to call. The
	// authored values are kept the first time so the scale never compounds.
	if (SpeedEntries.Num() == 0 || SpeedEntries[0].Mode.Get() != Walking)
	{
		SpeedEntries.Reset();
		for (TFieldIterator<FNumericProperty> It(Walking->GetClass()); It; ++It)
		{
			const FString Name = It->GetAuthoredName();
			if (It->IsFloatingPoint() && (Name.Equals(TEXT("WalkSpeed"), ESearchCase::IgnoreCase)
				|| Name.Equals(TEXT("RunSpeed"), ESearchCase::IgnoreCase)
				|| Name.Equals(TEXT("SprintSpeed"), ESearchCase::IgnoreCase)
				|| Name.Equals(TEXT("CrouchSpeed"), ESearchCase::IgnoreCase)))
			{
				FSpeedEntry& Entry = SpeedEntries.AddDefaulted_GetRef();
				Entry.Mode = Walking;
				Entry.Property = *It;
				Entry.Base = It->GetFloatingPointPropertyValue(It->ContainerPtrToValuePtr<void>(Walking));
			}
		}
		if (SpeedEntries.Num() == 0)
		{
			UE_LOG(LogNRStats, Warning, TEXT("%s: walking mode %s has no speed variables; move speed stat has no effect"),
				*GetNameSafe(GetOwner()), *GetNameSafe(Walking->GetClass()));
			return;
		}
	}

	const double Scale = GetStatValue(ENRPlayerStat::MoveSpeed) / 100.0;
	for (const FSpeedEntry& Entry : SpeedEntries)
	{
		if (UObject* Mode = Entry.Mode.Get())
		{
			Entry.Property->SetFloatingPointPropertyValue(Entry.Property->ContainerPtrToValuePtr<void>(Mode), Entry.Base * Scale);
			UE_LOG(LogNRStats, Verbose, TEXT("%s = %.1f"), *Entry.Property->GetAuthoredName(), Entry.Base * Scale);
		}
	}
}

// NR.Stats.Status / NR.Stats.AddXP [n] / NR.Stats.AddPoints [n]
namespace NRStatsCommands
{
	UNRStatsComponent* GetStats(UWorld* World)
	{
		const APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		UNRStatsComponent* Stats = Pawn ? Pawn->FindComponentByClass<UNRStatsComponent>() : nullptr;
		if (!Stats)
		{
			UE_LOG(LogNRStats, Warning, TEXT("The player pawn has no UNRStatsComponent"));
		}
		return Stats;
	}

	void Status(const TArray<FString>& Args, UWorld* World)
	{
		const UNRStatsComponent* Stats = GetStats(World);
		if (!Stats)
		{
			return;
		}
		UE_LOG(LogNRStats, Log, TEXT("Level %d, XP %d/%d, %d point(s) unspent"),
			Stats->GetLevel(), Stats->GetExperience(), Stats->GetExperienceToNextLevel(), Stats->GetUnspentPoints());
		for (int32 Index = 0; Index < NRPlayerStatCount; ++Index)
		{
			const ENRPlayerStat Stat = static_cast<ENRPlayerStat>(Index);
			UE_LOG(LogNRStats, Log, TEXT("  %s: %s (%d point(s))"), *Stats->GetDefinition(Stat).DisplayName.ToString(),
				*Stats->FormatValue(Stat, Stats->GetStatValue(Stat)).ToString(), Stats->GetInvestedPoints(Stat));
		}
	}

	void AddXP(const TArray<FString>& Args, UWorld* World)
	{
		if (UNRStatsComponent* Stats = GetStats(World))
		{
			Stats->AddExperience(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 100);
			Status(Args, World);
		}
	}

	void AddPoints(const TArray<FString>& Args, UWorld* World)
	{
		if (UNRStatsComponent* Stats = GetStats(World))
		{
			Stats->AddStatPoints(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1);
			Status(Args, World);
		}
	}

	/** NR.Stats.Spend <health|killheal|armor|speed|stamina> [n]: what Apply on the stat page does. */
	void Spend(const TArray<FString>& Args, UWorld* World)
	{
		UNRStatsComponent* Stats = GetStats(World);
		if (!Stats || Args.Num() == 0)
		{
			return;
		}

		static const TCHAR* Names[] = { TEXT("health"), TEXT("killheal"), TEXT("armor"), TEXT("speed"), TEXT("stamina") };
		TArray<int32> Points;
		Points.Init(0, NRPlayerStatCount);
		for (int32 Index = 0; Index < NRPlayerStatCount; ++Index)
		{
			if (Args[0].Equals(Names[Index], ESearchCase::IgnoreCase))
			{
				Points[Index] = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 1;
			}
		}
		if (!Stats->SpendPoints(Points))
		{
			UE_LOG(LogNRStats, Warning, TEXT("Could not spend: not enough points or unknown stat"));
		}
		Status(Args, World);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRStatsSpendCommand(TEXT("NR.Stats.Spend"),
	TEXT("NR.Stats.Spend <health|killheal|armor|speed|stamina> [n]: spend points, as Apply does."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRStatsCommands::Spend));

static FAutoConsoleCommandWithWorldAndArgs GNRStatsStatusCommand(TEXT("NR.Stats.Status"),
	TEXT("Log the player's level, experience, points and stats."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRStatsCommands::Status));

static FAutoConsoleCommandWithWorldAndArgs GNRStatsAddXPCommand(TEXT("NR.Stats.AddXP"),
	TEXT("NR.Stats.AddXP [amount]: give the player experience (default 100)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRStatsCommands::AddXP));

static FAutoConsoleCommandWithWorldAndArgs GNRStatsAddPointsCommand(TEXT("NR.Stats.AddPoints"),
	TEXT("NR.Stats.AddPoints [n]: give the player stat points (default 1)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRStatsCommands::AddPoints));
