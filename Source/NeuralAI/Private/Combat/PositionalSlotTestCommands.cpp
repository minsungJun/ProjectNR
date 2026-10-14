// Neural Enemy AI prototype module.

// Test tools for positional slots, typed in the console during play:
//   NeuralAI.PositionalSlots.Spawn <count> [distance] [melee|ranged|mixed]
//       crawlers (or shooters, or every other one a shooter) bunched by the player, awake
//   NeuralAI.PositionalSlots.Clear                      removes the ones Spawn made
//   NeuralAI.PositionalSlots.DriveTarget <pattern> [speed] [seconds] [yaw]
//       moves the player by itself so the ring can be watched following it. Patterns:
//       line (back and forth), zigzag (a sharp turn every second), circle, straight (on and on,
//       through doors and corridors), stop. Yaw sets the heading; the player's facing otherwise.

#include "Characters/EnemyCharacter.h"
#include "Characters/RangedEnemyCharacter.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NeuralAI.h"

namespace PositionalSlotTest
{
	const FName SpawnTag(TEXT("PositionalSlotTest"));

	void Spawn(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		if (!Player)
		{
			UE_LOG(LogNeuralAI, Warning, TEXT("PositionalSlots.Spawn: no player"));
			return;
		}
		const int32 Count = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 32) : 1;
		const float Distance = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 1200.0f;
		const FString Kind = Args.Num() > 2 ? Args[2].ToLower() : TEXT("melee");
		const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

		// Bunched on one side of the player, so spreading round it is the slots' doing.
		const float Facing = Player->GetActorForwardVector().Rotation().Yaw;
		int32 Spawned = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Yaw = FMath::DegreesToRadians(Facing + (Index - (Count - 1) * 0.5f) * 12.0f);
			FVector Location = Player->GetActorLocation() + FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0f) * Distance;
			FNavLocation Projected;
			if (Navigation && Navigation->ProjectPointToNavigation(Location, Projected, FVector(200.0f, 200.0f, 500.0f)))
			{
				Location = Projected.Location + FVector(0.0f, 0.0f, 70.0f);
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			const FRotator Rotation(0.0f, Facing + 180.0f, 0.0f);
			const bool bRanged = Kind == TEXT("ranged") || (Kind == TEXT("mixed") && Index % 2 == 1);
			UClass* Class = bRanged ? ARangedEnemyCharacter::StaticClass() : AEnemyCharacter::StaticClass();
			if (AEnemyCharacter* Enemy = World->SpawnActor<AEnemyCharacter>(Class, Location, Rotation, Params))
			{
				Enemy->Tags.Add(SpawnTag);
				++Spawned;
			}
		}
		UE_LOG(LogNeuralAI, Log, TEXT("PositionalSlots.Spawn: %d %s enemy(s) %.0f cm from the player"), Spawned, *Kind, Distance);
	}

	void Clear(UWorld* World)
	{
		int32 Removed = 0;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->Tags.Contains(SpawnTag))
			{
				It->Destroy();
				++Removed;
			}
		}
		UE_LOG(LogNeuralAI, Log, TEXT("PositionalSlots.Clear: removed %d"), Removed);
	}

	/** The one drive running, if any. */
	FTSTicker::FDelegateHandle DriveHandle;

	void StopDrive()
	{
		if (DriveHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(DriveHandle);
			DriveHandle.Reset();
		}
	}

	void DriveTarget(const TArray<FString>& Args, UWorld* World)
	{
		StopDrive();
		APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		const FString Pattern = Args.Num() > 0 ? Args[0].ToLower() : TEXT("line");
		if (!Player || Pattern == TEXT("stop"))
		{
			UE_LOG(LogNeuralAI, Log, TEXT("PositionalSlots.DriveTarget: stopped"));
			return;
		}
		const float Speed = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 500.0f;
		const float Seconds = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 8.0f;
		const FVector Start = Player->GetActorLocation();
		const FVector Forward = Args.Num() > 3
			? FRotator(0.0f, FCString::Atof(*Args[3]), 0.0f).Vector()
			: Player->GetActorForwardVector().GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		TWeakObjectPtr<APawn> WeakPlayer = Player;
		TWeakObjectPtr<UWorld> WeakWorld = World;
		const float Began = World->GetTimeSeconds();
		float Travelled = 0.0f;

		UE_LOG(LogNeuralAI, Log, TEXT("PositionalSlots.DriveTarget: %s at %.0f cm/s for %.1f s"), *Pattern, Speed, Seconds);
		DriveHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[WeakPlayer, WeakWorld, Pattern, Speed, Seconds, Start, Forward, Right, Began, Travelled](float DeltaTime) mutable
			{
				APawn* Pawn = WeakPlayer.Get();
				UWorld* PlayWorld = WeakWorld.Get();
				if (!Pawn || !PlayWorld || PlayWorld->GetTimeSeconds() - Began > Seconds)
				{
					UE_LOG(LogNeuralAI, Log, TEXT("PositionalSlots.DriveTarget: done"));
					DriveHandle.Reset();
					return false;
				}
				const float Elapsed = PlayWorld->GetTimeSeconds() - Began;
				Travelled += Speed * DeltaTime;
				FVector Direction = Forward;
				if (Pattern == TEXT("line"))
				{
					// Out and back over 800 cm.
					Direction = FMath::Fmod(Travelled, 1600.0f) < 800.0f ? Forward : -Forward;
				}
				else if (Pattern == TEXT("zigzag"))
				{
					// A 90 degree turn every second.
					const int32 Leg = FMath::FloorToInt(Elapsed);
					Direction = (Leg % 2 == 0 ? Forward + Right : Forward - Right).GetSafeNormal();
					if ((Leg / 2) % 2 == 1)
					{
						Direction = -Direction;
					}
				}
				else if (Pattern == TEXT("circle"))
				{
					const float Angle = Travelled / 500.0f;
					Direction = Forward * FMath::Cos(Angle) + Right * FMath::Sin(Angle);
				}
				const FVector Next = Pawn->GetActorLocation() + Direction * Speed * DeltaTime;
				Pawn->TeleportTo(Next, FRotator(0.0f, Direction.Rotation().Yaw, 0.0f));
				return true;
			}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GPositionalSlotSpawnCommand(TEXT("NeuralAI.PositionalSlots.Spawn"),
	TEXT("NeuralAI.PositionalSlots.Spawn <count> [distance] [melee|ranged|mixed]: enemies bunched on one side of the player, awake."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PositionalSlotTest::Spawn));

static FAutoConsoleCommandWithWorldAndArgs GPositionalSlotClearCommand(TEXT("NeuralAI.PositionalSlots.Clear"),
	TEXT("Removes the crawlers NeuralAI.PositionalSlots.Spawn made."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { PositionalSlotTest::Clear(World); }));

static FAutoConsoleCommandWithWorldAndArgs GPositionalSlotDriveCommand(TEXT("NeuralAI.PositionalSlots.DriveTarget"),
	TEXT("NeuralAI.PositionalSlots.DriveTarget <line|zigzag|circle|straight|stop> [speed cm/s] [seconds] [yaw]: moves the player by itself."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PositionalSlotTest::DriveTarget));
