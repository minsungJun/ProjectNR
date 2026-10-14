// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRSpawnSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NREncounterData.h"
#include "NRRoom.h"
#include "NRSpawnPicker.h"
#include "NRSpawnPoint.h"

DEFINE_LOG_CATEGORY(LogNRSpawn);

bool UNRSpawnSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNRSpawnSubsystem::Configure(const UNREncounterData* InData, int32 InSeed)
{
	EncounterData = InData;
	Seed = InSeed;
}

const UNREncounterData* UNRSpawnSubsystem::GetEncounterData() const
{
	return EncounterData ? EncounterData.Get() : GetDefault<UNREncounterData>();
}

UClass* UNRSpawnSubsystem::ClassFor(const ANRSpawnPoint* Point, uint8 PickedKind) const
{
	if (Point->EnemyClassOverride)
	{
		return Point->EnemyClassOverride;
	}
	const UNREncounterData* Data = GetEncounterData();
	switch (static_cast<ENRSpawnKind>(PickedKind))
	{
	case ENRSpawnKind::Ranged: return Data->RangedClass;
	case ENRSpawnKind::Boss: return Data->BossClass;
	default: return Data->MeleeClass;
	}
}

AEnemyCharacter* UNRSpawnSubsystem::SpawnAt(ANRRoom* Room, const ANRSpawnPoint* Point, UClass* Class) const
{
	UWorld* World = GetWorld();
	const AEnemyCharacter* Defaults = Class ? Class->GetDefaultObject<AEnemyCharacter>() : nullptr;
	if (!World || !Defaults)
	{
		return nullptr;
	}
	// Stand it on the floor under the point: its capsule's middle one half-height above the navmesh.
	FVector Location = Point->GetActorLocation();
	const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FNavLocation Projected;
	if (Navigation && Navigation->ProjectPointToNavigation(Location, Projected, FVector(100.0f, 100.0f, 300.0f)))
	{
		Location = Projected.Location + FVector(0.0f, 0.0f, Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.0f);
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	// Into the room's own level, so it goes when the room is unloaded.
	Params.OverrideLevel = Room->GetLevel();
	return World->SpawnActor<AEnemyCharacter>(Class, Location, FRotator(0.0f, Point->GetActorRotation().Yaw, 0.0f), Params);
}

bool UNRSpawnSubsystem::SpawnForRoom(ANRRoom* Room)
{
	UWorld* World = GetWorld();
	if (!World || !Room || Room->GetSpawnPoints().Num() == 0)
	{
		return false;
	}
	const UNREncounterData* Data = GetEncounterData();

	// A fixed order (by position) so the seed alone decides the picks, whatever order the level
	// happened to list its actors in.
	TArray<ANRSpawnPoint*> Points;
	for (ANRSpawnPoint* Point : Room->GetSpawnPoints())
	{
		if (Point)
		{
			Points.Add(Point);
		}
	}
	Points.Sort([](const ANRSpawnPoint& A, const ANRSpawnPoint& B)
	{
		const FVector LA = A.GetActorLocation();
		const FVector LB = B.GetActorLocation();
		return LA.X != LB.X ? LA.X < LB.X : LA.Y < LB.Y;
	});

	const APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
	const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	TArray<FNRSpawnPicker::FCandidate> Candidates;
	for (const ANRSpawnPoint* Point : Points)
	{
		FNRSpawnPicker::FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Location = Point->GetActorLocation();
		Candidate.Kind = Point->Kind;
		Candidate.Weight = Point->Weight;
		Candidate.bRequired = Point->bRequired;
		FNavLocation Projected;
		const bool bOnNav = !Navigation || Navigation->ProjectPointToNavigation(Candidate.Location, Projected, FVector(100.0f, 100.0f, 300.0f));
		const bool bFarFromPlayer = !Player || FVector::Dist2D(Player->GetActorLocation(), Candidate.Location) >= Data->MinPlayerDistance;
		Candidate.bUsable = bOnNav && bFarFromPlayer;
	}

	FNRSpawnPicker::FParams Params;
	Params.Rule = Data->GetRule(Room->GetRoomType());
	Params.bHasBoss = Data->BossClass != nullptr;
	Params.MinSpacing = Data->MinPointSpacing;
	// Same run seed, same room: same enemies in the same places.
	const int32 RoomKey = Room->IsGenerated() ? Room->GetGeneratedRoomId() : static_cast<int32>(GetTypeHash(Room->GetName()));
	FRandomStream Random(HashCombine(static_cast<uint32>(Seed), static_cast<uint32>(RoomKey) * 7919u));
	const TArray<FNRSpawnPicker::FPick> Picks = FNRSpawnPicker::Pick(Candidates, Params, Random);

	int32 Spawned = 0;
	int32 Ranged = 0;
	int32 Bosses = 0;
	for (const FNRSpawnPicker::FPick& Pick : Picks)
	{
		const ANRSpawnPoint* Point = Points[Pick.Candidate];
		UClass* Class = ClassFor(Point, static_cast<uint8>(Pick.Kind));
		if (!Class)
		{
			UE_LOG(LogNRSpawn, Warning, TEXT("%s: no class for a %s point, skipped"), *Room->GetLabel(),
				*UEnum::GetValueAsString(Pick.Kind));
			continue;
		}
		if (AEnemyCharacter* Enemy = SpawnAt(Room, Point, Class))
		{
			Room->AddSpawnedEnemy(Enemy);
			++Spawned;
			Ranged += Pick.Kind == ENRSpawnKind::Ranged ? 1 : 0;
			Bosses += Pick.Kind == ENRSpawnKind::Boss ? 1 : 0;
		}
		else
		{
			UE_LOG(LogNRSpawn, Warning, TEXT("%s: could not spawn %s at %s"), *Room->GetLabel(), *GetNameSafe(Class),
				*Point->GetActorLocation().ToCompactString());
		}
	}
	const int32 Unusable = Candidates.FilterByPredicate([](const FNRSpawnPicker::FCandidate& Candidate) { return !Candidate.bUsable; }).Num();
	UE_LOG(LogNRSpawn, Log, TEXT("%s: spawned %d enemy(s) on %d of %d point(s) (%d ranged, %d boss, %d point(s) unusable)"),
		*Room->GetLabel(), Spawned, Picks.Num(), Candidates.Num(), Ranged, Bosses, Unusable);

	Room->FinishSpawning();
	return true;
}

// --- Console -----------------------------------------------------------------------

namespace NRSpawnCommands
{
	ANRRoom* FindGeneratedRoom(UWorld* World, int32 RoomId)
	{
		for (const TWeakObjectPtr<ANRRoom>& Entry : ANRRoom::GetAllRooms())
		{
			ANRRoom* Room = Entry.Get();
			if (Room && Room->GetWorld() == World && Room->IsGenerated() && Room->GetGeneratedRoomId() == RoomId)
			{
				return Room;
			}
		}
		return nullptr;
	}

	/**
	 * NR.Spawn.AddTestPoints <roomId> [count]: puts spawn points on a grid in a room that has not
	 * woken yet (every 4th Any, every 5th Ranged), for trying spawning before the room levels have
	 * points of their own.
	 */
	void AddTestPoints(const TArray<FString>& Args, UWorld* World)
	{
		ANRRoom* Room = World && Args.Num() > 0 ? FindGeneratedRoom(World, FCString::Atoi(*Args[0])) : nullptr;
		if (!Room || Room->GetRoomState() != ENRRoomState::Dormant)
		{
			UE_LOG(LogNRSpawn, Warning, TEXT("NR.Spawn.AddTestPoints: no such room still waiting"));
			return;
		}
		const int32 Wanted = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 40) : 10;
		const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const FVector Center = Room->GetActorLocation();
		int32 Added = 0;
		for (int32 Ring = 1; Ring <= 6 && Added < Wanted; ++Ring)
		{
			const int32 Around = Ring * 6;
			for (int32 Step = 0; Step < Around && Added < Wanted; ++Step)
			{
				const float Angle = 2.0f * PI * Step / Around;
				const FVector Guess = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * (Ring * 350.0f);
				FNavLocation Projected;
				if (!Room->ContainsLocation(Guess, 300.0f)
					|| (Navigation && !Navigation->ProjectPointToNavigation(Guess, Projected, FVector(100.0f, 100.0f, 500.0f))))
				{
					continue;
				}
				FActorSpawnParameters Params;
				Params.OverrideLevel = Room->GetLevel();
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				const FVector Location = (Navigation ? Projected.Location : Guess) + FVector(0.0f, 0.0f, 65.0f);
				if (ANRSpawnPoint* Point = World->SpawnActor<ANRSpawnPoint>(ANRSpawnPoint::StaticClass(), Location, (Center - Location).Rotation(), Params))
				{
					Point->Kind = Added % 5 == 4 ? ENRSpawnKind::Ranged : Added % 4 == 3 ? ENRSpawnKind::Any : ENRSpawnKind::Melee;
					Room->AddSpawnPoint(Point);
					++Added;
				}
			}
		}
		UE_LOG(LogNRSpawn, Log, TEXT("NR.Spawn.AddTestPoints: %d point(s) in %s"), Added, *Room->GetLabel());
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRSpawnAddTestPointsCommand(TEXT("NR.Spawn.AddTestPoints"),
	TEXT("NR.Spawn.AddTestPoints <roomId> [count]: put spawn points in a room that has not woken yet."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRSpawnCommands::AddTestPoints));
