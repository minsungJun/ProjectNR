// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRSpawnSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NRAdaptationSubsystem.h"
#include "NREncounterData.h"
#include "NRRoom.h"
#include "NRSpawnPicker.h"
#include "NRSpawnMarker.h"
#include "NRSpawnPoint.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY(LogNRSpawn);

static TAutoConsoleVariable<int32> CVarSpawnDebug(TEXT("NR.Spawn.Debug"), 0,
	TEXT("1: draw every room's spawn points (green melee, purple ranged, yellow any, red boss) and the places about to spawn."));

bool UNRSpawnSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UNRSpawnSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNRSpawnSubsystem, STATGROUP_Tickables);
}

void UNRSpawnSubsystem::Configure(const UNREncounterData* InData, int32 InSeed)
{
	EncounterData = InData;
	Seed = InSeed;
	Pending.Reset();
}

const UNREncounterData* UNRSpawnSubsystem::GetEncounterData() const
{
	return EncounterData ? EncounterData.Get() : GetDefault<UNREncounterData>();
}

UClass* UNRSpawnSubsystem::ClassFor(const ANRSpawnPoint* Point, ENRSpawnKind PickedKind) const
{
	if (Point->EnemyClassOverride)
	{
		return Point->EnemyClassOverride;
	}
	const UNREncounterData* Data = GetEncounterData();
	switch (PickedKind)
	{
	case ENRSpawnKind::Ranged: return Data->RangedClass;
	case ENRSpawnKind::Boss: return Data->BossClass;
	default: return Data->MeleeClass;
	}
}

FVector UNRSpawnSubsystem::FloorUnder(const ANRSpawnPoint* Point) const
{
	const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (Navigation && Navigation->ProjectPointToNavigation(Point->GetActorLocation(), Projected, FVector(100.0f, 100.0f, 300.0f)))
	{
		return Projected.Location;
	}
	// Off the navmesh: the bottom of the point's capsule.
	return Point->GetActorLocation() - FVector(0.0f, 0.0f, 60.0f);
}

AEnemyCharacter* UNRSpawnSubsystem::SpawnAt(ANRRoom* Room, const ANRSpawnPoint* Point, UClass* Class) const
{
	UWorld* World = GetWorld();
	const AEnemyCharacter* Defaults = Class ? Class->GetDefaultObject<AEnemyCharacter>() : nullptr;
	if (!World || !Defaults)
	{
		return nullptr;
	}
	// Stand it on the floor under the point: its capsule's middle one half-height above it.
	const FVector Location = FloorUnder(Point) + FVector(0.0f, 0.0f, Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.0f);

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
	if (const UNRAdaptationSubsystem* Adaptation = World->GetSubsystem<UNRAdaptationSubsystem>())
	{
		const UNRAdaptationConfig& Config = Adaptation->GetConfig();
		Params.RangedBias = Adaptation->GetRangedBias();
		Params.MaxRangedShift = Config.MaxRangedShift;
		Params.AnyRangedChanceShift = Config.AnyRangedChanceShift;
		Params.ConvertChance = Config.ConvertChance;
	}
	// Same run seed, same room: same enemies in the same places.
	const int32 RoomKey = Room->IsGenerated() ? Room->GetGeneratedRoomId() : static_cast<int32>(GetTypeHash(Room->GetName()));
	FRandomStream Random(HashCombine(static_cast<uint32>(Seed), static_cast<uint32>(RoomKey) * 7919u));
	const TArray<FNRSpawnPicker::FPick> Picks = FNRSpawnPicker::Pick(Candidates, Params, Random);

	FPending& Entry = Pending.AddDefaulted_GetRef();
	Entry.Room = Room;
	Entry.SpawnTime = World->GetTimeSeconds() + Data->TelegraphSeconds;
	int32 Ranged = 0;
	int32 Bosses = 0;
	for (const FNRSpawnPicker::FPick& Pick : Picks)
	{
		ANRSpawnPoint* Point = Points[Pick.Candidate];
		UClass* Class = ClassFor(Point, Pick.Kind);
		if (!Class)
		{
			UE_LOG(LogNRSpawn, Warning, TEXT("%s: no class for a %s point, skipped"), *Room->GetLabel(), *UEnum::GetValueAsString(Pick.Kind));
			continue;
		}
		FPlanned& Planned = Entry.Planned.AddDefaulted_GetRef();
		Planned.Point = Point;
		Planned.Class = Class;
		Planned.Kind = Pick.Kind;
		Ranged += Pick.Kind == ENRSpawnKind::Ranged ? 1 : 0;
		Bosses += Pick.Kind == ENRSpawnKind::Boss ? 1 : 0;
		if (CVarSpawnDebug.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogNRSpawn, Log, TEXT("  %s at %s"), *UEnum::GetValueAsString(Pick.Kind), *Point->GetActorLocation().ToCompactString());
		}

		// The warning on the floor: red for a close-in enemy, purple for a shooter, deep red for the boss.
		if (Data->TelegraphSeconds > 0.0f)
		{
			FActorSpawnParameters MarkerParams;
			MarkerParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			MarkerParams.OverrideLevel = Room->GetLevel();
			if (ANRSpawnMarker* Marker = World->SpawnActor<ANRSpawnMarker>(ANRSpawnMarker::StaticClass(), FloorUnder(Point) + FVector(0.0f, 0.0f, 2.0f), FRotator::ZeroRotator, MarkerParams))
			{
				Marker->Show(Pick.Kind == ENRSpawnKind::Ranged ? FLinearColor(0.6f, 0.15f, 1.0f)
					: Pick.Kind == ENRSpawnKind::Boss ? FLinearColor(1.0f, 0.0f, 0.0f) : FLinearColor(1.0f, 0.25f, 0.1f),
					Data->TelegraphSeconds);
			}
		}
	}
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const bool bPicked = Picks.ContainsByPredicate([Index](const FNRSpawnPicker::FPick& Pick) { return Pick.Candidate == Index; });
		if (!bPicked && Candidates[Index].bUsable && Candidates[Index].Kind != ENRSpawnKind::Boss)
		{
			Entry.Spares.Add(Points[Index]);
		}
	}

	const int32 Unusable = Candidates.FilterByPredicate([](const FNRSpawnPicker::FCandidate& Candidate) { return !Candidate.bUsable; }).Num();
	UE_LOG(LogNRSpawn, Log, TEXT("%s: %d enemy(s) coming on %d of %d point(s) in %.1f s (%d ranged, %d boss, %d point(s) unusable, ranged shift %+.2f)"),
		*Room->GetLabel(), Entry.Planned.Num(), Picks.Num(), Candidates.Num(), Data->TelegraphSeconds, Ranged, Bosses, Unusable, Params.RangedBias);

	if (Data->TelegraphSeconds <= 0.0f)
	{
		Release(Entry);
		Pending.Pop();
	}
	return true;
}

void UNRSpawnSubsystem::Release(FPending& Entry)
{
	ANRRoom* Room = Entry.Room.Get();
	if (!Room)
	{
		return;
	}
	const UNREncounterData* Data = GetEncounterData();
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	// Standing right on a marked place: that enemy comes from a spare point away from the player.
	const float OnTop = 150.0f;

	TSet<const ANRSpawnPoint*> Used;
	for (const FPlanned& Planned : Entry.Planned)
	{
		Used.Add(Planned.Point.Get());
	}
	int32 Spawned = 0;
	int32 Moved = 0;
	for (const FPlanned& Planned : Entry.Planned)
	{
		ANRSpawnPoint* Point = Planned.Point.Get();
		UClass* Class = Planned.Class.Get();
		if (!Point || !Class)
		{
			continue;
		}
		if (Player && FVector::Dist2D(Player->GetActorLocation(), Point->GetActorLocation()) < OnTop)
		{
			ANRSpawnPoint* Spare = nullptr;
			for (const TWeakObjectPtr<ANRSpawnPoint>& Candidate : Entry.Spares)
			{
				ANRSpawnPoint* Other = Candidate.Get();
				if (Other && !Used.Contains(Other)
					&& FVector::Dist2D(Player->GetActorLocation(), Other->GetActorLocation()) >= Data->MinPlayerDistance)
				{
					Spare = Other;
					break;
				}
			}
			if (!Spare)
			{
				UE_LOG(LogNRSpawn, Log, TEXT("%s: the player stands on a marked place and no spare point is free; one enemy fewer"), *Room->GetLabel());
				continue;
			}
			Used.Add(Spare);
			Point = Spare;
			// A stand-in point keeps the planned enemy, not whatever its own override would give.
			++Moved;
		}
		if (AEnemyCharacter* Enemy = SpawnAt(Room, Point, Class))
		{
			Room->AddSpawnedEnemy(Enemy);
			++Spawned;
		}
		else
		{
			UE_LOG(LogNRSpawn, Warning, TEXT("%s: could not spawn %s at %s"), *Room->GetLabel(), *GetNameSafe(Class),
				*Point->GetActorLocation().ToCompactString());
		}
	}
	UE_LOG(LogNRSpawn, Log, TEXT("%s: spawned %d enemy(s)%s"), *Room->GetLabel(), Spawned,
		Moved > 0 ? *FString::Printf(TEXT(", %d moved off the player"), Moved) : TEXT(""));
	Room->FinishSpawning();
}

void UNRSpawnSubsystem::Tick(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	for (int32 Index = Pending.Num() - 1; Index >= 0; --Index)
	{
		if (!Pending[Index].Room.IsValid())
		{
			Pending.RemoveAt(Index);
		}
		else if (Now >= Pending[Index].SpawnTime)
		{
			// Taken out first: spawning may wake things that ask for more.
			FPending Ready = MoveTemp(Pending[Index]);
			Pending.RemoveAt(Index);
			Release(Ready);
		}
	}
	if (CVarSpawnDebug.GetValueOnGameThread() != 0)
	{
		DrawDebug();
	}
}

void UNRSpawnSubsystem::DrawDebug() const
{
	UWorld* World = GetWorld();
	for (const TWeakObjectPtr<ANRRoom>& Entry : ANRRoom::GetAllRooms())
	{
		const ANRRoom* Room = Entry.Get();
		if (!Room || Room->GetWorld() != World)
		{
			continue;
		}
		for (const ANRSpawnPoint* Point : Room->GetSpawnPoints())
		{
			if (!Point)
			{
				continue;
			}
			const FColor Color = Point->Kind == ENRSpawnKind::Ranged ? FColor(150, 60, 220)
				: Point->Kind == ENRSpawnKind::Any ? FColor(240, 200, 40)
				: Point->Kind == ENRSpawnKind::Boss ? FColor(230, 30, 30) : FColor(60, 200, 90);
			DrawDebugSphere(World, Point->GetActorLocation(), 30.0f, 8, Color, false, -1.0f, 0, 2.0f);
			DrawDebugDirectionalArrow(World, Point->GetActorLocation(), Point->GetActorLocation() + Point->GetActorForwardVector() * 80.0f,
				40.0f, Color, false, -1.0f, 0, 2.0f);
		}
	}
	for (const FPending& Entry : Pending)
	{
		for (const FPlanned& Planned : Entry.Planned)
		{
			if (const ANRSpawnPoint* Point = Planned.Point.Get())
			{
				DrawDebugCircle(World, FloorUnder(Point) + FVector(0.0f, 0.0f, 5.0f), 70.0f, 24, FColor::Red, false, -1.0f, 0, 3.0f,
					FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), false);
			}
		}
	}
}

void UNRSpawnSubsystem::LogStatus() const
{
	UE_LOG(LogNRSpawn, Log, TEXT("Spawning: seed %d, %s rules, %d room(s) waiting on a telegraph"), Seed,
		EncounterData ? *GetNameSafe(EncounterData.Get()) : TEXT("default"), Pending.Num());
	for (const TWeakObjectPtr<ANRRoom>& Entry : ANRRoom::GetAllRooms())
	{
		const ANRRoom* Room = Entry.Get();
		if (!Room || Room->GetWorld() != GetWorld() || Room->GetSpawnPoints().Num() == 0)
		{
			continue;
		}
		int32 Kinds[4] = { 0, 0, 0, 0 };
		for (const ANRSpawnPoint* Point : Room->GetSpawnPoints())
		{
			Kinds[Point ? static_cast<int32>(Point->Kind) : 0] += Point ? 1 : 0;
		}
		UE_LOG(LogNRSpawn, Log, TEXT("  %s: %s, %d point(s) (melee %d, ranged %d, any %d, boss %d), %d/%d enemies left%s"),
			*Room->GetLabel(), *UEnum::GetValueAsString(Room->GetRoomState()), Room->GetSpawnPoints().Num(),
			Kinds[0], Kinds[1], Kinds[2], Kinds[3], Room->GetRemainingEnemies(), Room->GetTotalEnemies(),
			Room->IsSpawning() ? TEXT(", spawning") : TEXT(""));
	}
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

static FAutoConsoleCommandWithWorldAndArgs GNRSpawnStatusCommand(TEXT("NR.Spawn.Status"),
	TEXT("Log every room's spawn points and what is waiting to spawn."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		if (const UNRSpawnSubsystem* Spawner = World ? World->GetSubsystem<UNRSpawnSubsystem>() : nullptr)
		{
			Spawner->LogStatus();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GNRSpawnRoomCommand(TEXT("NR.Spawn.Room"),
	TEXT("NR.Spawn.Room <roomId>: wake a generated room now, as if the player had walked in."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (ANRRoom* Room = World && Args.Num() > 0 ? NRSpawnCommands::FindGeneratedRoom(World, FCString::Atoi(*Args[0])) : nullptr)
		{
			Room->Activate();
		}
	}));
