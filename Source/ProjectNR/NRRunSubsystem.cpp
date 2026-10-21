// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRRunSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Components/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NRWeaponPartComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRRun, Log, All);

bool UNRRunSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game and PIE worlds only: an editor world never plays a run.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UNRRunSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	for (TActorIterator<ANRRoom> It(&InWorld); It; ++It)
	{
		// A generated map's rooms register themselves through the map generator.
		if (!It->IsWaitingForGenerator())
		{
			RegisterRoom(*It);
		}
	}

	// No rooms, no run: sandbox and test levels keep playing as they always did, and a
	// generated map starts its run once it has been built.
	if (Rooms.Num() == 0)
	{
		return;
	}
	StartRun();
}

void UNRRunSubsystem::RegisterRoom(ANRRoom* Room)
{
	if (!Room || Rooms.Contains(Room))
	{
		return;
	}
	Rooms.Add(Room);
	Room->OnRoomStateChanged.AddUniqueDynamic(this, &UNRRunSubsystem::HandleRoomStateChanged);
}

void UNRRunSubsystem::UnregisterRoom(ANRRoom* Room)
{
	if (!Room)
	{
		return;
	}
	Room->OnRoomStateChanged.RemoveDynamic(this, &UNRRunSubsystem::HandleRoomStateChanged);
	Rooms.Remove(Room);
	ClearedRooms.Remove(Room);
	RoomsCleared = ClearedRooms.Num();
}

void UNRRunSubsystem::StartRun()
{
	UWorld* World = GetWorld();
	if (State == ENRRunState::InProgress || !World)
	{
		return;
	}
	if (!AnyDamagedHandle.IsValid())
	{
		AnyDamagedHandle = UHealthComponent::OnAnyDamaged.AddUObject(this, &UNRRunSubsystem::HandleAnyDamaged);
	}
	StartTime = World->GetTimeSeconds();
	ClearedRooms.Reset();
	RoomsCleared = 0;
	Kills = 0;
	Money = 0;
	DamageDealt = 0.0f;
	SetState(ENRRunState::InProgress);
	UE_LOG(LogNRRun, Log, TEXT("Run started: %d room(s)%s"), Rooms.Num(),
		GeneratedSeed != 0 ? *FString::Printf(TEXT(", map seed %d"), GeneratedSeed) : TEXT(""));
}

void UNRRunSubsystem::ResetRun()
{
	for (const TWeakObjectPtr<ANRRoom>& Entry : Rooms)
	{
		if (ANRRoom* Room = Entry.Get())
		{
			Room->OnRoomStateChanged.RemoveDynamic(this, &UNRRunSubsystem::HandleRoomStateChanged);
		}
	}
	Rooms.Reset();
	ClearedRooms.Reset();
	RoomsCleared = 0;
	Kills = 0;
	Money = 0;
	DamageDealt = 0.0f;
	SetState(ENRRunState::NoRun);
}

void UNRRunSubsystem::AddMoney(int32 Amount)
{
	Money = FMath::Max(Money + Amount, 0);
	UE_LOG(LogNRRun, Log, TEXT("Gold %+d: %d"), Amount, Money);
}

void UNRRunSubsystem::SetManagesSafePhase(bool bManage)
{
	bManagesSafePhase = bManage;
	bSafePhase = false;
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (UNRWeaponPartComponent* Parts = Player ? Player->FindComponentByClass<UNRWeaponPartComponent>() : nullptr)
	{
		Parts->SetPrototypeAllowLoadoutEditing(!bManage);
		Parts->SetInSafePhase(false);
	}
	UpdateSafePhase();
}

void UNRRunSubsystem::UpdateSafePhase()
{
	if (!bManagesSafePhase)
	{
		return;
	}

	// Safe only inside a Start or Safe room, and only while no room anywhere is fighting -
	// a room woken by a long shot counts too. A corridor is never safe.
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	bool bSafe = State == ENRRunState::InProgress && Player != nullptr;
	const ANRRoom* PlayerRoom = nullptr;
	for (const TWeakObjectPtr<ANRRoom>& Entry : Rooms)
	{
		const ANRRoom* Room = Entry.Get();
		if (!Room)
		{
			continue;
		}
		bSafe &= Room->GetRoomState() != ENRRoomState::Active;
		if (Player && !PlayerRoom && Room->ContainsLocation(Player->GetActorLocation()))
		{
			PlayerRoom = Room;
		}
	}
	bSafe &= PlayerRoom && (PlayerRoom->GetRoomType() == ENRGridRoomType::Start || PlayerRoom->GetRoomType() == ENRGridRoomType::Safe);

	if (bSafe != bSafePhase)
	{
		bSafePhase = bSafe;
		UE_LOG(LogNRRun, Log, TEXT("Safe phase %s (%s)"), bSafe ? TEXT("on") : TEXT("off"),
			PlayerRoom ? *PlayerRoom->GetLabel() : TEXT("outside any room"));
	}
	if (UNRWeaponPartComponent* Parts = Player ? Player->FindComponentByClass<UNRWeaponPartComponent>() : nullptr)
	{
		if (Parts->IsInSafePhase() != bSafe)
		{
			Parts->SetInSafePhase(bSafe);
		}
	}
}

void UNRRunSubsystem::Deinitialize()
{
	UHealthComponent::OnAnyDamaged.Remove(AnyDamagedHandle);
	AnyDamagedHandle.Reset();
	Super::Deinitialize();
}

TStatId UNRRunSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNRRunSubsystem, STATGROUP_Tickables);
}

void UNRRunSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (State != ENRRunState::InProgress)
	{
		return;
	}

	// Death ends the run. Polled: the pawn arrives after begin play and can be replaced.
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const UHealthComponent* Health = Player ? Player->FindComponentByClass<UHealthComponent>() : nullptr;
	if (Health && Health->IsDead())
	{
		EndRun(false);
	}
	UpdateSafePhase();
}

float UNRRunSubsystem::GetElapsedTime() const
{
	const UWorld* World = GetWorld();
	if (State == ENRRunState::NoRun || !World)
	{
		return 0.0f;
	}
	const double Now = State == ENRRunState::InProgress ? World->GetTimeSeconds() : EndTime;
	return static_cast<float>(Now - StartTime);
}

float UNRRunSubsystem::GetTimeSinceEnded() const
{
	const UWorld* World = GetWorld();
	return (State == ENRRunState::Won || State == ENRRunState::Lost) && World
		? static_cast<float>(World->GetTimeSeconds() - EndTime)
		: -1.0f;
}

void UNRRunSubsystem::HandleRoomStateChanged(ANRRoom* Room, ENRRoomState NewState)
{
	if (State != ENRRunState::InProgress || NewState != ENRRoomState::Cleared)
	{
		return;
	}

	bool bAlreadyCleared = false;
	ClearedRooms.Add(Room, &bAlreadyCleared);
	if (bAlreadyCleared)
	{
		return;
	}
	RoomsCleared = ClearedRooms.Num();
	UE_LOG(LogNRRun, Log, TEXT("Room cleared: %s (%d/%d)"), Room ? *Room->GetLabel() : TEXT("?"), RoomsCleared, Rooms.Num());

	const bool bAnyFinal = Rooms.ContainsByPredicate([](const TWeakObjectPtr<ANRRoom>& Entry)
	{
		return Entry.IsValid() && Entry->bFinalRoom;
	});
	if ((Room && Room->bFinalRoom) || (!bAnyFinal && RoomsCleared >= Rooms.Num()))
	{
		EndRun(true);
	}
}

void UNRRunSubsystem::HandleAnyDamaged(UHealthComponent* Health, float Damage)
{
	if (State != ENRRunState::InProgress || !Health || Health->GetWorld() != GetWorld())
	{
		return;
	}

	// Damage to enemies only; what the player takes is not "dealt".
	const AActor* Owner = Health->GetOwner();
	if (!Owner || !Owner->IsA<AEnemyCharacter>())
	{
		return;
	}

	DamageDealt += Damage;
	// OnAnyDamaged fires before the dead flag latches: no health left means this was the kill.
	if (Health->GetCurrentHealth() <= 0.0f)
	{
		++Kills;
	}
}

void UNRRunSubsystem::EndRun(bool bWon)
{
	if (State != ENRRunState::InProgress)
	{
		return;
	}

	EndTime = GetWorld() ? GetWorld()->GetTimeSeconds() : StartTime;

	// A lost run hands the player nothing more to do: the pawn's input goes off (move, look,
	// fire, roll, aim all bind there), while the HUD keeps its own input for the restart key.
	// A won run leaves the player free to walk around until they restart.
	if (!bWon)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
		if (APawn* Player = PC ? PC->GetPawn() : nullptr)
		{
			Player->DisableInput(PC);
		}
	}

	SetState(bWon ? ENRRunState::Won : ENRRunState::Lost);
	UE_LOG(LogNRRun, Log, TEXT("Run %s after %.1fs: %d/%d rooms, %d kills, %.0f damage"),
		bWon ? TEXT("WON") : TEXT("LOST"), GetElapsedTime(), RoomsCleared, Rooms.Num(), Kills, DamageDealt);
}

void UNRRunSubsystem::RestartRun(bool bSameSeed)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Reloading the level resets every room, enemy and the player; a fresh subsystem starts
	// the new run on its begin play. A generated map reads the seed (and its shape and
	// environment, when they were chosen) back from the options.
	TArray<FString> Parts;
	if (bSameSeed && GeneratedSeed != 0)
	{
		Parts.Add(FString::Printf(TEXT("GridSeed=%d"), GeneratedSeed));
	}
	if (!GeneratedMapOptions.IsEmpty())
	{
		Parts.Add(GeneratedMapOptions);
	}
	const FString Options = FString::Join(Parts, TEXT("?"));
	UE_LOG(LogNRRun, Log, TEXT("Restarting run%s"), Options.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Options));
	// Full package path without the PIE prefix, so a level name shared by two folders
	// cannot load the wrong one.
	UGameplayStatics::OpenLevel(World, FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())), true, Options);
}

void UNRRunSubsystem::SetState(ENRRunState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
	OnRunStateChanged.Broadcast(NewState);
}

// --- Console -----------------------------------------------------------------------

namespace NRRunCommands
{
	UNRRunSubsystem* Get(UWorld* World)
	{
		UNRRunSubsystem* Run = World ? World->GetSubsystem<UNRRunSubsystem>() : nullptr;
		if (!Run)
		{
			UE_LOG(LogNRRun, Warning, TEXT("No run subsystem in this world (run commands need PIE or a game)"));
		}
		return Run;
	}

	void Status(const TArray<FString>& Args, UWorld* World)
	{
		if (const UNRRunSubsystem* Run = Get(World))
		{
			UE_LOG(LogNRRun, Log, TEXT("Run %s: %.1fs, %d/%d rooms, %d kills, %.0f damage"),
				*UEnum::GetValueAsString(Run->GetRunState()), Run->GetElapsedTime(), Run->GetRoomsCleared(),
				Run->GetTotalRooms(), Run->GetKills(), Run->GetDamageDealt());
		}
	}

	void Win(const TArray<FString>& Args, UWorld* World)
	{
		if (UNRRunSubsystem* Run = Get(World))
		{
			Run->EndRun(true);
		}
	}

	void Lose(const TArray<FString>& Args, UWorld* World)
	{
		if (UNRRunSubsystem* Run = Get(World))
		{
			Run->EndRun(false);
		}
	}

	/** NR.Run.Restart [same]: "same" rebuilds a generated map from the same seed. */
	void Restart(const TArray<FString>& Args, UWorld* World)
	{
		if (UNRRunSubsystem* Run = Get(World))
		{
			Run->RestartRun(Args.Num() > 0 && Args[0].Equals(TEXT("same"), ESearchCase::IgnoreCase));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRRunStatusCommand(TEXT("NR.Run.Status"),
	TEXT("Log the run's state, time, rooms, kills and damage."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRRunCommands::Status));

static FAutoConsoleCommandWithWorldAndArgs GNRRunWinCommand(TEXT("NR.Run.Win"),
	TEXT("End the run as won."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRRunCommands::Win));

static FAutoConsoleCommandWithWorldAndArgs GNRRunLoseCommand(TEXT("NR.Run.Lose"),
	TEXT("End the run as lost."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRRunCommands::Lose));

static FAutoConsoleCommandWithWorldAndArgs GNRRunRestartCommand(TEXT("NR.Run.Restart"),
	TEXT("NR.Run.Restart [same]: reload the level and start a new run (same: same generated map seed)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRRunCommands::Restart));
