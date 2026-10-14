// Neural Enemy AI prototype module.

// Test tools for climbing, typed in the console during play:
//   NeuralAI.Climb.Test [all] [toward]   the enemy nearest the player (or all of them) climbs the
//                                        nearest wall it can (toward: the wall between it and the player)
//   NeuralAI.Climb.Probe [all] [toward]  the same probe, drawn and logged for 5 s, without climbing
//   NeuralAI.Climb.Cancel                every climb under way gives up and drops
//   NeuralAI.Climb.SpawnAt <x> <y> [z] [yaw]  a crawler standing there, tagged for NeuralAI.PositionalSlots.Clear
//   NeuralAI.Climb.Where                 logs where every enemy is and whether it is climbing
//   NeuralAI.Climb.KillClimbers          kills every enemy on a wall, to watch it fall
//   NeuralAI.Climb.Debug 1               draws climbs under way

#include "Characters/EnemyCharacter.h"
#include "Climb/EnemyClimbComponent.h"
#include "Climb/EnemyClimbProbe.h"
#include "Climb/EnemyClimbSettings.h"
#include "Components/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NeuralAI.h"

namespace EnemyClimbTest
{
	/** Live enemies that climb: all of them, or the one nearest the player. */
	TArray<AEnemyCharacter*> PickEnemies(UWorld* World, bool bAll)
	{
		TArray<AEnemyCharacter*> Enemies;
		const APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
		const FVector From = Player ? Player->GetActorLocation() : FVector::ZeroVector;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (!It->IsDead() && It->CanClimb() && It->GetClimbComponent())
			{
				Enemies.Add(*It);
			}
		}
		if (!bAll && Enemies.Num() > 1)
		{
			Enemies.Sort([&From](const AEnemyCharacter& A, const AEnemyCharacter& B)
			{
				return FVector::DistSquared(A.GetActorLocation(), From) < FVector::DistSquared(B.GetActorLocation(), From);
			});
			Enemies.SetNum(1);
		}
		return Enemies;
	}

	void Run(const TArray<FString>& Args, UWorld* World, bool bClimb)
	{
		if (!World)
		{
			return;
		}
		const bool bAll = Args.Contains(TEXT("all"));
		const bool bToward = Args.Contains(TEXT("toward"));
		const APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
		const UEnemyClimbSettings* Settings = GetDefault<UEnemyClimbSettings>();
		const TArray<AEnemyCharacter*> Enemies = PickEnemies(World, bAll);
		if (Enemies.Num() == 0)
		{
			UE_LOG(LogNeuralAI, Warning, TEXT("Climb: no enemy that climbs"));
			return;
		}
		for (AEnemyCharacter* Enemy : Enemies)
		{
			FEnemyClimbLedge Ledge;
			const EEnemyClimbProbeResult Result = bToward && Player
				? FEnemyClimbProbe::ProbeDirection(World, Enemy, Player->GetActorLocation() - Enemy->GetActorLocation(), *Settings, Ledge)
				: FEnemyClimbProbe::FindNearest(World, Enemy, *Settings, 16, Ledge);
			FEnemyClimbProbe::DrawDebug(World, Ledge, Result, 5.0f);
			UE_LOG(LogNeuralAI, Log, TEXT("Climb probe %s: %s, %s %.0f cm up, foot at %s, stand at %s"), *GetNameSafe(Enemy),
				FEnemyClimbProbe::ToString(Result), *GetNameSafe(Ledge.Wall.Get()), Ledge.GetHeight(),
				*Ledge.BaseCenter.ToCompactString(), *Ledge.StandCenter.ToCompactString());
			if (bClimb && Result == EEnemyClimbProbeResult::Ok)
			{
				Enemy->GetClimbComponent()->StartClimb(Ledge);
			}
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GEnemyClimbTestCommand(TEXT("NeuralAI.Climb.Test"),
	TEXT("NeuralAI.Climb.Test [all] [toward]: the enemy nearest the player (all: every one) climbs the nearest wall it can (toward: the one towards the player)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { EnemyClimbTest::Run(Args, World, true); }));

static FAutoConsoleCommandWithWorldAndArgs GEnemyClimbProbeCommand(TEXT("NeuralAI.Climb.Probe"),
	TEXT("NeuralAI.Climb.Probe [all] [toward]: as NeuralAI.Climb.Test, drawn and logged, without climbing."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { EnemyClimbTest::Run(Args, World, false); }));

static FAutoConsoleCommandWithWorldAndArgs GEnemyClimbSpawnAtCommand(TEXT("NeuralAI.Climb.SpawnAt"),
	TEXT("NeuralAI.Climb.SpawnAt <x> <y> [z] [yaw]: a crawler standing at x y (z: the floor, default 0)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			return;
		}
		const FVector Location(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), (Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.0f) + 70.0f);
		const FRotator Rotation(0.0f, Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.0f, 0.0f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		if (AEnemyCharacter* Enemy = World->SpawnActor<AEnemyCharacter>(AEnemyCharacter::StaticClass(), Location, Rotation, Params))
		{
			// The same tag as NeuralAI.PositionalSlots.Spawn, so its Clear removes these too.
			Enemy->Tags.Add(TEXT("PositionalSlotTest"));
			UE_LOG(LogNeuralAI, Log, TEXT("Climb.SpawnAt: %s at %s"), *GetNameSafe(Enemy), *Enemy->GetActorLocation().ToCompactString());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GEnemyClimbWhereCommand(TEXT("NeuralAI.Climb.Where"),
	TEXT("Logs where every enemy is and whether it is climbing."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			const UEnemyClimbComponent* Climb = It->GetClimbComponent();
			UE_LOG(LogNeuralAI, Log, TEXT("Climb.Where %s at %s, %s%s"), *GetNameSafe(*It), *It->GetActorLocation().ToCompactString(),
				Climb ? *StaticEnum<EEnemyClimbPhase>()->GetNameStringByValue(static_cast<int64>(Climb->GetPhase())) : TEXT("no climb"),
				It->IsDead() ? TEXT(", dead") : TEXT(""));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GEnemyClimbKillCommand(TEXT("NeuralAI.Climb.KillClimbers"),
	TEXT("Kills every enemy on a wall, to watch it fall."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			const UEnemyClimbComponent* Climb = It->GetClimbComponent();
			if (Climb && Climb->IsOnWall() && It->GetHealthComponent())
			{
				It->GetHealthComponent()->ApplyDamage(100000.0f, nullptr, FVector::ZeroVector);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GEnemyClimbCancelCommand(TEXT("NeuralAI.Climb.Cancel"),
	TEXT("Every climb under way gives up and drops."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (UEnemyClimbComponent* Climb = It->GetClimbComponent())
			{
				Climb->Cancel(TEXT("NeuralAI.Climb.Cancel"));
			}
		}
	}));
