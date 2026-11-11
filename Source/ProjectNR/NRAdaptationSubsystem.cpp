// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRAdaptationSubsystem.h"

#include "Characters/BossEnemyCharacter.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/RangedEnemyCharacter.h"
#include "Components/HealthComponent.h"
#include "Components/NeuralBrainComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NREncounterData.h"
#include "NRRoom.h"
#include "NRSpawnSubsystem.h"
#include "NRStatsComponent.h"
#include "NRWeaponComponent.h"

DEFINE_LOG_CATEGORY(LogNRAdapt);

bool UNRAdaptationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UNRAdaptationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!AnyDamagedHandle.IsValid())
	{
		AnyDamagedHandle = UHealthComponent::OnAnyDamaged.AddUObject(this, &UNRAdaptationSubsystem::HandleAnyDamaged);
	}
}

void UNRAdaptationSubsystem::Deinitialize()
{
	UHealthComponent::OnAnyDamaged.Remove(AnyDamagedHandle);
	AnyDamagedHandle.Reset();
	Super::Deinitialize();
}

const UNRAdaptationConfig& UNRAdaptationSubsystem::GetConfig() const
{
	const UNRSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UNRSpawnSubsystem>() : nullptr;
	const UNREncounterData* Encounter = Spawner ? Spawner->GetEncounterData() : nullptr;
	return Encounter && Encounter->Adaptation ? *Encounter->Adaptation : *GetDefault<UNRAdaptationConfig>();
}

ENREnemyFamily UNRAdaptationSubsystem::GetFamily(const AActor* Enemy)
{
	if (Enemy && Enemy->IsA<ABossEnemyCharacter>())
	{
		return ENREnemyFamily::Boss;
	}
	if (Enemy && Enemy->IsA<ARangedEnemyCharacter>())
	{
		return ENREnemyFamily::Ranged;
	}
	return ENREnemyFamily::Melee;
}

void UNRAdaptationSubsystem::HandleAnyDamaged(UHealthComponent* Health, float Damage)
{
	if (!Health || Health->GetWorld() != GetWorld() || !Cast<AEnemyCharacter>(Health->GetOwner()))
	{
		return;
	}
	const int32 Weapon = NRAdaptWeaponIndex(Health->GetLastDamageSource());
	if (Weapon < 0 || Weapon >= NRAdaptWeaponCount)
	{
		return;
	}
	Record.Damage[Weapon] += Damage;
	// OnAnyDamaged fires before the dead flag latches: no health left means this was the kill.
	if (Health->GetCurrentHealth() <= 0.0f)
	{
		++Record.Kills[Weapon];
	}
}

void UNRAdaptationSubsystem::SnapshotMagazines(float Out[NRAdaptWeaponCount]) const
{
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const UNRWeaponComponent* Weapons = Player ? Player->FindComponentByClass<UNRWeaponComponent>() : nullptr;
	for (int32 Index = 0; Index < NRAdaptWeaponCount; ++Index)
	{
		Out[Index] = Weapons ? Weapons->GetMagazinesFired(static_cast<ENRWeaponId>(Index)) : 0.0f;
	}
}

int32 UNRAdaptationSubsystem::GetPlayerLevel() const
{
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const UNRStatsComponent* Stats = Player ? Player->FindComponentByClass<UNRStatsComponent>() : nullptr;
	return Stats ? Stats->GetLevel() : 1;
}

void UNRAdaptationSubsystem::HandleRoomActivated(ANRRoom* Room)
{
	if (!Room)
	{
		return;
	}
	// One record per fight. A second room woken mid-fight shares the open record: the player's
	// weapons are the same either way.
	if (!bRecording)
	{
		bRecording = true;
		SnapshotMagazines(MagazinesAtStart);
	}
	for (AActor* Enemy : Room->GetEnemies())
	{
		ApplyToEnemy(Enemy);
	}
}

void UNRAdaptationSubsystem::HandleRoomCleared(ANRRoom* Room)
{
	if (!Room || Room->GetTotalEnemies() == 0)
	{
		return;
	}
	const ENRGridRoomType Type = Room->GetRoomType();
	const bool bFighting = Type == ENRGridRoomType::Combat || Type == ENRGridRoomType::Elite || Type == ENRGridRoomType::Boss;
	if (!bFighting)
	{
		return;
	}

	float MagazinesNow[NRAdaptWeaponCount];
	SnapshotMagazines(MagazinesNow);
	for (int32 Index = 0; Index < NRAdaptWeaponCount; ++Index)
	{
		Record.Magazines[Index] = bRecording ? FMath::Max(MagazinesNow[Index] - MagazinesAtStart[Index], 0.0f) : 0.0f;
	}
	UE_LOG(LogNRAdapt, Log, TEXT("%s cleared: %s"), *Room->GetLabel(), *Record.ToString());
	LearnFromRecord(Record, true);

	Record = FNRRoomCombatRecord();
	bRecording = false;
}

void UNRAdaptationSubsystem::LearnFromRecord(const FNRRoomCombatRecord& InRecord, bool bCountsAsStage)
{
	Model.CommitRoom(InRecord, GetConfig(), bCountsAsStage);
	UE_LOG(LogNRAdapt, Log, TEXT("Adaptation for the next room (stage %d):"), Model.GetStage());
	LogStatus();
}

void UNRAdaptationSubsystem::ResetAdaptation()
{
	Model.Reset();
	Record = FNRRoomCombatRecord();
	bRecording = false;
}

float UNRAdaptationSubsystem::GetRangedBias() const
{
	return Model.GetRangedBias(GetConfig());
}

void UNRAdaptationSubsystem::ApplyToEnemy(AActor* Actor)
{
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor);
	if (!IsValid(Enemy) || Enemy->IsDead())
	{
		return;
	}
	const ENREnemyFamily Family = GetFamily(Enemy);
	if (UNeuralBrainComponent* Brain = Enemy->GetNeuralBrain())
	{
		Brain->ApplyAdaptationProfile(Model.GetProfile(Family));
	}
	Enemy->ApplyCombatModifiers(Model.BuildModifiers(Family, GetPlayerLevel(), GetConfig()));
}

void UNRAdaptationSubsystem::LogStatus() const
{
	const UNRAdaptationConfig& Config = GetConfig();
	UE_LOG(LogNRAdapt, Log, TEXT("  stage %d, %d room(s) learned, player level %d, spawn shift %+.2f (+ more shooters)"),
		Model.GetStage(), Model.GetRoomsLearned(), GetPlayerLevel(), Model.GetRangedBias(Config));
	UE_LOG(LogNRAdapt, Log, TEXT("  weapon levels: rifle %.2f, shotgun %.2f, melee %.2f"),
		Model.GetWeaponLevel(EAdaptationWeapon::Rifle), Model.GetWeaponLevel(EAdaptationWeapon::Shotgun),
		Model.GetWeaponLevel(EAdaptationWeapon::Melee));
	for (int32 Family = 0; Family < NREnemyFamilyCount; ++Family)
	{
		const ENREnemyFamily Id = static_cast<ENREnemyFamily>(Family);
		UE_LOG(LogNRAdapt, Log, TEXT("  %s: %s"), *UEnum::GetValueAsString(Id), *Model.GetProfile(Id).ToString());
		UE_LOG(LogNRAdapt, Log, TEXT("      %s"), *Model.BuildModifiers(Id, GetPlayerLevel(), Config).ToString());
	}
	UE_LOG(LogNRAdapt, Log, TEXT("  this room so far: %s"), *Record.ToString());
}

// --- Console -----------------------------------------------------------------------

namespace NRAdaptCommands
{
	UNRAdaptationSubsystem* Get(UWorld* World)
	{
		UNRAdaptationSubsystem* Adapt = World ? World->GetSubsystem<UNRAdaptationSubsystem>() : nullptr;
		if (!Adapt)
		{
			UE_LOG(LogNRAdapt, Warning, TEXT("No adaptation subsystem in this world (needs PIE or a game)"));
		}
		return Adapt;
	}

	void Status(const TArray<FString>& Args, UWorld* World)
	{
		if (const UNRAdaptationSubsystem* Adapt = Get(World))
		{
			Adapt->LogStatus();
		}
	}

	/** NR.Adapt.Learn <rifle|shotgun|melee> [rooms=1] [kills=5]: as if rooms were cleared with that weapon alone. */
	void Learn(const TArray<FString>& Args, UWorld* World)
	{
		UNRAdaptationSubsystem* Adapt = Get(World);
		if (!Adapt || Args.Num() == 0)
		{
			UE_LOG(LogNRAdapt, Warning, TEXT("Usage: NR.Adapt.Learn <rifle|shotgun|melee> [rooms] [kills]"));
			return;
		}
		const FString Name = Args[0].ToLower();
		const int32 Weapon = Name.StartsWith(TEXT("r")) ? 0 : Name.StartsWith(TEXT("s")) ? 1 : Name.StartsWith(TEXT("m")) ? 2 : INDEX_NONE;
		if (Weapon == INDEX_NONE)
		{
			UE_LOG(LogNRAdapt, Warning, TEXT("Unknown weapon '%s'"), *Args[0]);
			return;
		}
		const int32 Rooms = Args.Num() > 1 ? FMath::Max(FCString::Atoi(*Args[1]), 1) : 1;
		const int32 Kills = Args.Num() > 2 ? FMath::Max(FCString::Atoi(*Args[2]), 0) : 5;
		for (int32 Room = 0; Room < Rooms; ++Room)
		{
			FNRRoomCombatRecord Record;
			Record.Damage[Weapon] = 100.0f * Kills;
			Record.Kills[Weapon] = Kills;
			Adapt->LearnFromRecord(Record, true);
		}
	}

	/**
	 * NR.Adapt.KillRoom <rifle|shotgun|melee>: kills the enemies of the player's room as hits from
	 * that weapon, through ApplyDamage like a real hit. The first takes a 10-point probe first, to
	 * show its resistance.
	 */
	void KillRoom(const TArray<FString>& Args, UWorld* World)
	{
		const FString Name = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("rifle"));
		const EAdaptationWeapon Weapon = Name.StartsWith(TEXT("s")) ? EAdaptationWeapon::Shotgun
			: Name.StartsWith(TEXT("m")) ? EAdaptationWeapon::Melee : EAdaptationWeapon::Rifle;
		APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		ANRRoom* Room = Player ? ANRRoom::FindRoomAt(World, Player->GetActorLocation()) : nullptr;
		if (!Room)
		{
			UE_LOG(LogNRAdapt, Warning, TEXT("NR.Adapt.KillRoom: the player is not inside any room"));
			return;
		}
		bool bProbed = false;
		int32 Killed = 0;
		for (AActor* Enemy : Room->GetEnemies())
		{
			UHealthComponent* Health = IsValid(Enemy) ? Enemy->FindComponentByClass<UHealthComponent>() : nullptr;
			if (!Health || Health->IsDead())
			{
				continue;
			}
			Health->ClearInvulnerability();
			if (!bProbed)
			{
				bProbed = true;
				const float Applied = Health->ApplyDamage(10.0f, Player, FVector::ForwardVector, Weapon);
				UE_LOG(LogNRAdapt, Log, TEXT("Probe on %s: 10 %s damage -> %.2f applied (resistance %.0f%%, max health %.0f)"),
					*Enemy->GetName(), *UEnum::GetValueAsString(Weapon), Applied, Health->GetWeaponResistance(Weapon) * 100.0f,
					Health->GetMaxHealth());
			}
			// Generous: resistance and armor come off first.
			Health->ApplyDamage((Health->GetCurrentHealth() + Health->GetCurrentShield()) * 3.0f + 100.0f, Player, FVector::ForwardVector, Weapon);
			++Killed;
		}
		UE_LOG(LogNRAdapt, Log, TEXT("Killed %d enemy(s) of %s with the %s"), Killed, *Room->GetLabel(), *UEnum::GetValueAsString(Weapon));
	}

	void Reset(const TArray<FString>& Args, UWorld* World)
	{
		if (UNRAdaptationSubsystem* Adapt = Get(World))
		{
			Adapt->ResetAdaptation();
			UE_LOG(LogNRAdapt, Log, TEXT("Adaptation reset"));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRAdaptStatusCommand(TEXT("NR.Adapt.Status"),
	TEXT("Log the enemies' adaptation: profiles per kind of enemy, weapon levels, modifiers, this room's record."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRAdaptCommands::Status));

static FAutoConsoleCommandWithWorldAndArgs GNRAdaptLearnCommand(TEXT("NR.Adapt.Learn"),
	TEXT("NR.Adapt.Learn <rifle|shotgun|melee> [rooms=1] [kills=5]: learn as if rooms were cleared with that weapon alone."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRAdaptCommands::Learn));

static FAutoConsoleCommandWithWorldAndArgs GNRAdaptKillRoomCommand(TEXT("NR.Adapt.KillRoom"),
	TEXT("NR.Adapt.KillRoom <rifle|shotgun|melee>: kill the enemies of the player's room as hits from that weapon (feeds adaptation)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRAdaptCommands::KillRoom));

static FAutoConsoleCommandWithWorldAndArgs GNRAdaptResetCommand(TEXT("NR.Adapt.Reset"),
	TEXT("Forget everything the enemies learned this run."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRAdaptCommands::Reset));
