// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRMapGenerator.h"

#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"
#include "DrawDebugHelpers.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "MoverComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NRGridLayoutGenerator.h"
#include "Characters/EnemyCharacter.h"
#include "Components/HealthComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "NRDoor.h"
#include "NRGridTemplateData.h"
#include "NRRoom.h"
#include "NRRunSubsystem.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRMapGen, Log, All);

namespace NRMapGen
{
	FName PortTag(const TCHAR* Letter) { return FName(*FString::Printf(TEXT("NRPort.%s"), Letter)); }
	FName BlockTag(const TCHAR* Letter) { return FName(*FString::Printf(TEXT("NRBlock.%s"), Letter)); }

	bool IsModuleTag(const FName& Tag)
	{
		const FString Text = Tag.ToString();
		return Text.StartsWith(TEXT("NRPort.")) || Text.StartsWith(TEXT("NRBlock."));
	}

	/** The template direction that ends up facing World once turned by Steps. */
	ENRGridDirection WorldToLocal(ENRGridDirection World, int32 Steps)
	{
		return static_cast<ENRGridDirection>(((static_cast<int32>(World) + Steps) % 4 + 4) % 4);
	}

	const TCHAR* StateName(ENRMapGenState State)
	{
		switch (State)
		{
		case ENRMapGenState::Idle: return TEXT("Idle");
		case ENRMapGenState::Generating: return TEXT("Generating");
		case ENRMapGenState::Loading: return TEXT("Loading");
		case ENRMapGenState::Initializing: return TEXT("Initializing");
		case ENRMapGenState::Ready: return TEXT("Ready");
		case ENRMapGenState::Failed:
		default: return TEXT("Failed");
		}
	}

	/** World directions each room is joined in, by room id. */
	TMap<int32, uint8> OpenMasks(const FNRGeneratedLayout& Layout)
	{
		TMap<int32, uint8> Masks;
		for (const FNRGeneratedConnection& Connection : Layout.Connections)
		{
			const FNRGeneratedRoom* A = Layout.FindRoom(Connection.RoomA);
			const FNRGeneratedRoom* B = Layout.FindRoom(Connection.RoomB);
			ENRGridDirection Direction;
			if (A && B && NRGrid::DirectionBetween(A->Cell, B->Cell, Direction))
			{
				Masks.FindOrAdd(A->RoomId) |= 1 << static_cast<int32>(Direction);
				Masks.FindOrAdd(B->RoomId) |= 1 << static_cast<int32>(NRGrid::Opposite(Direction));
			}
		}
		return Masks;
	}
}

ANRMapGenerator::ANRMapGenerator()
{
	PrimaryActorTick.bCanEverTick = false;

	RoomLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Levels/GridRun/Modules/Lvl_GR_Room_Basic.Lvl_GR_Room_Basic")));
	CorridorLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Levels/GridRun/Modules/Lvl_GR_Corridor_Straight.Lvl_GR_Corridor_Straight")));

	// Stage A test: the same room twice, the second turned a quarter, joined by one corridor.
	// Turned by 90 degrees, the second room meets the corridor with its own north doorway.
	FNRGeneratedRoom Start;
	Start.RoomId = 0;
	Start.Cell = FIntPoint(0, 0);
	Start.RoomType = ENRGridRoomType::Start;
	FixedLayout.Rooms.Add(Start);

	FNRGeneratedRoom Second;
	Second.RoomId = 1;
	Second.Cell = FIntPoint(1, 0);
	Second.RotationSteps = 1;
	FixedLayout.Rooms.Add(Second);

	FNRGeneratedConnection Link;
	Link.RoomA = 0;
	Link.RoomB = 1;
	FixedLayout.Connections.Add(Link);
	FixedLayout.StartRoomId = 0;
}

void ANRMapGenerator::BeginPlay()
{
	Super::BeginPlay();
	if (bStartOnBeginPlay)
	{
		// A same-seed restart passes the seed as a level option.
		int32 UseSeed = Seed;
		const AGameModeBase* GameMode = GetWorld()->GetAuthGameMode();
		const FString Option = GameMode ? UGameplayStatics::ParseOption(GameMode->OptionsString, TEXT("GridSeed")) : FString();
		if (!Option.IsEmpty())
		{
			UseSeed = FCString::Atoi(*Option);
		}
		StartMap(UseSeed);
	}
}

void ANRMapGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(TimeoutTimer);
	GetWorldTimerManager().ClearTimer(NavigationTimer);
	for (ULevelStreamingDynamic* Streaming : Streamings)
	{
		if (Streaming)
		{
			Streaming->OnLevelShown.RemoveAll(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// --- Layout ----------------------------------------------------------------------------

const FNRMapGenSettings& ANRMapGenerator::GetSettings() const
{
	return Config ? Config->Settings : InlineSettings;
}

FVector ANRMapGenerator::GetCellCenter(const FIntPoint& Cell) const
{
	const float Pitch = RoomSize + GetCorridorLength();
	return GetActorLocation() + FVector(Cell.X * Pitch, Cell.Y * Pitch, 0.0f);
}

FTransform ANRMapGenerator::GetRoomTransform(const FNRGeneratedRoom& Room) const
{
	return FTransform(FRotator(0.0f, 90.0f * Room.RotationSteps, 0.0f), GetCellCenter(Room.Cell));
}

FTransform ANRMapGenerator::GetCorridorTransform(const FNRGeneratedRoom& A, const FNRGeneratedRoom& B) const
{
	// Halfway between the two cells, running along the link: the corridor template runs along
	// its local X, so a north-south link is turned a quarter.
	const bool bAlongY = A.Cell.X == B.Cell.X;
	return FTransform(FRotator(0.0f, bAlongY ? 90.0f : 0.0f, 0.0f), (GetCellCenter(A.Cell) + GetCellCenter(B.Cell)) * 0.5f);
}

TArray<FNRRoomTemplateDesc> ANRMapGenerator::DescribeTemplates() const
{
	TArray<FNRRoomTemplateDesc> Descs;
	if (!Config)
	{
		return Descs;
	}
	for (const UNRRoomTemplateData* Template : Config->RoomTemplates)
	{
		FNRRoomTemplateDesc& Desc = Descs.AddDefaulted_GetRef();
		if (!Template)
		{
			Desc.Weight = 0.0f;
			continue;
		}
		Desc.PortMask = static_cast<uint8>(Template->PortMask & 0b1111);
		Desc.RotationMask = static_cast<uint8>(Template->AllowedRotations & 0b1111);
		Desc.Weight = Template->Weight;
		Desc.TypeMask = 0;
		for (ENRGridRoomType Type : Template->RoomTypes)
		{
			Desc.TypeMask |= 1 << static_cast<int32>(Type);
		}
	}
	return Descs;
}

TSoftObjectPtr<UWorld> ANRMapGenerator::GetRoomLevel(const FNRGeneratedRoom& Room) const
{
	if (Room.RoomId == DebugMissingRoomId)
	{
		return TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Levels/GridRun/Modules/Lvl_GR_DoesNotExist.Lvl_GR_DoesNotExist")));
	}
	if (Config && Config->RoomTemplates.IsValidIndex(Room.TemplateIndex) && Config->RoomTemplates[Room.TemplateIndex])
	{
		return Config->RoomTemplates[Room.TemplateIndex]->Level;
	}
	return RoomLevel;
}

TSoftObjectPtr<UWorld> ANRMapGenerator::GetCorridorLevel() const
{
	return Config && Config->CorridorTemplate ? Config->CorridorTemplate->Level : CorridorLevel;
}

float ANRMapGenerator::GetCorridorLength() const
{
	return Config && Config->CorridorTemplate ? Config->CorridorTemplate->Length : CorridorLength;
}

bool ANRMapGenerator::GenerateLayout(int32 InSeed, FString& OutError, TOptional<ENRGridLayoutShape> ShapeOverride)
{
	FNRMapGenSettings Settings = GetSettings();
	if (ShapeOverride.IsSet())
	{
		Settings.Shape = ShapeOverride.GetValue();
	}

	GeneratedLayout = FNRGeneratedLayout();
	FNRGeneratedLayout Layout;
	if (!FNRGridLayoutGenerator::Generate(Settings, InSeed, Layout, OutError))
	{
		UE_LOG(LogNRMapGen, Error, TEXT("Layout generation failed: %s"), *OutError);
		return false;
	}
	GeneratedLayout = MoveTemp(Layout);

	const UEnum* ShapeEnum = StaticEnum<ENRGridLayoutShape>();
	UE_LOG(LogNRMapGen, Log, TEXT("Layout seed %d (%s, attempt %d, v%d): %d rooms, %d corridors, %d loop(s), %d branch room(s)"),
		InSeed, *ShapeEnum->GetNameStringByValue(static_cast<int64>(Settings.Shape)), GeneratedLayout.Attempt, GeneratedLayout.GeneratorVersion,
		GeneratedLayout.Rooms.Num(), GeneratedLayout.Connections.Num(),
		FNRGridLayoutGenerator::CountLoops(GeneratedLayout), FNRGridLayoutGenerator::CountBranchRooms(GeneratedLayout));
	TArray<FString> Lines;
	FNRGridLayoutGenerator::ToAscii(Settings, GeneratedLayout).ParseIntoArrayLines(Lines, false);
	for (const FString& Line : Lines)
	{
		UE_LOG(LogNRMapGen, Log, TEXT("    %s"), *Line);
	}
	return true;
}

// --- Building ---------------------------------------------------------------------------

bool ANRMapGenerator::StartMap(int32 InSeed)
{
	UnloadLayout();
	State = ENRMapGenState::Generating;
	LastError.Reset();
	HoldPlayer(true);

	if (LayoutSource == ENRMapLayoutSource::Fixed)
	{
		ActiveLayout = FixedLayout;
	}
	else
	{
		const int32 UseSeed = InSeed != 0 ? InSeed : FMath::RandRange(1, MAX_int32 - 1);
		FString Error;
		if (!GenerateLayout(UseSeed, Error))
		{
			ActiveLayout = FNRGeneratedLayout();
			ActiveLayout.Seed = UseSeed;
			Fail(Error);
			return false;
		}

		ActiveLayout = GeneratedLayout;
		const TArray<FNRRoomTemplateDesc> Templates = DescribeTemplates();
		if (Templates.Num() == 0)
		{
			Fail(TEXT("the config has no room templates"));
			return false;
		}
		for (const UNRRoomTemplateData* Template : Config->RoomTemplates)
		{
			if (Template && !FMath::IsNearlyEqual(Template->Size, RoomSize))
			{
				Fail(FString::Printf(TEXT("room template %s is %.0f cm, the map's rooms are %.0f cm"), *GetNameSafe(Template), Template->Size, RoomSize));
				return false;
			}
		}
		if (!FNRGridLayoutGenerator::AssignTemplates(Templates, ActiveLayout, Error))
		{
			Fail(Error);
			return false;
		}
		const TArray<FString> Problems = FNRGridLayoutGenerator::ValidateTemplates(Templates, ActiveLayout);
		if (Problems.Num() > 0)
		{
			Fail(FString::Join(Problems, TEXT("; ")));
			return false;
		}
	}

	FString Error;
	if (!LoadActiveLayout(Error))
	{
		Fail(Error);
		return false;
	}
	return true;
}

ULevelStreamingDynamic* ANRMapGenerator::LoadModule(const TSoftObjectPtr<UWorld>& Level, const FTransform& Transform, const FString& Name)
{
	ULevelStreamingDynamic::FLoadLevelInstanceParams Params(GetWorld(), Level.ToSoftObjectPath().GetLongPackageName(), Transform);
	Params.OptionalLevelNameOverride = &Name;

	bool bSuccess = false;
	ULevelStreamingDynamic* Streaming = ULevelStreamingDynamic::LoadLevelInstance(Params, bSuccess);
	if (!bSuccess || !Streaming)
	{
		return nullptr;
	}

	// Success here only means the request was accepted; HandleModuleShown waits for the level.
	Streaming->bShouldBlockOnLoad = bBlockOnLoad;
	Streaming->OnLevelShown.AddDynamic(this, &ANRMapGenerator::HandleModuleShown);
	return Streaming;
}

bool ANRMapGenerator::LoadActiveLayout(FString& OutError)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		OutError = TEXT("no world");
		return false;
	}
	if (GetCorridorLevel().IsNull())
	{
		OutError = TEXT("no corridor level");
		return false;
	}

	// Every request decided up front; the order they are sent in must not matter.
	struct FRequest
	{
		bool bCorridor;
		int32 Id;
		TSoftObjectPtr<UWorld> Level;
		FTransform Transform;
	};
	TArray<FRequest> Requests;
	for (const FNRGeneratedRoom& Room : ActiveLayout.Rooms)
	{
		const TSoftObjectPtr<UWorld> Level = GetRoomLevel(Room);
		if (Level.IsNull())
		{
			OutError = FString::Printf(TEXT("room %d has no level"), Room.RoomId);
			return false;
		}
		Requests.Add({ false, Room.RoomId, Level, GetRoomTransform(Room) });
	}
	for (int32 Index = 0; Index < ActiveLayout.Connections.Num(); ++Index)
	{
		const FNRGeneratedConnection& Connection = ActiveLayout.Connections[Index];
		const FNRGeneratedRoom* A = ActiveLayout.FindRoom(Connection.RoomA);
		const FNRGeneratedRoom* B = ActiveLayout.FindRoom(Connection.RoomB);
		ENRGridDirection Direction;
		if (!A || !B || !NRGrid::DirectionBetween(A->Cell, B->Cell, Direction))
		{
			OutError = FString::Printf(TEXT("connection %d does not join two neighbouring rooms"), Index);
			return false;
		}
		Requests.Add({ true, Index, GetCorridorLevel(), GetCorridorTransform(*A, *B) });
	}

	// Every level must exist before anything is requested: a missing one would otherwise only
	// show up as a load that never finishes.
	TArray<FString> Missing;
	for (const FRequest& Request : Requests)
	{
		const FString Package = Request.Level.ToSoftObjectPath().GetLongPackageName();
		if (!FPackageName::DoesPackageExist(Package))
		{
			Missing.AddUnique(FString::Printf(TEXT("%s %d: %s"), Request.bCorridor ? TEXT("corridor") : TEXT("room"), Request.Id, *Package));
		}
	}
	if (Missing.Num() > 0)
	{
		OutError = FString::Printf(TEXT("level(s) not found, nothing loaded: %s"), *FString::Join(Missing, TEXT(", ")));
		return false;
	}

	if (bShuffleLoadOrder)
	{
		// Not the layout's stream: the order must be free to differ between runs of one seed.
		for (int32 Index = Requests.Num() - 1; Index > 0; --Index)
		{
			Requests.Swap(Index, FMath::RandRange(0, Index));
		}
	}

	State = ENRMapGenState::Loading;
	LoadStartTime = World->GetTimeSeconds();
	const int32 ThisGeneration = Generation;
	for (const FRequest& Request : Requests)
	{
		const FString Name = FString::Printf(TEXT("GR_G%d_%s%d"), ThisGeneration, Request.bCorridor ? TEXT("Corridor") : TEXT("Room"), Request.Id);
		ULevelStreamingDynamic* Streaming = LoadModule(Request.Level, Request.Transform, Name);
		if (!Streaming)
		{
			OutError = FString::Printf(TEXT("could not load %s %d: %s"), Request.bCorridor ? TEXT("corridor") : TEXT("room"),
				Request.Id, *Request.Level.ToString());
			return false;
		}
		Streamings.Add(Streaming);
		Modules.Add({ Request.bCorridor, Request.Id, Request.Transform, Request.Level.ToString() });
	}

	UE_LOG(LogNRMapGen, Log, TEXT("Generation %d (seed %d): requested %d room(s) and %d corridor(s)%s"),
		ThisGeneration, ActiveLayout.Seed, ActiveLayout.Rooms.Num(), ActiveLayout.Connections.Num(),
		bShuffleLoadOrder ? TEXT(" in shuffled order") : TEXT(""));
	// Checked twice a second: a level that fails to load ends the map at once rather than at the timeout.
	GetWorldTimerManager().SetTimer(TimeoutTimer, this, &ANRMapGenerator::CheckLoadTimeout, 0.5f, true);
	return true;
}

void ANRMapGenerator::UnloadLayout()
{
	++Generation;
	GetWorldTimerManager().ClearTimer(TimeoutTimer);
	GetWorldTimerManager().ClearTimer(NavigationTimer);

	for (ULevelStreamingDynamic* Streaming : Streamings)
	{
		if (Streaming)
		{
			Streaming->OnLevelShown.RemoveAll(this);
			Streaming->SetIsRequestingUnloadAndRemoval(true);
		}
	}
	if (Streamings.Num() > 0)
	{
		UE_LOG(LogNRMapGen, Log, TEXT("Unloading %d module(s)"), Streamings.Num());
	}
	Streamings.Reset();
	Modules.Reset();

	// The run was over these rooms; it goes with them.
	if (GeneratedRooms.Num() > 0)
	{
		if (UNRRunSubsystem* Run = GetWorld() ? GetWorld()->GetSubsystem<UNRRunSubsystem>() : nullptr)
		{
			Run->ResetRun();
			Run->SetManagesSafePhase(false);
		}
		GeneratedRooms.Reset();
	}

	if (State != ENRMapGenState::Failed)
	{
		State = ENRMapGenState::Idle;
	}
}

void ANRMapGenerator::HandleModuleShown()
{
	if (State != ENRMapGenState::Loading)
	{
		return;
	}
	for (const ULevelStreamingDynamic* Streaming : Streamings)
	{
		if (!Streaming || !Streaming->GetLoadedLevel() || !Streaming->IsLevelVisible())
		{
			return;
		}
	}
	GetWorldTimerManager().ClearTimer(TimeoutTimer);
	InitializeModules();
}

void ANRMapGenerator::CheckLoadTimeout()
{
	if (State != ENRMapGenState::Loading)
	{
		return;
	}

	TArray<FString> Failed;
	TArray<FString> Waiting;
	for (int32 Index = 0; Index < Streamings.Num(); ++Index)
	{
		const ULevelStreamingDynamic* Streaming = Streamings[Index];
		if (!Streaming || Streaming->GetLevelStreamingState() == ELevelStreamingState::FailedToLoad)
		{
			Failed.Add(Modules[Index].LevelPath);
		}
		else if (!Streaming->GetLoadedLevel() || !Streaming->IsLevelVisible())
		{
			Waiting.Add(Modules[Index].LevelPath);
		}
	}

	if (Failed.Num() > 0)
	{
		Fail(FString::Printf(TEXT("level(s) failed to load: %s"), *FString::Join(Failed, TEXT(", "))));
	}
	else if (GetWorld()->GetTimeSeconds() - LoadStartTime > LoadTimeout)
	{
		Fail(FString::Printf(TEXT("timed out after %.0f s waiting for: %s"), LoadTimeout, *FString::Join(Waiting, TEXT(", "))));
	}
}

void ANRMapGenerator::Fail(const FString& Reason)
{
	LastError = Reason;
	UE_LOG(LogNRMapGen, Error, TEXT("Map generation failed (seed %d): %s"), ActiveLayout.Seed, *Reason);
	// Nothing half-built is played: every module requested so far goes.
	State = ENRMapGenState::Failed;
	UnloadLayout();
	HoldPlayer(false);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.0f, FColor::Red,
			FString::Printf(TEXT("Map generation failed (seed %d): %s   - NR.Run.Restart [same] or NR.Grid.Start to retry"), ActiveLayout.Seed, *Reason));
	}
	OnMapFailed.Broadcast(this, Reason);
}

void ANRMapGenerator::HoldPlayer(bool bHold)
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || bHold == bPlayerHeld)
	{
		return;
	}
	bPlayerHeld = bHold;
	if (bHold)
	{
		Pawn->DisableInput(PC);
	}
	else
	{
		Pawn->EnableInput(PC);
	}
}

void ANRMapGenerator::InitializeRooms(TArray<FString>& Errors)
{
	const TMap<int32, uint8> Masks = NRMapGen::OpenMasks(ActiveLayout);
	for (const FNRGeneratedRoom& Room : ActiveLayout.Rooms)
	{
		const int32 ModuleIndex = FindRoomModule(Room.RoomId);
		const ULevel* Level = Streamings.IsValidIndex(ModuleIndex) && Streamings[ModuleIndex] ? Streamings[ModuleIndex]->GetLoadedLevel() : nullptr;
		if (!Level)
		{
			continue;
		}

		TArray<ANRRoom*> RoomActors;
		TArray<AActor*> Enemies;
		for (AActor* Actor : Level->Actors)
		{
			if (ANRRoom* RoomActor = Cast<ANRRoom>(Actor))
			{
				RoomActors.Add(RoomActor);
			}
			else if (Cast<AEnemyCharacter>(Actor))
			{
				Enemies.Add(Actor);
			}
		}

		if (RoomActors.Num() != 1)
		{
			// A bare room level (the stage A test) has no room logic; anything else must have one.
			if (LayoutSource == ENRMapLayoutSource::Generated)
			{
				Errors.Add(FString::Printf(TEXT("room %d: level needs exactly one NRRoom, has %d"), Room.RoomId, RoomActors.Num()));
			}
			continue;
		}

		if (Room.RoomId == DebugEmptyRoomId)
		{
			for (AActor* Enemy : Enemies)
			{
				Enemy->Destroy();
			}
			Enemies.Reset();
			DebugEmptyRoomId = INDEX_NONE;
		}

		const bool bFights = Room.RoomType == ENRGridRoomType::Combat || Room.RoomType == ENRGridRoomType::Elite || Room.RoomType == ENRGridRoomType::Boss;
		if (bFights && Enemies.Num() == 0)
		{
			Errors.Add(FString::Printf(TEXT("room %d (%s): fighting room without an enemy"), Room.RoomId,
				*UEnum::GetValueAsString(Room.RoomType)));
			continue;
		}
		if (!bFights && Enemies.Num() > 0)
		{
			Errors.Add(FString::Printf(TEXT("room %d (%s): %d enemy(s) in a room meant to be safe"), Room.RoomId,
				*UEnum::GetValueAsString(Room.RoomType), Enemies.Num()));
			continue;
		}

		// Doors on joined doorways belong to the room; the rest are hidden behind the blockers.
		TArray<ANRDoor*> Doors;
		const uint8 Mask = Masks.FindRef(Room.RoomId);
		for (int32 Local = 0; Local < 4; ++Local)
		{
			const ENRGridDirection LocalDirection = static_cast<ENRGridDirection>(Local);
			const bool bJoined = (Mask & (1 << static_cast<int32>(NRGrid::LocalToWorld(LocalDirection, Room.RotationSteps)))) != 0;
			for (AActor* Actor : FindTagged(ModuleIndex, FName(*FString::Printf(TEXT("NRDoor.%s"), NRGrid::Letter(LocalDirection)))))
			{
				if (ANRDoor* Door = Cast<ANRDoor>(Actor); Door && bJoined)
				{
					Doors.Add(Door);
				}
				else
				{
					Actor->SetActorHiddenInGame(true);
					Actor->SetActorEnableCollision(false);
				}
			}
		}

		RoomActors[0]->InitializeGenerated(Room.RoomId, Room.RoomType, Room.RoomId == ActiveLayout.BossRoomId, Doors, Enemies);
		GeneratedRooms.Add(RoomActors[0]);
	}
}

TArray<AActor*> ANRMapGenerator::FindTagged(int32 ModuleIndex, FName Tag) const
{
	TArray<AActor*> Found;
	const ULevelStreamingDynamic* Streaming = Streamings.IsValidIndex(ModuleIndex) ? Streamings[ModuleIndex].Get() : nullptr;
	const ULevel* Level = Streaming ? Streaming->GetLoadedLevel() : nullptr;
	if (!Level)
	{
		return Found;
	}
	for (AActor* Actor : Level->Actors)
	{
		if (Actor && Actor->ActorHasTag(Tag))
		{
			Found.Add(Actor);
		}
	}
	return Found;
}

int32 ANRMapGenerator::FindRoomModule(int32 RoomId) const
{
	return Modules.IndexOfByPredicate([RoomId](const FModule& Module) { return !Module.bCorridor && Module.Id == RoomId; });
}

int32 ANRMapGenerator::FindCorridorModule(int32 ConnectionIndex) const
{
	return Modules.IndexOfByPredicate([ConnectionIndex](const FModule& Module) { return Module.bCorridor && Module.Id == ConnectionIndex; });
}

void ANRMapGenerator::InitializeModules()
{
	State = ENRMapGenState::Initializing;
	TArray<FString> Errors;
	const TMap<int32, uint8> Masks = NRMapGen::OpenMasks(ActiveLayout);

	// Doorways: open the joined ones, leave the rest walled. Hidden and without collision, so
	// an opened blocker leaves neither a visible panel nor an invisible wall.
	int32 Opened = 0;
	for (const FNRGeneratedRoom& Room : ActiveLayout.Rooms)
	{
		const int32 ModuleIndex = FindRoomModule(Room.RoomId);
		const uint8 Mask = Masks.FindRef(Room.RoomId);
		for (int32 Local = 0; Local < 4; ++Local)
		{
			const ENRGridDirection LocalDirection = static_cast<ENRGridDirection>(Local);
			const ENRGridDirection WorldDirection = NRGrid::LocalToWorld(LocalDirection, Room.RotationSteps);
			const TArray<AActor*> Blockers = FindTagged(ModuleIndex, NRMapGen::BlockTag(NRGrid::Letter(LocalDirection)));
			const bool bJoined = (Mask & (1 << static_cast<int32>(WorldDirection))) != 0;
			if (Blockers.Num() == 0)
			{
				// A doorway the template does not have is fine as long as nothing joins there.
				if (bJoined)
				{
					Errors.Add(FString::Printf(TEXT("room %d: joined to the %s but its level has no %s"), Room.RoomId,
						NRGrid::Letter(WorldDirection), *NRMapGen::BlockTag(NRGrid::Letter(LocalDirection)).ToString()));
				}
				continue;
			}
			if (bJoined)
			{
				for (AActor* Blocker : Blockers)
				{
					Blocker->SetActorHiddenInGame(true);
					Blocker->SetActorEnableCollision(false);
				}
				++Opened;
			}
		}
	}

	// Ports: each connection's room doorways must meet its corridor's ends.
	for (int32 Index = 0; Index < ActiveLayout.Connections.Num(); ++Index)
	{
		const FNRGeneratedConnection& Connection = ActiveLayout.Connections[Index];
		const FNRGeneratedRoom* A = ActiveLayout.FindRoom(Connection.RoomA);
		const FNRGeneratedRoom* B = ActiveLayout.FindRoom(Connection.RoomB);
		ENRGridDirection Direction;
		if (!A || !B || !NRGrid::DirectionBetween(A->Cell, B->Cell, Direction))
		{
			continue;
		}

		const int32 CorridorModule = FindCorridorModule(Index);
		TArray<AActor*> Ends = FindTagged(CorridorModule, NRMapGen::PortTag(TEXT("A")));
		Ends.Append(FindTagged(CorridorModule, NRMapGen::PortTag(TEXT("B"))));
		if (Ends.Num() != 2)
		{
			Errors.Add(FString::Printf(TEXT("corridor %d: level needs exactly one NRPort.A and one NRPort.B"), Index));
			continue;
		}

		auto CheckRoomPort = [&](const FNRGeneratedRoom& Room, ENRGridDirection WorldDirection)
		{
			const ENRGridDirection Local = NRMapGen::WorldToLocal(WorldDirection, Room.RotationSteps);
			const TArray<AActor*> Ports = FindTagged(FindRoomModule(Room.RoomId), NRMapGen::PortTag(NRGrid::Letter(Local)));
			if (Ports.Num() != 1)
			{
				Errors.Add(FString::Printf(TEXT("room %d: level needs exactly one NRPort.%s"), Room.RoomId, NRGrid::Letter(Local)));
				return;
			}

			const AActor* Port = Ports[0];
			const AActor* End = FVector::DistSquared(Port->GetActorLocation(), Ends[0]->GetActorLocation())
				<= FVector::DistSquared(Port->GetActorLocation(), Ends[1]->GetActorLocation()) ? Ends[0] : Ends[1];
			const float Gap = FVector::Dist(Port->GetActorLocation(), End->GetActorLocation());
			const float Facing = FVector::DotProduct(Port->GetActorForwardVector(), End->GetActorForwardVector());
			// Both markers face out of their own module, so meeting doorways face each other.
			if (Gap > PortTolerance || Facing > -0.99f)
			{
				Errors.Add(FString::Printf(TEXT("link %d: room %d doorway %s is %.2f cm from the corridor end (facing %.3f)"),
					Index, Room.RoomId, NRGrid::Letter(WorldDirection), Gap, Facing));
			}
		};
		CheckRoomPort(*A, Direction);
		CheckRoomPort(*B, NRGrid::Opposite(Direction));
	}

	if (Errors.Num() == 0)
	{
		InitializeRooms(Errors);
	}
	if (Errors.Num() > 0)
	{
		Fail(FString::Join(Errors, TEXT("; ")));
		return;
	}

	UE_LOG(LogNRMapGen, Log, TEXT("Generation %d: %d module(s) loaded in %.2f s, %d doorway(s) opened, %d link(s) checked; waiting for navigation"),
		Generation, Streamings.Num(), GetWorld()->GetTimeSeconds() - LoadStartTime, Opened, ActiveLayout.Connections.Num());

	// Navigation has to be rebuilt over the new geometry before anything may walk it.
	NavigationStartTime = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(NavigationTimer, this, &ANRMapGenerator::PollNavigation, 0.25f, true, 0.0f);
}

bool ANRMapGenerator::HasNavigationPath(float* OutLength, bool* OutBuilding) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Navigation = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Navigation)
	{
		return false;
	}

	const FNRGeneratedRoom* From = ActiveLayout.FindRoom(ActiveLayout.StartRoomId);
	const FNRGeneratedRoom* To = ActiveLayout.BossRoomId != INDEX_NONE ? ActiveLayout.FindRoom(ActiveLayout.BossRoomId)
		: (ActiveLayout.Rooms.Num() > 1 ? &ActiveLayout.Rooms[1] : nullptr);
	if (!From || !To)
	{
		return true;
	}

	if (OutBuilding)
	{
		*OutBuilding = Navigation->IsNavigationBuildInProgress();
	}
	const UNavigationPath* Path = Navigation->FindPathToLocationSynchronously(World,
		GetCellCenter(From->Cell) + FVector(0.0f, 0.0f, 50.0f), GetCellCenter(To->Cell) + FVector(0.0f, 0.0f, 50.0f));
	const bool bWhole = Path && Path->IsValid() && !Path->IsPartial();
	if (OutLength)
	{
		*OutLength = bWhole ? Path->GetPathLength() : 0.0f;
	}
	return bWhole;
}

void ANRMapGenerator::PollNavigation()
{
	if (State != ENRMapGenState::Initializing)
	{
		GetWorldTimerManager().ClearTimer(NavigationTimer);
		return;
	}

	bool bBuilding = false;
	if (HasNavigationPath(nullptr, &bBuilding) && !bBuilding)
	{
		GetWorldTimerManager().ClearTimer(NavigationTimer);
		BecomeReady();
		return;
	}
	if (GetWorld()->GetTimeSeconds() - NavigationStartTime > NavigationTimeout)
	{
		GetWorldTimerManager().ClearTimer(NavigationTimer);
		Fail(FString::Printf(TEXT("navigation did not join the start room to the boss room within %.0f s"), NavigationTimeout));
	}
}

void ANRMapGenerator::BecomeReady()
{
	State = ENRMapGenState::Ready;
	float Length = 0.0f;
	HasNavigationPath(&Length);
	UE_LOG(LogNRMapGen, Log, TEXT("Generation %d ready (seed %d): %d room(s), %d corridor(s) in %.2f s; start->boss path %.0f cm"),
		Generation, ActiveLayout.Seed, ActiveLayout.Rooms.Num(), ActiveLayout.Connections.Num(),
		GetWorld()->GetTimeSeconds() - LoadStartTime, Length);
	UE_LOG(LogNRMapGen, Log, TEXT("  signature %08x"), GetTypeHash(BuildSignature()));

	if (bMovePlayerToStartOnReady)
	{
		MovePlayerToStart();
	}

	// The run starts only now, with every room registered, so it can neither be won nor
	// count a clear before the map is whole; the rooms come alive after it.
	UNRRunSubsystem* Run = GetWorld()->GetSubsystem<UNRRunSubsystem>();
	if (bStartRunOnReady && Run && GeneratedRooms.Num() > 0)
	{
		Run->ResetRun();
		Run->SetGeneratedSeed(ActiveLayout.Seed);
		for (const TWeakObjectPtr<ANRRoom>& Room : GeneratedRooms)
		{
			Run->RegisterRoom(Room.Get());
		}
		Run->SetManagesSafePhase(true);
		Run->StartRun();
	}
	for (const TWeakObjectPtr<ANRRoom>& Room : GeneratedRooms)
	{
		if (ANRRoom* RoomActor = Room.Get())
		{
			RoomActor->OpenGate();
		}
	}

	HoldPlayer(false);
	OnMapReady.Broadcast(this);
}

void ANRMapGenerator::MovePlayerToStart() const
{
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const FNRGeneratedRoom* Start = ActiveLayout.FindRoom(ActiveLayout.StartRoomId);
	if (!Pawn || !Start)
	{
		return;
	}

	// The start room's NRSpawn marker if it has one, otherwise its centre.
	FVector Target = GetCellCenter(Start->Cell) + FVector(0.0f, 0.0f, 100.0f);
	const TArray<AActor*> Spawns = FindTagged(FindRoomModule(Start->RoomId), TEXT("NRSpawn"));
	if (Spawns.Num() > 0)
	{
		Target = Spawns[0]->GetActorLocation() + FVector(0.0f, 0.0f, 100.0f);
	}
	if (UMoverComponent* Mover = Pawn->FindComponentByClass<UMoverComponent>())
	{
		// Mover owns the pawn's position; moving the actor directly would be undone next step.
		TSharedPtr<FTeleportEffect> Teleport = MakeShared<FTeleportEffect>();
		Teleport->TargetLocation = Target;
		Teleport->bUseActorRotation = true;
		Mover->QueueInstantMovementEffect(Teleport);
	}
	else
	{
		Pawn->TeleportTo(Target, Pawn->GetActorRotation());
	}
	UE_LOG(LogNRMapGen, Log, TEXT("  player moved to the start room at %s"), *Target.ToCompactString());
}

// --- Debug ------------------------------------------------------------------------------

FString ANRMapGenerator::BuildSignature() const
{
	TArray<FString> Parts;
	const TMap<int32, uint8> Masks = NRMapGen::OpenMasks(ActiveLayout);

	TArray<FNRGeneratedRoom> Rooms = ActiveLayout.Rooms;
	Rooms.Sort([](const FNRGeneratedRoom& A, const FNRGeneratedRoom& B) { return A.RoomId < B.RoomId; });
	for (const FNRGeneratedRoom& Room : Rooms)
	{
		const int32 ModuleIndex = FindRoomModule(Room.RoomId);
		FString Ports;
		for (int32 Local = 0; Local < 4; ++Local)
		{
			for (const AActor* Port : FindTagged(ModuleIndex, NRMapGen::PortTag(NRGrid::Letter(static_cast<ENRGridDirection>(Local)))))
			{
				const FVector Location = Port->GetActorLocation();
				Ports += FString::Printf(TEXT("%s(%.0f,%.0f)"), NRGrid::Letter(static_cast<ENRGridDirection>(Local)), Location.X, Location.Y);
			}
		}
		const FVector Location = ModuleIndex != INDEX_NONE ? Modules[ModuleIndex].Transform.GetLocation() : FVector::ZeroVector;
		Parts.Add(FString::Printf(TEXT("R%d t%d r%d (%.0f,%.0f) open %s %s"), Room.RoomId, Room.TemplateIndex, Room.RotationSteps,
			Location.X, Location.Y, *FNRGridLayoutGenerator::MaskToString(Masks.FindRef(Room.RoomId)), *Ports));
	}
	for (int32 Index = 0; Index < ActiveLayout.Connections.Num(); ++Index)
	{
		const int32 ModuleIndex = FindCorridorModule(Index);
		const FTransform Transform = ModuleIndex != INDEX_NONE ? Modules[ModuleIndex].Transform : FTransform::Identity;
		Parts.Add(FString::Printf(TEXT("C%d %d-%d (%.0f,%.0f) yaw %.0f"), Index, ActiveLayout.Connections[Index].RoomA,
			ActiveLayout.Connections[Index].RoomB, Transform.GetLocation().X, Transform.GetLocation().Y, Transform.Rotator().Yaw));
	}
	return FString::Join(Parts, TEXT("\n"));
}

void ANRMapGenerator::LogStatus() const
{
	UE_LOG(LogNRMapGen, Log, TEXT("%s: %s, generation %d, seed %d, %d module(s)%s"), *GetName(),
		NRMapGen::StateName(State), Generation, ActiveLayout.Seed, Streamings.Num(),
		LastError.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", last error: %s"), *LastError));

	// Each module through its own level: no actor may belong to two modules.
	TSet<const AActor*> Seen;
	int32 Shared = 0;
	int32 Loaded = 0;
	for (const ULevelStreamingDynamic* Streaming : Streamings)
	{
		const ULevel* Level = Streaming ? Streaming->GetLoadedLevel() : nullptr;
		Loaded += Level && Streaming->IsLevelVisible() ? 1 : 0;
		if (Level)
		{
			for (const AActor* Actor : Level->Actors)
			{
				bool bAlreadySeen = false;
				if (Actor)
				{
					Seen.Add(Actor, &bAlreadySeen);
					Shared += bAlreadySeen ? 1 : 0;
				}
			}
		}
	}
	UE_LOG(LogNRMapGen, Log, TEXT("  modules loaded and visible: %d/%d, actors shared between modules: %d"), Loaded, Streamings.Num(), Shared);

	// Anything left from earlier loads shows up here: the count must match the live modules.
	int32 WorldTagged = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		for (const FName& Tag : It->Tags)
		{
			if (NRMapGen::IsModuleTag(Tag))
			{
				++WorldTagged;
				break;
			}
		}
	}
	UE_LOG(LogNRMapGen, Log, TEXT("  module-tagged actors in the world: %d"), WorldTagged);

	if (State != ENRMapGenState::Ready)
	{
		return;
	}
	UWorld* World = GetWorld();

	// Collision, traced against world geometry only (the player's capsule is not a wall).
	FCollisionObjectQueryParams Geometry;
	Geometry.AddObjectTypesToQuery(ECC_WorldStatic);
	Geometry.AddObjectTypesToQuery(ECC_WorldDynamic);
	const FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(NRMapGenCheck), false);

	// Doorways: from each room's centre straight out through each side, at knee height. A
	// joined side must be clear past the room's edge; any other side must stop at the wall.
	const TMap<int32, uint8> Masks = NRMapGen::OpenMasks(ActiveLayout);
	int32 Faults = 0;
	for (const FNRGeneratedRoom& Room : ActiveLayout.Rooms)
	{
		for (int32 Dir = 0; Dir < 4; ++Dir)
		{
			const FIntPoint Offset = NRGrid::ToOffset(static_cast<ENRGridDirection>(Dir));
			const FVector Start = GetCellCenter(Room.Cell) + FVector(0.0f, 0.0f, 60.0f);
			const FVector End = Start + FVector(Offset.X, Offset.Y, 0.0f) * (RoomSize * 0.5f + 50.0f);
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByObjectType(Hit, Start, End, Geometry, TraceParams);
			const bool bOpen = (Masks.FindRef(Room.RoomId) & (1 << Dir)) != 0;
			if (bOpen == bHit)
			{
				++Faults;
				UE_LOG(LogNRMapGen, Warning, TEXT("  room %d doorway %s should be %s but is %s%s"), Room.RoomId,
					NRGrid::Letter(static_cast<ENRGridDirection>(Dir)), bOpen ? TEXT("open") : TEXT("closed"), bHit ? TEXT("blocked") : TEXT("clear"),
					bHit ? *FString::Printf(TEXT(" by %s (%s) at %s"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
						*Hit.ImpactPoint.ToCompactString()) : TEXT(""));
			}
		}
	}

	// Floor: straight down every 25 cm along each connection, room centre to room centre. A
	// miss or a step means the player could fall through or trip at a seam.
	int32 Samples = 0;
	int32 Holes = 0;
	float MinZ = TNumericLimits<float>::Max();
	float MaxZ = TNumericLimits<float>::Lowest();
	for (const FNRGeneratedConnection& Connection : ActiveLayout.Connections)
	{
		const FNRGeneratedRoom* A = ActiveLayout.FindRoom(Connection.RoomA);
		const FNRGeneratedRoom* B = ActiveLayout.FindRoom(Connection.RoomB);
		if (!A || !B)
		{
			continue;
		}
		const FVector From = GetCellCenter(A->Cell);
		const FVector To = GetCellCenter(B->Cell);
		const int32 Steps = FMath::CeilToInt(FVector::Dist(From, To) / 25.0f);
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const FVector Point = FMath::Lerp(From, To, static_cast<float>(Step) / Steps);
			FHitResult Hit;
			++Samples;
			if (World->LineTraceSingleByObjectType(Hit, Point + FVector(0.0f, 0.0f, 100.0f), Point - FVector(0.0f, 0.0f, 100.0f), Geometry, TraceParams))
			{
				MinZ = FMath::Min(MinZ, static_cast<float>(Hit.ImpactPoint.Z));
				MaxZ = FMath::Max(MaxZ, static_cast<float>(Hit.ImpactPoint.Z));
				if (FMath::Abs(Hit.ImpactPoint.Z - Point.Z) > 1.0f)
				{
					UE_LOG(LogNRMapGen, Warning, TEXT("  floor between rooms %d and %d: %s (%s) at %s"), A->RoomId, B->RoomId,
						*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToCompactString());
				}
			}
			else
			{
				++Holes;
			}
		}
	}
	Faults += Holes > 0 || MaxZ - MinZ > 1.0f ? 1 : 0;
	UE_LOG(LogNRMapGen, Log, TEXT("  doorways checked: %d, floor samples: %d, holes: %d, height %.2f..%.2f, collision faults: %d"),
		ActiveLayout.Rooms.Num() * 4, Samples, Holes, MinZ, MaxZ, Faults);

	// Dormant rooms' enemies should stay where they were placed (within 800 cm of the centre).
	for (TActorIterator<ANRRoom> It(World); It; ++It)
	{
		if (!It->IsGenerated() || It->GetRoomState() != ENRRoomState::Dormant || !It->GetLevel())
		{
			continue;
		}
		float Farthest = 0.0f;
		const AActor* Wanderer = nullptr;
		for (const AActor* Actor : It->GetLevel()->Actors)
		{
			if (Cast<AEnemyCharacter>(Actor))
			{
				const float Distance = FVector::Dist2D(Actor->GetActorLocation(), It->GetActorLocation());
				if (Distance > Farthest)
				{
					Farthest = Distance;
					Wanderer = Actor;
				}
			}
		}
		if (Farthest > 900.0f)
		{
			UE_LOG(LogNRMapGen, Warning, TEXT("  %s is dormant but %s is %.0f cm from its centre"), *It->GetLabel(),
				*GetNameSafe(Wanderer), Farthest);
		}
	}

	float Length = 0.0f;
	bool bBuilding = false;
	const bool bPath = HasNavigationPath(&Length, &bBuilding);
	UE_LOG(LogNRMapGen, Log, TEXT("  navigation %s: start->boss path %s, %.0f cm"), bBuilding ? TEXT("building") : TEXT("idle"),
		bPath ? TEXT("whole") : TEXT("missing or partial"), Length);
	UE_LOG(LogNRMapGen, Log, TEXT("  signature %08x"), GetTypeHash(BuildSignature()));
}

void ANRMapGenerator::DrawLayout(const FNRGeneratedLayout& Layout, float Duration) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Up(0.0f, 0.0f, DebugDrawHeight);
	const float Half = RoomSize * 0.5f;
	FlushPersistentDebugLines(World);

	for (const FNRGeneratedRoom& Room : Layout.Rooms)
	{
		FColor Color = Room.MainPathIndex == INDEX_NONE ? FColor(120, 120, 120) : FColor::White;
		const TCHAR* TypeName = TEXT("Combat");
		switch (Room.RoomType)
		{
		case ENRGridRoomType::Start: Color = FColor::Green; TypeName = TEXT("Start"); break;
		case ENRGridRoomType::Safe: Color = FColor::Cyan; TypeName = TEXT("Safe"); break;
		case ENRGridRoomType::Elite: Color = FColor::Orange; TypeName = TEXT("Elite"); break;
		case ENRGridRoomType::Boss: Color = FColor::Red; TypeName = TEXT("Boss"); break;
		default: break;
		}
		const FVector Center = GetCellCenter(Room.Cell) + Up;
		DrawDebugBox(World, Center, FVector(Half, Half, 50.0f), Color, Duration > 0.0f, Duration, 0, 40.0f);
		DrawDebugString(World, Center + FVector(0.0f, 0.0f, 150.0f),
			FString::Printf(TEXT("R%d %s%s"), Room.RoomId, TypeName,
				Room.MainPathIndex != INDEX_NONE ? *FString::Printf(TEXT(" #%d"), Room.MainPathIndex) : TEXT("")),
			nullptr, Color, Duration, true, 2.0f);
	}

	for (const FNRGeneratedConnection& Connection : Layout.Connections)
	{
		const FNRGeneratedRoom* A = Layout.FindRoom(Connection.RoomA);
		const FNRGeneratedRoom* B = Layout.FindRoom(Connection.RoomB);
		if (!A || !B)
		{
			continue;
		}
		const bool bMainPath = A->MainPathIndex != INDEX_NONE && B->MainPathIndex != INDEX_NONE
			&& FMath::Abs(A->MainPathIndex - B->MainPathIndex) == 1;
		DrawDebugLine(World, GetCellCenter(A->Cell) + Up, GetCellCenter(B->Cell) + Up,
			bMainPath ? FColor::Yellow : FColor(180, 180, 255), Duration > 0.0f, Duration, 0, bMainPath ? 120.0f : 50.0f);
	}
}

// NR.Grid.Start / NR.Grid.Unload / NR.Grid.Status / NR.Grid.Generate / NR.Grid.TestMissing
namespace NRGridCommands
{
	ANRMapGenerator* Find(UWorld* World)
	{
		TActorIterator<ANRMapGenerator> It(World);
		if (!It)
		{
			UE_LOG(LogNRMapGen, Warning, TEXT("No NRMapGenerator in this world"));
			return nullptr;
		}
		return *It;
	}

	/** NR.Grid.Start [seed] [shuffle]: build a map; seed 0 or none for a random one. */
	void Start(const TArray<FString>& Args, UWorld* World)
	{
		ANRMapGenerator* Generator = World ? Find(World) : nullptr;
		if (!Generator)
		{
			return;
		}
		Generator->SetShuffleLoadOrder(Args.Num() > 1 && Args[1].Equals(TEXT("shuffle"), ESearchCase::IgnoreCase));
		Generator->SetDebugMissingRoom(INDEX_NONE);
		Generator->StartMap(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
	}

	/**
	 * NR.Grid.GoTo <roomId|start|safe|elite|boss> [x y]: puts the player in a generated room,
	 * at the centre or at x,y cm from it - a test shortcut for walking there.
	 */
	void GoTo(const TArray<FString>& Args, UWorld* World)
	{
		const ANRMapGenerator* Generator = World ? Find(World) : nullptr;
		APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		if (!Generator || !Pawn || Args.Num() == 0)
		{
			return;
		}

		const FNRGeneratedLayout& Layout = Generator->GetActiveLayout();
		const FNRGeneratedRoom* Room = nullptr;
		const UEnum* TypeEnum = StaticEnum<ENRGridRoomType>();
		const int64 Type = TypeEnum->GetValueByNameString(Args[0]);
		if (Type != INDEX_NONE)
		{
			Room = Layout.Rooms.FindByPredicate([Type](const FNRGeneratedRoom& Candidate) { return static_cast<int64>(Candidate.RoomType) == Type; });
		}
		else
		{
			Room = Layout.FindRoom(FCString::Atoi(*Args[0]));
		}
		if (!Room)
		{
			UE_LOG(LogNRMapGen, Warning, TEXT("NR.Grid.GoTo: no room %s"), *Args[0]);
			return;
		}

		const FVector Offset(Args.Num() > 2 ? FCString::Atof(*Args[1]) : 0.0f, Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.0f, 100.0f);
		const FVector Target = Generator->GetCellCenter(Room->Cell) + Offset;
		if (UMoverComponent* Mover = Pawn->FindComponentByClass<UMoverComponent>())
		{
			TSharedPtr<FTeleportEffect> Teleport = MakeShared<FTeleportEffect>();
			Teleport->TargetLocation = Target;
			Teleport->bUseActorRotation = true;
			Mover->QueueInstantMovementEffect(Teleport);
		}
		else
		{
			Pawn->TeleportTo(Target, Pawn->GetActorRotation());
		}
		UE_LOG(LogNRMapGen, Log, TEXT("Player sent to room %d (%s) at %s"), Room->RoomId,
			*TypeEnum->GetNameStringByValue(static_cast<int64>(Room->RoomType)), *Target.ToCompactString());
	}

	/** NR.Grid.TestEmptyRoom <seed> <roomId>: build with that room's enemies removed; a fighting room must fail the map. */
	void TestEmptyRoom(const TArray<FString>& Args, UWorld* World)
	{
		ANRMapGenerator* Generator = World ? Find(World) : nullptr;
		if (!Generator)
		{
			return;
		}
		Generator->SetShuffleLoadOrder(false);
		Generator->SetDebugMissingRoom(INDEX_NONE);
		Generator->SetDebugEmptyRoom(Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 1);
		Generator->StartMap(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 42);
	}

	/** NR.Grid.HurtRoom <roomId>: one point of damage to an enemy of that room, as a shot from outside would. */
	void HurtRoom(const TArray<FString>& Args, UWorld* World)
	{
		const int32 RoomId = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1;
		for (TActorIterator<ANRRoom> It(World); It; ++It)
		{
			if (It->IsGenerated() && It->GetGeneratedRoomId() == RoomId)
			{
				for (AActor* Actor : It->GetLevel()->Actors)
				{
					UHealthComponent* Health = Cast<AEnemyCharacter>(Actor) ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
					if (Health && !Health->IsDead())
					{
						Health->ApplyDamage(1.0f, nullptr, FVector::ForwardVector);
						UE_LOG(LogNRMapGen, Log, TEXT("Hurt %s in %s"), *Actor->GetName(), *It->GetLabel());
						return;
					}
				}
			}
		}
		UE_LOG(LogNRMapGen, Warning, TEXT("NR.Grid.HurtRoom: no living enemy in room %d"), RoomId);
	}

	/** NR.Grid.FindSeed [from] [count]: the first seeds with the most rooms and the most loops the settings allow. */
	void FindSeed(const TArray<FString>& Args, UWorld* World)
	{
		const ANRMapGenerator* Generator = World ? Find(World) : nullptr;
		if (!Generator)
		{
			return;
		}
		const FNRMapGenSettings& Settings = Generator->GetSettings();
		const int32 From = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1;
		const int32 Count = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 500;
		int32 MostRooms = INDEX_NONE;
		int32 MostLoops = INDEX_NONE;
		for (int32 Seed = From; Seed < From + Count && (MostRooms == INDEX_NONE || MostLoops == INDEX_NONE); ++Seed)
		{
			FNRGeneratedLayout Layout;
			FString Error;
			if (!FNRGridLayoutGenerator::Generate(Settings, Seed, Layout, Error))
			{
				continue;
			}
			if (MostRooms == INDEX_NONE && Layout.Rooms.Num() == Settings.MaxRooms)
			{
				MostRooms = Seed;
			}
			if (MostLoops == INDEX_NONE && FNRGridLayoutGenerator::CountLoops(Layout) == Settings.MaxLoops)
			{
				MostLoops = Seed;
			}
		}
		UE_LOG(LogNRMapGen, Log, TEXT("First seed with %d rooms: %d; with %d loops: %d"), Settings.MaxRooms, MostRooms, Settings.MaxLoops, MostLoops);
	}

	/** NR.Grid.TestMissing <seed> [roomId]: build with one room's level pointing nowhere; must fail whole. */
	void TestMissing(const TArray<FString>& Args, UWorld* World)
	{
		ANRMapGenerator* Generator = World ? Find(World) : nullptr;
		if (!Generator)
		{
			return;
		}
		Generator->SetShuffleLoadOrder(false);
		Generator->SetDebugMissingRoom(Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 3);
		Generator->StartMap(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 42);
		Generator->SetDebugMissingRoom(INDEX_NONE);
		Generator->LogStatus();
	}

	void Unload(const TArray<FString>& Args, UWorld* World)
	{
		if (ANRMapGenerator* Generator = World ? Find(World) : nullptr)
		{
			Generator->UnloadLayout();
		}
	}

	void Status(const TArray<FString>& Args, UWorld* World)
	{
		if (const ANRMapGenerator* Generator = World ? Find(World) : nullptr)
		{
			Generator->LogStatus();
		}
	}

	/** NR.Grid.Signature: the full placement text the signature hash is taken from. */
	void Signature(const TArray<FString>& Args, UWorld* World)
	{
		if (const ANRMapGenerator* Generator = World ? Find(World) : nullptr)
		{
			TArray<FString> Lines;
			Generator->BuildSignature().ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				UE_LOG(LogNRMapGen, Log, TEXT("  %s"), *Line);
			}
		}
	}

	/** NR.Grid.Generate [seed] [Linear|Branching|Loop|Mixed]: graph only, drawn for a minute. */
	void Generate(const TArray<FString>& Args, UWorld* World)
	{
		ANRMapGenerator* Generator = World ? Find(World) : nullptr;
		if (!Generator)
		{
			return;
		}
		const int32 InSeed = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : FMath::Rand();
		TOptional<ENRGridLayoutShape> Shape;
		if (Args.Num() > 1)
		{
			const int64 Value = StaticEnum<ENRGridLayoutShape>()->GetValueByNameString(Args[1]);
			if (Value == INDEX_NONE)
			{
				UE_LOG(LogNRMapGen, Warning, TEXT("Unknown shape %s: Linear, Branching, Loop or Mixed"), *Args[1]);
				return;
			}
			Shape = static_cast<ENRGridLayoutShape>(Value);
		}
		FString Error;
		if (Generator->GenerateLayout(InSeed, Error, Shape))
		{
			Generator->DrawLayout(Generator->GetGeneratedLayout(), 60.0f);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRGridStartCommand(TEXT("NR.Grid.Start"),
	TEXT("NR.Grid.Start [seed] [shuffle]: generate and load a map (seed 0 or none for random; shuffle requests modules in random order)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::Start));

static FAutoConsoleCommandWithWorldAndArgs GNRGridGoToCommand(TEXT("NR.Grid.GoTo"),
	TEXT("NR.Grid.GoTo <roomId|Start|Safe|Elite|Boss> [x y]: put the player in a generated room."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::GoTo));

static FAutoConsoleCommandWithWorldAndArgs GNRGridTestEmptyRoomCommand(TEXT("NR.Grid.TestEmptyRoom"),
	TEXT("NR.Grid.TestEmptyRoom <seed> <roomId>: load with that room's enemies removed; a fighting room must fail the map."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::TestEmptyRoom));

static FAutoConsoleCommandWithWorldAndArgs GNRGridHurtRoomCommand(TEXT("NR.Grid.HurtRoom"),
	TEXT("NR.Grid.HurtRoom <roomId>: damage one enemy of that room, like a shot from outside."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::HurtRoom));

static FAutoConsoleCommandWithWorldAndArgs GNRGridFindSeedCommand(TEXT("NR.Grid.FindSeed"),
	TEXT("NR.Grid.FindSeed [from] [count]: first seeds giving the most rooms and the most loops."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::FindSeed));

static FAutoConsoleCommandWithWorldAndArgs GNRGridTestMissingCommand(TEXT("NR.Grid.TestMissing"),
	TEXT("NR.Grid.TestMissing <seed> [roomId]: load a map with one room's level missing; it must fail as a whole."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::TestMissing));

static FAutoConsoleCommandWithWorldAndArgs GNRGridUnloadCommand(TEXT("NR.Grid.Unload"),
	TEXT("Unload every module the map generator loaded."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::Unload));

static FAutoConsoleCommandWithWorldAndArgs GNRGridStatusCommand(TEXT("NR.Grid.Status"),
	TEXT("Log the map generator's modules, collision and navigation checks and placement signature."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::Status));

static FAutoConsoleCommandWithWorldAndArgs GNRGridSignatureCommand(TEXT("NR.Grid.Signature"),
	TEXT("Log the full placement text of the loaded map."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::Signature));

static FAutoConsoleCommandWithWorldAndArgs GNRGridGenerateCommand(TEXT("NR.Grid.Generate"),
	TEXT("NR.Grid.Generate [seed] [Linear|Branching|Loop|Mixed]: generate a layout graph, log it and draw it for a minute."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::Generate));
