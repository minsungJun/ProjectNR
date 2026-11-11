// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRRoom.h"

#include "Characters/EnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/EnemySensorComponent.h"
#include "Components/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NRAdaptationSubsystem.h"
#include "NRDoor.h"
#include "NRSpawnPoint.h"
#include "NRSpawnSubsystem.h"
#include "NRWeaponPartComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRRoom, Log, All);

TArray<TWeakObjectPtr<ANRRoom>> ANRRoom::AllRooms;

ANRRoom::ANRRoom()
{
	// Polls a handful of things ten times a second: where the player is, who is still alive.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	SetRootComponent(Bounds);
	Bounds->SetBoxExtent(FVector(1000.0f, 1000.0f, 300.0f));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetGenerateOverlapEvents(false);
	Bounds->SetHiddenInGame(true);
	Bounds->ShapeColor = FColor(255, 170, 0);
}

void ANRRoom::BeginPlay()
{
	Super::BeginPlay();

	AllRooms.RemoveAll([](const TWeakObjectPtr<ANRRoom>& Entry) { return !Entry.IsValid(); });
	AllRooms.AddUnique(this);

	if (bWaitForGenerator)
	{
		// Loaded as part of a generated map: everything waits for InitializeGenerated.
		bGateOpen = false;
		SetActorTickEnabled(false);
		UE_LOG(LogNRRoom, Log, TEXT("%s: waiting for the map generator"), *GetNameSafe(this));
		return;
	}

	CollectEnemies();
	SetEnemiesDormant(true);

	UE_LOG(LogNRRoom, Log, TEXT("%s: %d enemy(s), %d spawn point(s), %d door(s)"), *GetNameSafe(this), RoomEnemies.Num(),
		SpawnPoints.Num(), Doors.Num());
}

void ANRRoom::InitializeGenerated(int32 InRoomId, ENRGridRoomType InRoomType, bool bInFinalRoom,
	const TArray<ANRDoor*>& InDoors, const TArray<AActor*>& InEnemies, const TArray<ANRSpawnPoint*>& InSpawnPoints)
{
	GeneratedRoomId = InRoomId;
	RoomType = InRoomType;
	bFinalRoom = bInFinalRoom;

	Doors.Reset();
	for (ANRDoor* Door : InDoors)
	{
		if (Door)
		{
			Doors.Add(Door);
			Door->Open();
		}
	}
	RoomEnemies.Reset();
	for (AActor* Enemy : InEnemies)
	{
		if (Enemy)
		{
			RoomEnemies.AddUnique(Enemy);
		}
	}
	SetEnemiesDormant(true);
	SpawnPoints.Reset();
	for (ANRSpawnPoint* Point : InSpawnPoints)
	{
		if (Point)
		{
			SpawnPoints.AddUnique(Point);
		}
	}

	UE_LOG(LogNRRoom, Log, TEXT("%s%s set up: %d enemy(s), %d spawn point(s), %d door(s)"), *GetLabel(),
		bFinalRoom ? TEXT(", final") : TEXT(""), RoomEnemies.Num(), SpawnPoints.Num(), Doors.Num());
}

void ANRRoom::AddSpawnPoint(ANRSpawnPoint* Point)
{
	if (Point && State == ENRRoomState::Dormant)
	{
		SpawnPoints.AddUnique(Point);
	}
}

void ANRRoom::AddSpawnedEnemy(AActor* Enemy)
{
	if (Enemy)
	{
		RoomEnemies.AddUnique(Enemy);
		if (UEnemySensorComponent* Sensor = Enemy->FindComponentByClass<UEnemySensorComponent>())
		{
			Sensor->SetDormant(false);
		}
		// What the enemies learned in the rooms before this one.
		if (UNRAdaptationSubsystem* Adaptation = GetWorld() ? GetWorld()->GetSubsystem<UNRAdaptationSubsystem>() : nullptr)
		{
			Adaptation->ApplyToEnemy(Enemy);
		}
	}
}

FString ANRRoom::GetLabel() const
{
	if (!IsGenerated())
	{
		return GetName();
	}
	return FString::Printf(TEXT("Room %d (%s)"), GeneratedRoomId,
		*StaticEnum<ENRGridRoomType>()->GetNameStringByValue(static_cast<int64>(RoomType)));
}

void ANRRoom::OpenGate()
{
	bGateOpen = true;
	SetActorTickEnabled(State != ENRRoomState::Cleared);
}

void ANRRoom::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AllRooms.Remove(this);
	Super::EndPlay(EndPlayReason);
}

void ANRRoom::CollectEnemies()
{
	RoomEnemies.Reset();
	for (AActor* Enemy : ExtraEnemies)
	{
		if (Enemy)
		{
			RoomEnemies.AddUnique(Enemy);
		}
	}

	if (bCollectEnemiesInBounds)
	{
		for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
		{
			if (ContainsLocation(It->GetActorLocation()))
			{
				RoomEnemies.AddUnique(*It);
			}
		}
		SpawnPoints.Reset();
		for (TActorIterator<ANRSpawnPoint> It(GetWorld()); It; ++It)
		{
			if (ContainsLocation(It->GetActorLocation()))
			{
				SpawnPoints.AddUnique(*It);
			}
		}
	}
}

void ANRRoom::SetEnemiesDormant(bool bDormant) const
{
	for (AActor* Enemy : RoomEnemies)
	{
		if (UEnemySensorComponent* Sensor = Enemy ? Enemy->FindComponentByClass<UEnemySensorComponent>() : nullptr)
		{
			Sensor->SetDormant(bDormant);
		}
	}
}

int32 ANRRoom::GetRemainingEnemies() const
{
	int32 Alive = 0;
	for (const AActor* Enemy : RoomEnemies)
	{
		const UHealthComponent* Health = IsValid(Enemy) ? Enemy->FindComponentByClass<UHealthComponent>() : nullptr;
		Alive += (Health && !Health->IsDead()) ? 1 : 0;
	}
	return Alive;
}

bool ANRRoom::IsAnyEnemyHurt() const
{
	for (const AActor* Enemy : RoomEnemies)
	{
		const UHealthComponent* Health = IsValid(Enemy) ? Enemy->FindComponentByClass<UHealthComponent>() : nullptr;
		if (Health && Health->GetCurrentHealth() < Health->GetMaxHealth())
		{
			return true;
		}
	}
	return false;
}

float ANRRoom::GetTimeSinceCleared() const
{
	const UWorld* World = GetWorld();
	return State == ENRRoomState::Cleared && World ? World->GetTimeSeconds() - ClearedTime : -1.0f;
}

bool ANRRoom::ContainsLocation(const FVector& Location, float Inset) const
{
	const FVector Local = Bounds->GetComponentTransform().InverseTransformPositionNoScale(Location);
	const FVector Extent = Bounds->GetScaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X - Inset
		&& FMath::Abs(Local.Y) <= Extent.Y - Inset
		&& FMath::Abs(Local.Z) <= Extent.Z;
}

ANRRoom* ANRRoom::FindRoomAt(const UWorld* World, const FVector& Location)
{
	for (const TWeakObjectPtr<ANRRoom>& Entry : AllRooms)
	{
		ANRRoom* Room = Entry.Get();
		if (Room && Room->GetWorld() == World && Room->ContainsLocation(Location))
		{
			return Room;
		}
	}
	return nullptr;
}

void ANRRoom::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bGateOpen)
	{
		return;
	}

	switch (State)
	{
	case ENRRoomState::Dormant:
	{
		const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
		if ((Player && ContainsLocation(Player->GetActorLocation(), EntryInset)) || IsAnyEnemyHurt())
		{
			Activate();
		}
		break;
	}
	case ENRRoomState::Active:
		if (GetRemainingEnemies() == 0 && !bSpawnPending)
		{
			SetState(ENRRoomState::Cleared);
		}
		break;
	case ENRRoomState::Cleared:
		// Nothing left to watch.
		SetActorTickEnabled(false);
		break;
	}
}

void ANRRoom::Activate()
{
	if (State != ENRRoomState::Dormant || !bGateOpen)
	{
		return;
	}

	// An empty room is a reward or safe room: nothing to fight, nothing to lock.
	if (GetRemainingEnemies() == 0 && SpawnPoints.Num() == 0)
	{
		SetState(ENRRoomState::Cleared);
		return;
	}

	// Its enemies come now: the doors close first, and the room waits for them before it may clear.
	UNRSpawnSubsystem* Spawner = SpawnPoints.Num() > 0 && GetWorld() ? GetWorld()->GetSubsystem<UNRSpawnSubsystem>() : nullptr;
	bSpawnPending = Spawner != nullptr;
	SetState(ENRRoomState::Active);
	if (Spawner && !Spawner->SpawnForRoom(this))
	{
		bSpawnPending = false;
	}
}

void ANRRoom::SetState(ENRRoomState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;

	if (NewState == ENRRoomState::Active)
	{
		for (ANRDoor* Door : Doors)
		{
			if (Door)
			{
				Door->Close();
			}
		}
		SetEnemiesDormant(false);
		if (!IsGenerated())
		{
			UpdateSafePhase(false);
		}
		if (UNRAdaptationSubsystem* Adaptation = GetWorld() ? GetWorld()->GetSubsystem<UNRAdaptationSubsystem>() : nullptr)
		{
			Adaptation->HandleRoomActivated(this);
		}
	}
	else if (NewState == ENRRoomState::Cleared)
	{
		ClearedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		for (ANRDoor* Door : Doors)
		{
			if (Door)
			{
				Door->Open();
			}
		}
		// A generated run decides the safe phase in one place, from where the player is.
		if (!IsGenerated())
		{
			UpdateSafePhase(true);
		}
		if (UNRAdaptationSubsystem* Adaptation = GetWorld() ? GetWorld()->GetSubsystem<UNRAdaptationSubsystem>() : nullptr)
		{
			Adaptation->HandleRoomCleared(this);
		}
	}

	UE_LOG(LogNRRoom, Log, TEXT("%s -> %s (%d/%d enemies left)"), *GetLabel(),
		*UEnum::GetValueAsString(NewState), GetRemainingEnemies(), RoomEnemies.Num());
	OnRoomStateChanged.Broadcast(this, NewState);
}

void ANRRoom::UpdateSafePhase(bool bSafe) const
{
	// Hook for the loadout rule: parts change only in a safe moment. While the prototype
	// flag on the part component allows editing anywhere, this has no visible effect.
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (UNRWeaponPartComponent* Parts = Player ? Player->FindComponentByClass<UNRWeaponPartComponent>() : nullptr)
	{
		Parts->SetInSafePhase(bSafe);
	}
}

void ANRRoom::KillAllEnemies()
{
	for (AActor* Enemy : RoomEnemies)
	{
		UHealthComponent* Health = IsValid(Enemy) ? Enemy->FindComponentByClass<UHealthComponent>() : nullptr;
		if (Health && !Health->IsDead())
		{
			// A dodge in progress must not save an enemy from the room being cleared.
			Health->ClearInvulnerability();
			Health->ApplyDamage(Health->GetCurrentHealth() + Health->GetCurrentShield(), nullptr, FVector::ForwardVector);
		}
	}
}

// --- Console -----------------------------------------------------------------------

namespace NRRoomCommands
{
	void List(const TArray<FString>& Args, UWorld* World)
	{
		const APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		const ANRRoom* Current = Player ? ANRRoom::FindRoomAt(World, Player->GetActorLocation()) : nullptr;
		for (const TWeakObjectPtr<ANRRoom>& Entry : ANRRoom::GetAllRooms())
		{
			const ANRRoom* Room = Entry.Get();
			if (Room && Room->GetWorld() == World)
			{
				UE_LOG(LogNRRoom, Log, TEXT("  %s%s: %s, %d/%d enemies left"), *Room->GetLabel(),
					Room == Current ? TEXT(" (player here)") : TEXT(""),
					*UEnum::GetValueAsString(Room->GetRoomState()), Room->GetRemainingEnemies(), Room->GetTotalEnemies());
			}
		}
	}

	void KillAll(const TArray<FString>& Args, UWorld* World)
	{
		const APawn* Player = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		if (ANRRoom* Room = Player ? ANRRoom::FindRoomAt(World, Player->GetActorLocation()) : nullptr)
		{
			Room->KillAllEnemies();
			UE_LOG(LogNRRoom, Log, TEXT("Killed the enemies of %s"), *Room->GetLabel());
		}
		else
		{
			UE_LOG(LogNRRoom, Warning, TEXT("The player is not inside any room"));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRRoomListCommand(TEXT("NR.Room.List"),
	TEXT("Log every room's state and remaining enemies."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRRoomCommands::List));

static FAutoConsoleCommandWithWorldAndArgs GNRRoomKillAllCommand(TEXT("NR.Room.KillAll"),
	TEXT("Kill every enemy of the room the player is standing in (test the clear -> door flow)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRRoomCommands::KillAll));
