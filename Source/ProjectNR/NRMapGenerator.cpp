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
#include "NREncounterData.h"
#include "NRMapGenerationConfig.h"
#include "NRRunSubsystem.h"
#include "NRSpawnPoint.h"
#include "NRSpawnSubsystem.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRMapGen, Log, All);

namespace NRMapGen
{
	FName PortTag(const TCHAR* Letter) { return FName(*FString::Printf(TEXT("NRPort.%s"), Letter)); }

	FNRRoomPort MakePort(const FIntPoint& Cell, ENRGridDirection Side)
	{
		FNRRoomPort Port;
		Port.Cell = Cell;
		Port.Side = Side;
		return Port;
	}

	/** A room's template footprint before it was turned. */
	FIntPoint TemplateFootprint(const FNRGeneratedRoom& Room)
	{
		return NRGrid::TurnFootprint(Room.Footprint, Room.RotationSteps);
	}

	/** A doorway of a placed room, given by world cell and direction, as its template names it. */
	FNRRoomPort ToTemplate(const FNRGeneratedRoom& Room, const FNRRoomPort& World)
	{
		return MakePort(NRGrid::UnturnCell(World.Cell - Room.Cell, TemplateFootprint(Room), Room.RotationSteps),
			NRGrid::WorldToLocal(World.Side, Room.RotationSteps));
	}

	/** Module tag of one doorway place, in the template's own frame: NRBlock.N in a one-cell
	 *  room, NRBlock.1_0.N (cell 1,0, north side) in a bigger one. */
	FName SlotTag(const TCHAR* Kind, const FNRGeneratedRoom& Room, const FNRRoomPort& World)
	{
		const FNRRoomPort Local = ToTemplate(Room, World);
		return TemplateFootprint(Room) == FIntPoint(1, 1)
			? FName(*FString::Printf(TEXT("%s.%s"), Kind, NRGrid::Letter(Local.Side)))
			: FName(*FString::Printf(TEXT("%s.%d_%d.%s"), Kind, Local.Cell.X, Local.Cell.Y, NRGrid::Letter(Local.Side)));
	}

	/** Every doorway place on a room's outline, by world cell and direction. */
	TArray<FNRRoomPort> Outline(const FNRGeneratedRoom& Room)
	{
		TArray<FNRRoomPort> Places = FNRRoomTemplateDesc::OutlinePorts(Room.Footprint, 0b1111);
		for (FNRRoomPort& Place : Places)
		{
			Place.Cell += Room.Cell;
		}
		return Places;
	}

	/** Doorways each room uses, by world cell and direction, by room id. */
	TMap<int32, TArray<FNRRoomPort>> OpenPorts(const FNRGeneratedLayout& Layout)
	{
		TMap<int32, TArray<FNRRoomPort>> Open;
		for (const FNRGeneratedConnection& Connection : Layout.Connections)
		{
			ENRGridDirection Direction;
			if (NRGrid::DirectionBetween(Connection.CellA, Connection.CellB, Direction))
			{
				Open.FindOrAdd(Connection.RoomA).Add(MakePort(Connection.CellA, Direction));
				Open.FindOrAdd(Connection.RoomB).Add(MakePort(Connection.CellB, NRGrid::Opposite(Direction)));
			}
		}
		return Open;
	}

	bool IsModuleTag(const FName& Tag)
	{
		const FString Text = Tag.ToString();
		return Text.StartsWith(TEXT("NRPort.")) || Text.StartsWith(TEXT("NRBlock."));
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
	Link.CellA = Start.Cell;
	Link.CellB = Second.Cell;
	FixedLayout.Connections.Add(Link);
	FixedLayout.StartRoomId = 0;
}

void ANRMapGenerator::BeginPlay()
{
	Super::BeginPlay();
	if (bStartOnBeginPlay)
	{
		// A same-seed restart passes the seed as a level option; a restart of a map started with
		// a chosen shape or environment mode passes those too.
		int32 UseSeed = Seed;
		const AGameModeBase* GameMode = GetWorld()->GetAuthGameMode();
		const FString Options = GameMode ? GameMode->OptionsString : FString();
		const FString SeedOption = UGameplayStatics::ParseOption(Options, TEXT("GridSeed"));
		if (!SeedOption.IsEmpty())
		{
			UseSeed = FCString::Atoi(*SeedOption);
		}
		TOptional<ENRGridLayoutShape> ShapeOption;
		TOptional<ENRMapEnvironmentMode> EnvironmentOption;
		const int64 ShapeValue = StaticEnum<ENRGridLayoutShape>()->GetValueByNameString(UGameplayStatics::ParseOption(Options, TEXT("GridShape")));
		if (ShapeValue != INDEX_NONE)
		{
			ShapeOption = static_cast<ENRGridLayoutShape>(ShapeValue);
		}
		const int64 EnvironmentValue = StaticEnum<ENRMapEnvironmentMode>()->GetValueByNameString(UGameplayStatics::ParseOption(Options, TEXT("GridEnv")));
		if (EnvironmentValue != INDEX_NONE)
		{
			EnvironmentOption = static_cast<ENRMapEnvironmentMode>(EnvironmentValue);
		}
		TOptional<ENRHybridAssignment> AssignmentOption;
		const int64 AssignmentValue = StaticEnum<ENRHybridAssignment>()->GetValueByNameString(UGameplayStatics::ParseOption(Options, TEXT("GridAssign")));
		if (AssignmentValue != INDEX_NONE)
		{
			AssignmentOption = static_cast<ENRHybridAssignment>(AssignmentValue);
		}
		StartMapWithChoices(UseSeed, ShapeOption, EnvironmentOption, AssignmentOption);
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

FNRMapGenSettings ANRMapGenerator::GetEffectiveSettings() const
{
	FNRMapGenSettings Settings = GetSettings();
	if (ShapeChoice.IsSet())
	{
		Settings.Shape = ShapeChoice.GetValue();
	}
	if (EnvironmentChoice.IsSet())
	{
		Settings.EnvironmentMode = EnvironmentChoice.GetValue();
	}
	if (AssignmentChoice.IsSet())
	{
		Settings.HybridAssignment = AssignmentChoice.GetValue();
	}
	return Settings;
}

FVector ANRMapGenerator::GetCellCenter(const FIntPoint& Cell) const
{
	const float Pitch = RoomSize + GetCorridorLength();
	return GetActorLocation() + FVector(Cell.X * Pitch, Cell.Y * Pitch, 0.0f);
}

FVector ANRMapGenerator::GetRoomCenter(const FNRGeneratedRoom& Room) const
{
	// The middle of the footprint: halfway between its first and last cell centres.
	return (GetCellCenter(Room.Cell) + GetCellCenter(Room.Cell + Room.Footprint - FIntPoint(1, 1))) * 0.5f;
}

FTransform ANRMapGenerator::GetRoomTransform(const FNRGeneratedRoom& Room) const
{
	return FTransform(FRotator(0.0f, 90.0f * Room.RotationSteps, 0.0f), GetRoomCenter(Room));
}

FTransform ANRMapGenerator::GetCorridorTransform(const FNRGeneratedConnection& Connection) const
{
	// Halfway between the two cells, running along the link: the corridor template runs along
	// its local X, so a north-south link is turned a quarter.
	const FVector Middle = (GetCellCenter(Connection.CellA) + GetCellCenter(Connection.CellB)) * 0.5f;
	const UNRCorridorTemplateData* Template = GetCorridorTemplate(Connection);
	if (!Template)
	{
		// The fallback corridor is the same either way round.
		const bool bAlongY = Connection.CellA.X == Connection.CellB.X;
		return FTransform(FRotator(0.0f, bAlongY ? 90.0f : 0.0f, 0.0f), Middle);
	}

	// A chosen corridor runs from its A end (NRPort.A, local -X) at the room of its EndpointA
	// environment to its B end at the other room.
	const FNRGeneratedRoom* RoomA = ActiveLayout.FindRoom(Connection.RoomA);
	const bool bFromA = !RoomA || RoomA->Environment == Template->EndpointA;
	ENRGridDirection Direction = ENRGridDirection::East;
	NRGrid::DirectionBetween(bFromA ? Connection.CellA : Connection.CellB, bFromA ? Connection.CellB : Connection.CellA, Direction);
	float Yaw = 0.0f;
	switch (Direction)
	{
	case ENRGridDirection::North: Yaw = 90.0f; break;
	case ENRGridDirection::West: Yaw = 180.0f; break;
	case ENRGridDirection::South: Yaw = 270.0f; break;
	default: break;
	}
	return FTransform(FRotator(0.0f, Yaw, 0.0f), Middle);
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
		Desc.Footprint = Template->Footprint;
		Desc.Ports = Template->Ports.Num() > 0 ? Template->Ports
			: FNRRoomTemplateDesc::OutlinePorts(Template->Footprint, static_cast<uint8>(Template->PortMask & 0b1111));
		Desc.RotationMask = static_cast<uint8>(Template->AllowedRotations & 0b1111);
		Desc.Weight = Template->Weight;
		Desc.Environment = Template->Environment;
		Desc.IndoorSide = Template->bFixIndoorSide ? static_cast<int32>(Template->IndoorSide) : INDEX_NONE;
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

TArray<FNRCorridorTemplateDesc> ANRMapGenerator::DescribeCorridors() const
{
	TArray<FNRCorridorTemplateDesc> Descs;
	if (!Config)
	{
		return Descs;
	}
	for (const UNRCorridorTemplateData* Template : Config->CorridorTemplates)
	{
		FNRCorridorTemplateDesc& Desc = Descs.AddDefaulted_GetRef();
		if (!Template)
		{
			Desc.Weight = 0.0f;
			continue;
		}
		Desc.EndpointA = Template->EndpointA;
		Desc.EndpointB = Template->EndpointB;
		Desc.Weight = Template->Weight;
	}
	return Descs;
}

const UNRCorridorTemplateData* ANRMapGenerator::GetCorridorTemplate(const FNRGeneratedConnection& Connection) const
{
	if (Config && Config->CorridorTemplates.IsValidIndex(Connection.TemplateIndex))
	{
		return Config->CorridorTemplates[Connection.TemplateIndex];
	}
	return nullptr;
}

TSoftObjectPtr<UWorld> ANRMapGenerator::GetCorridorLevel(const FNRGeneratedConnection& Connection) const
{
	if (const UNRCorridorTemplateData* Template = GetCorridorTemplate(Connection))
	{
		return Template->Level;
	}
	return Config && Config->CorridorTemplate ? Config->CorridorTemplate->Level : CorridorLevel;
}

float ANRMapGenerator::GetCorridorLength() const
{
	// Every corridor template is one length (StartMap checks): the grid has one pitch.
	if (Config)
	{
		for (const UNRCorridorTemplateData* Template : Config->CorridorTemplates)
		{
			if (Template)
			{
				return Template->Length;
			}
		}
	}
	return Config && Config->CorridorTemplate ? Config->CorridorTemplate->Length : CorridorLength;
}

bool ANRMapGenerator::GenerateLayout(int32 InSeed, FString& OutError, TOptional<ENRGridLayoutShape> ShapeOverride,
	TOptional<ENRMapEnvironmentMode> EnvironmentOverride, TOptional<ENRHybridAssignment> AssignmentOverride)
{
	FNRMapGenSettings Settings = GetEffectiveSettings();
	if (ShapeOverride.IsSet())
	{
		Settings.Shape = ShapeOverride.GetValue();
	}
	if (EnvironmentOverride.IsSet())
	{
		Settings.EnvironmentMode = EnvironmentOverride.GetValue();
	}
	if (AssignmentOverride.IsSet())
	{
		Settings.HybridAssignment = AssignmentOverride.GetValue();
	}

	GeneratedLayout = FNRGeneratedLayout();
	FNRGeneratedLayout Layout;
	if (!FNRGridLayoutGenerator::Generate(Settings, DescribeTemplates(), DescribeCorridors(), InSeed, Layout, OutError))
	{
		UE_LOG(LogNRMapGen, Error, TEXT("Layout generation failed: %s"), *OutError);
		return false;
	}
	GeneratedLayout = MoveTemp(Layout);

	const UEnum* ShapeEnum = StaticEnum<ENRGridLayoutShape>();
	const int32 Halls = FNRGridLayoutGenerator::CountHalls(GeneratedLayout);
	// Rooms and hall cells per environment, and the cells they cover.
	int32 ByEnvironment[3] = { 0, 0, 0 };
	int32 CellsByEnvironment[3] = { 0, 0, 0 };
	for (const FNRGeneratedRoom& Room : GeneratedLayout.Rooms)
	{
		++ByEnvironment[static_cast<int32>(Room.Environment)];
		CellsByEnvironment[static_cast<int32>(Room.Environment)] += Room.Footprint.X * Room.Footprint.Y;
	}
	UE_LOG(LogNRMapGen, Log, TEXT("Layout seed %d (%s, %s, attempt %d, v%d): %d rooms, %d hall cell(s), %d corridors, %d loop(s), %d branch room(s); Indoor %d (%d cells), Outdoor %d (%d cells), Transition %d (%d cells)"),
		InSeed, *ShapeEnum->GetNameStringByValue(static_cast<int64>(Settings.Shape)),
		*(StaticEnum<ENRMapEnvironmentMode>()->GetNameStringByValue(static_cast<int64>(Settings.EnvironmentMode))
			+ (Settings.EnvironmentMode == ENRMapEnvironmentMode::Hybrid
				? TEXT(" ") + StaticEnum<ENRHybridAssignment>()->GetNameStringByValue(static_cast<int64>(Settings.HybridAssignment))
				: FString())),
		GeneratedLayout.Attempt, GeneratedLayout.GeneratorVersion,
		GeneratedLayout.Rooms.Num() - Halls, Halls, GeneratedLayout.Connections.Num(),
		FNRGridLayoutGenerator::CountLoops(GeneratedLayout), FNRGridLayoutGenerator::CountBranchRooms(GeneratedLayout),
		ByEnvironment[0], CellsByEnvironment[0], ByEnvironment[1], CellsByEnvironment[1], ByEnvironment[2], CellsByEnvironment[2]);
	// Room types on the left, environments (I, O, T) on the right.
	TArray<FString> Lines;
	TArray<FString> EnvironmentLines;
	FNRGridLayoutGenerator::ToAscii(Settings, GeneratedLayout).ParseIntoArrayLines(Lines, false);
	FNRGridLayoutGenerator::ToAscii(Settings, GeneratedLayout, true).ParseIntoArrayLines(EnvironmentLines, false);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		UE_LOG(LogNRMapGen, Log, TEXT("    %s    %s"), *Lines[Index], EnvironmentLines.IsValidIndex(Index) ? *EnvironmentLines[Index] : TEXT(""));
	}
	return true;
}

// --- Building ---------------------------------------------------------------------------

bool ANRMapGenerator::StartMap(int32 InSeed)
{
	return StartMapWithChoices(InSeed, {}, {});
}

bool ANRMapGenerator::StartMapWithOptions(int32 InSeed, ENRGridLayoutShape InShape, ENRMapEnvironmentMode InEnvironmentMode)
{
	return StartMapWithChoices(InSeed, InShape, InEnvironmentMode);
}

bool ANRMapGenerator::StartMapWithEnvironment(int32 InSeed, ENRMapEnvironmentMode InEnvironmentMode)
{
	return StartMapWithChoices(InSeed, {}, InEnvironmentMode);
}

bool ANRMapGenerator::StartMapWithChoices(int32 InSeed, TOptional<ENRGridLayoutShape> InShape, TOptional<ENRMapEnvironmentMode> InEnvironmentMode,
	TOptional<ENRHybridAssignment> InAssignment)
{
	UnloadLayout();
	ShapeChoice = InShape;
	EnvironmentChoice = InEnvironmentMode;
	AssignmentChoice = InAssignment;
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
		if (DescribeTemplates().Num() == 0)
		{
			ActiveLayout = FNRGeneratedLayout();
			ActiveLayout.Seed = UseSeed;
			Fail(TEXT("the config has no room templates"));
			return false;
		}
		for (const UNRRoomTemplateData* Template : Config->RoomTemplates)
		{
			if (Template && !FMath::IsNearlyEqual(Template->Size, RoomSize))
			{
				ActiveLayout = FNRGeneratedLayout();
				ActiveLayout.Seed = UseSeed;
				Fail(FString::Printf(TEXT("room template %s has %.0f cm cells, the map's are %.0f cm"), *GetNameSafe(Template), Template->Size, RoomSize));
				return false;
			}
		}
		// One corridor length for the whole grid, whichever corridor a link gets.
		for (const UNRCorridorTemplateData* Template : Config->CorridorTemplates)
		{
			if (Template && !FMath::IsNearlyEqual(Template->Length, GetCorridorLength()))
			{
				ActiveLayout = FNRGeneratedLayout();
				ActiveLayout.Seed = UseSeed;
				Fail(FString::Printf(TEXT("corridor template %s is %.0f cm long, the others %.0f cm"), *GetNameSafe(Template), Template->Length, GetCorridorLength()));
				return false;
			}
		}

		// Templates are chosen while the layout is generated: a room's shape decides where it fits.
		FString Error;
		if (!GenerateLayout(UseSeed, Error))
		{
			ActiveLayout = FNRGeneratedLayout();
			ActiveLayout.Seed = UseSeed;
			Fail(Error);
			return false;
		}
		ActiveLayout = GeneratedLayout;
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
		if (!A || !B || !A->Covers(Connection.CellA) || !B->Covers(Connection.CellB)
			|| !NRGrid::DirectionBetween(Connection.CellA, Connection.CellB, Direction))
		{
			OutError = FString::Printf(TEXT("connection %d does not join neighbouring cells of its two rooms"), Index);
			return false;
		}
		const TSoftObjectPtr<UWorld> Level = GetCorridorLevel(Connection);
		if (Level.IsNull())
		{
			OutError = FString::Printf(TEXT("connection %d has no corridor level"), Index);
			return false;
		}
		Requests.Add({ true, Index, Level, GetCorridorTransform(Connection) });
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
	const TMap<int32, TArray<FNRRoomPort>> Open = NRMapGen::OpenPorts(ActiveLayout);
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
		TArray<ANRSpawnPoint*> SpawnPoints;
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
			else if (ANRSpawnPoint* Point = Cast<ANRSpawnPoint>(Actor))
			{
				SpawnPoints.Add(Point);
			}
		}

		// A hall cell is a corridor: no room logic, nobody waiting in it.
		if (Room.RoomType == ENRGridRoomType::Hall)
		{
			if (RoomActors.Num() > 0 || Enemies.Num() > 0 || SpawnPoints.Num() > 0)
			{
				Errors.Add(FString::Printf(TEXT("hall cell %d: %d NRRoom(s), %d enemy(s) and %d spawn point(s) in a corridor"), Room.RoomId,
					RoomActors.Num(), Enemies.Num(), SpawnPoints.Num()));
			}
			continue;
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
			SpawnPoints.Reset();
			DebugEmptyRoomId = INDEX_NONE;
		}

		const bool bFights = Room.RoomType == ENRGridRoomType::Combat || Room.RoomType == ENRGridRoomType::Elite || Room.RoomType == ENRGridRoomType::Boss;
		// Enemies come placed in the level or from its spawn points; a fighting room needs one or
		// the other, a safe one neither.
		if (bFights && Enemies.Num() == 0 && SpawnPoints.Num() == 0)
		{
			Errors.Add(FString::Printf(TEXT("room %d (%s): fighting room without an enemy or a spawn point"), Room.RoomId,
				*UEnum::GetValueAsString(Room.RoomType)));
			continue;
		}
		if (!bFights && (Enemies.Num() > 0 || SpawnPoints.Num() > 0))
		{
			Errors.Add(FString::Printf(TEXT("room %d (%s): %d enemy(s) and %d spawn point(s) in a room meant to be safe"), Room.RoomId,
				*UEnum::GetValueAsString(Room.RoomType), Enemies.Num(), SpawnPoints.Num()));
			continue;
		}

		// The room box must cover the whole footprint, or part of the room would never activate it.
		{
			const FVector Center = GetRoomCenter(Room);
			const FVector Reach = (GetCellCenter(Room.Cell + Room.Footprint - FIntPoint(1, 1)) - GetCellCenter(Room.Cell)) * 0.5f
				+ FVector(RoomSize * 0.5f - 300.0f, RoomSize * 0.5f - 300.0f, 0.0f);
			for (const FVector Corner : { Center + FVector(Reach.X, Reach.Y, 0.0f), Center + FVector(-Reach.X, Reach.Y, 0.0f),
				Center + FVector(Reach.X, -Reach.Y, 0.0f), Center + FVector(-Reach.X, -Reach.Y, 0.0f) })
			{
				if (!RoomActors[0]->ContainsLocation(Corner))
				{
					Errors.Add(FString::Printf(TEXT("room %d: its NRRoom box does not cover its %dx%d footprint"), Room.RoomId, Room.Footprint.X, Room.Footprint.Y));
					break;
				}
			}
		}

		// Doors on joined doorways belong to the room; the rest are hidden behind the blockers.
		TSet<FName> JoinedDoors;
		for (const FNRRoomPort& Port : Open.FindRef(Room.RoomId))
		{
			JoinedDoors.Add(NRMapGen::SlotTag(TEXT("NRDoor"), Room, Port));
		}
		TArray<ANRDoor*> Doors;
		for (AActor* Actor : Level->Actors)
		{
			const FName* DoorTag = Actor ? Actor->Tags.FindByPredicate([](const FName& Tag) { return Tag.ToString().StartsWith(TEXT("NRDoor.")); }) : nullptr;
			if (!DoorTag)
			{
				continue;
			}
			if (ANRDoor* Door = Cast<ANRDoor>(Actor); Door && JoinedDoors.Contains(*DoorTag))
			{
				Doors.Add(Door);
			}
			else
			{
				Actor->SetActorHiddenInGame(true);
				Actor->SetActorEnableCollision(false);
			}
		}
		if (Doors.Num() != JoinedDoors.Num())
		{
			Errors.Add(FString::Printf(TEXT("room %d: %d joined doorway(s) but %d door(s) found for them"), Room.RoomId, JoinedDoors.Num(), Doors.Num()));
			continue;
		}

		RoomActors[0]->InitializeGenerated(Room.RoomId, Room.RoomType, Room.RoomId == ActiveLayout.BossRoomId, Doors, Enemies, SpawnPoints);
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
	const TMap<int32, TArray<FNRRoomPort>> Open = NRMapGen::OpenPorts(ActiveLayout);

	// Doorways: open the joined ones, leave the rest walled. Hidden and without collision, so
	// an opened blocker leaves neither a visible panel nor an invisible wall.
	int32 Opened = 0;
	for (const FNRGeneratedRoom& Room : ActiveLayout.Rooms)
	{
		const int32 ModuleIndex = FindRoomModule(Room.RoomId);
		for (const FNRRoomPort& Port : Open.FindRef(Room.RoomId))
		{
			const FName Tag = NRMapGen::SlotTag(TEXT("NRBlock"), Room, Port);
			const TArray<AActor*> Blockers = FindTagged(ModuleIndex, Tag);
			// A hall cell is a template with exactly the doorways it joins: nothing to open.
			if (Blockers.Num() == 0 && Room.RoomType == ENRGridRoomType::Hall)
			{
				++Opened;
				continue;
			}
			if (Blockers.Num() == 0)
			{
				Errors.Add(FString::Printf(TEXT("room %d: joined at %d,%d %s but its level has no %s"), Room.RoomId,
					Port.Cell.X, Port.Cell.Y, NRGrid::Letter(Port.Side), *Tag.ToString()));
				continue;
			}
			for (AActor* Blocker : Blockers)
			{
				Blocker->SetActorHiddenInGame(true);
				Blocker->SetActorEnableCollision(false);
			}
			++Opened;
		}
	}

	// Ports: each connection's room doorways must meet its corridor's ends.
	for (int32 Index = 0; Index < ActiveLayout.Connections.Num(); ++Index)
	{
		const FNRGeneratedConnection& Connection = ActiveLayout.Connections[Index];
		const FNRGeneratedRoom* A = ActiveLayout.FindRoom(Connection.RoomA);
		const FNRGeneratedRoom* B = ActiveLayout.FindRoom(Connection.RoomB);
		ENRGridDirection Direction;
		if (!A || !B || !NRGrid::DirectionBetween(Connection.CellA, Connection.CellB, Direction))
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

		auto CheckRoomPort = [&](const FNRGeneratedRoom& Room, const FIntPoint& Cell, ENRGridDirection WorldDirection)
		{
			const FName Tag = NRMapGen::SlotTag(TEXT("NRPort"), Room, NRMapGen::MakePort(Cell, WorldDirection));
			const TArray<AActor*> Ports = FindTagged(FindRoomModule(Room.RoomId), Tag);
			if (Ports.Num() != 1)
			{
				Errors.Add(FString::Printf(TEXT("room %d: level needs exactly one %s"), Room.RoomId, *Tag.ToString()));
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
		CheckRoomPort(*A, Connection.CellA, Direction);
		CheckRoomPort(*B, Connection.CellB, NRGrid::Opposite(Direction));
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

	// The player goes to the start room now (still held), so the teleport has landed by the
	// time the rooms wake up; navigation has to be rebuilt over the new geometry meanwhile.
	if (bMovePlayerToStartOnReady)
	{
		MovePlayerToStart();
	}
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
		GetRoomCenter(*From) + FVector(0.0f, 0.0f, 50.0f), GetRoomCenter(*To) + FVector(0.0f, 0.0f, 50.0f));
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

	// The teleport to the start room (sent when the modules were set up) lands on the pawn's next
	// movement update. No room may look for the player before it has, or a room standing where
	// the player waited during loading would wake up.
	bool bPlayerPlaced = true;
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const FNRGeneratedRoom* Start = ActiveLayout.FindRoom(ActiveLayout.StartRoomId);
	if (bMovePlayerToStartOnReady && Pawn && Start)
	{
		const FVector Half = (GetCellCenter(Start->Cell + Start->Footprint - FIntPoint(1, 1)) - GetCellCenter(Start->Cell)) * 0.5f
			+ FVector(RoomSize * 0.5f, RoomSize * 0.5f, 0.0f);
		const FVector Offset = Pawn->GetActorLocation() - GetRoomCenter(*Start);
		bPlayerPlaced = FMath::Abs(Offset.X) <= Half.X && FMath::Abs(Offset.Y) <= Half.Y;
		if (!bPlayerPlaced && GetWorld()->GetTimeSeconds() - NavigationStartTime > 5.0f)
		{
			UE_LOG(LogNRMapGen, Warning, TEXT("Generation %d: the player is still outside the start room 5 s after the teleport; going on"), Generation);
			bPlayerPlaced = true;
		}
	}

	bool bBuilding = false;
	if (HasNavigationPath(nullptr, &bBuilding) && !bBuilding && bPlayerPlaced)
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

	// Rooms spawn their enemies by this map's encounter rules, seeded by the map: the same seed
	// fills the same rooms the same way.
	if (UNRSpawnSubsystem* Spawner = GetWorld()->GetSubsystem<UNRSpawnSubsystem>())
	{
		Spawner->Configure(Config ? Config->Encounters.Get() : nullptr, ActiveLayout.Seed);
	}

	// The run starts only now, with every room registered, so it can neither be won nor
	// count a clear before the map is whole; the rooms come alive after it.
	UNRRunSubsystem* Run = GetWorld()->GetSubsystem<UNRRunSubsystem>();
	if (bStartRunOnReady && Run && GeneratedRooms.Num() > 0)
	{
		Run->ResetRun();
		Run->SetGeneratedSeed(ActiveLayout.Seed);
		TArray<FString> Choices;
		if (ShapeChoice.IsSet())
		{
			Choices.Add(TEXT("GridShape=") + StaticEnum<ENRGridLayoutShape>()->GetNameStringByValue(static_cast<int64>(ShapeChoice.GetValue())));
		}
		if (EnvironmentChoice.IsSet())
		{
			Choices.Add(TEXT("GridEnv=") + StaticEnum<ENRMapEnvironmentMode>()->GetNameStringByValue(static_cast<int64>(EnvironmentChoice.GetValue())));
		}
		if (AssignmentChoice.IsSet())
		{
			Choices.Add(TEXT("GridAssign=") + StaticEnum<ENRHybridAssignment>()->GetNameStringByValue(static_cast<int64>(AssignmentChoice.GetValue())));
		}
		Run->SetGeneratedMapOptions(FString::Join(Choices, TEXT("?")));
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
	FVector Target = GetRoomCenter(*Start) + FVector(0.0f, 0.0f, 100.0f);
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
	const TMap<int32, TArray<FNRRoomPort>> Open = NRMapGen::OpenPorts(ActiveLayout);

	TArray<FNRGeneratedRoom> Rooms = ActiveLayout.Rooms;
	Rooms.Sort([](const FNRGeneratedRoom& A, const FNRGeneratedRoom& B) { return A.RoomId < B.RoomId; });
	for (const FNRGeneratedRoom& Room : Rooms)
	{
		const int32 ModuleIndex = FindRoomModule(Room.RoomId);
		// Port markers by tag, wherever they ended up.
		TArray<FString> Ports;
		const ULevelStreamingDynamic* Streaming = Streamings.IsValidIndex(ModuleIndex) ? Streamings[ModuleIndex].Get() : nullptr;
		if (const ULevel* Level = Streaming ? Streaming->GetLoadedLevel() : nullptr)
		{
			for (const AActor* Actor : Level->Actors)
			{
				for (const FName& Tag : Actor ? Actor->Tags : TArray<FName>())
				{
					if (Tag.ToString().StartsWith(TEXT("NRPort.")))
					{
						const FVector Location = Actor->GetActorLocation();
						Ports.Add(FString::Printf(TEXT("%s(%.0f,%.0f)"), *Tag.ToString().RightChop(7), Location.X, Location.Y));
					}
				}
			}
		}
		Ports.Sort();
		TArray<FString> Joined;
		for (const FNRRoomPort& Port : Open.FindRef(Room.RoomId))
		{
			Joined.Add(FString::Printf(TEXT("%d_%d.%s"), Port.Cell.X, Port.Cell.Y, NRGrid::Letter(Port.Side)));
		}
		Joined.Sort();
		const FVector Location = ModuleIndex != INDEX_NONE ? Modules[ModuleIndex].Transform.GetLocation() : FVector::ZeroVector;
		Parts.Add(FString::Printf(TEXT("R%d %c t%d r%d %dx%d (%.0f,%.0f) open %s %s"), Room.RoomId, NRGrid::EnvironmentLetter(Room.Environment),
			Room.TemplateIndex, Room.RotationSteps,
			Room.Footprint.X, Room.Footprint.Y, Location.X, Location.Y, *FString::Join(Joined, TEXT(",")), *FString::Join(Ports, TEXT(""))));
	}
	for (int32 Index = 0; Index < ActiveLayout.Connections.Num(); ++Index)
	{
		const int32 ModuleIndex = FindCorridorModule(Index);
		const FTransform Transform = ModuleIndex != INDEX_NONE ? Modules[ModuleIndex].Transform : FTransform::Identity;
		Parts.Add(FString::Printf(TEXT("C%d %d-%d c%d (%.0f,%.0f) yaw %.0f"), Index, ActiveLayout.Connections[Index].RoomA,
			ActiveLayout.Connections[Index].RoomB, ActiveLayout.Connections[Index].TemplateIndex, Transform.GetLocation().X,
			Transform.GetLocation().Y, Transform.Rotator().Yaw));
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

	// What stands where: every room's type, environment and template, every corridor's two
	// environments and the corridor template chosen for them.
	const UEnum* TypeEnum = StaticEnum<ENRGridRoomType>();
	for (const FNRGeneratedRoom& Room : ActiveLayout.Rooms)
	{
		const UNRRoomTemplateData* Template = Config && Config->RoomTemplates.IsValidIndex(Room.TemplateIndex) ? Config->RoomTemplates[Room.TemplateIndex].Get() : nullptr;
		UE_LOG(LogNRMapGen, Log, TEXT("  room %d: %s, %s, template %s (t%d), turn %d, cell %d,%d %dx%d%s"), Room.RoomId,
			*TypeEnum->GetNameStringByValue(static_cast<int64>(Room.RoomType)), *FNRGridLayoutGenerator::EnvironmentName(Room.Environment),
			Template ? *Template->GetName() : *RoomLevel.GetAssetName(), Room.TemplateIndex, Room.RotationSteps, Room.Cell.X, Room.Cell.Y,
			Room.Footprint.X, Room.Footprint.Y, Template && Template->Environment != Room.Environment ? TEXT(" - TEMPLATE ENVIRONMENT DIFFERS") : TEXT(""));
	}
	for (int32 Index = 0; Index < ActiveLayout.Connections.Num(); ++Index)
	{
		const FNRGeneratedConnection& Connection = ActiveLayout.Connections[Index];
		const FNRGeneratedRoom* A = ActiveLayout.FindRoom(Connection.RoomA);
		const FNRGeneratedRoom* B = ActiveLayout.FindRoom(Connection.RoomB);
		const UNRCorridorTemplateData* Template = GetCorridorTemplate(Connection);
		UE_LOG(LogNRMapGen, Log, TEXT("  corridor %d: rooms %d-%d, %s-%s, %s (c%d)%s"), Index, Connection.RoomA, Connection.RoomB,
			A ? *FNRGridLayoutGenerator::EnvironmentName(A->Environment) : TEXT("?"), B ? *FNRGridLayoutGenerator::EnvironmentName(B->Environment) : TEXT("?"),
			Template ? *FString::Printf(TEXT("template %s joins %s-%s"), *Template->GetName(), *FNRGridLayoutGenerator::EnvironmentName(Template->EndpointA),
				*FNRGridLayoutGenerator::EnvironmentName(Template->EndpointB)) : *FString::Printf(TEXT("fallback %s"), *GetCorridorLevel(Connection).GetAssetName()),
			Connection.TemplateIndex,
			A && B && !NRGrid::EnvironmentsMeet(A->Environment, B->Environment) ? TEXT(" - INDOOR MEETS OUTDOOR") : TEXT(""));
	}

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

	// Doorways: from the centre of each cell on a room's outline straight out through its outer
	// side, at knee height. A joined side must be clear past the room's edge; any other side
	// must stop at the wall.
	const TMap<int32, TArray<FNRRoomPort>> Open = NRMapGen::OpenPorts(ActiveLayout);
	int32 Faults = 0;
	int32 Doorways = 0;
	for (const FNRGeneratedRoom& Room : ActiveLayout.Rooms)
	{
		for (const FNRRoomPort& Place : NRMapGen::Outline(Room))
		{
			++Doorways;
			const FIntPoint Offset = NRGrid::ToOffset(Place.Side);
			const FVector Start = GetCellCenter(Place.Cell) + FVector(0.0f, 0.0f, 60.0f);
			const FVector End = Start + FVector(Offset.X, Offset.Y, 0.0f) * (RoomSize * 0.5f + 50.0f);
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByObjectType(Hit, Start, End, Geometry, TraceParams);
			const bool bOpen = Open.FindRef(Room.RoomId).Contains(Place);
			if (bOpen == bHit)
			{
				++Faults;
				UE_LOG(LogNRMapGen, Warning, TEXT("  room %d doorway %d,%d %s should be %s but is %s%s"), Room.RoomId, Place.Cell.X, Place.Cell.Y,
					NRGrid::Letter(Place.Side), bOpen ? TEXT("open") : TEXT("closed"), bHit ? TEXT("blocked") : TEXT("clear"),
					bHit ? *FString::Printf(TEXT(" by %s (%s) at %s"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
						*Hit.ImpactPoint.ToCompactString()) : TEXT(""));
			}
		}
	}

	// Floor: straight down every 25 cm along each connection, cell centre to cell centre, and
	// 5 cm either side of each room-corridor seam. A miss or a step means the player could fall
	// through or trip at a seam. The regular samples are offset half a step so none lands exactly
	// on a seam, where two floors meeting edge to edge can both miss a trace by a rounding error.
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
		const FVector From = GetCellCenter(Connection.CellA);
		const FVector To = GetCellCenter(Connection.CellB);
		const float Length = FVector::Dist(From, To);
		const FVector Along = (To - From).GetSafeNormal();
		TArray<float> Distances;
		for (float Distance = 12.5f; Distance < Length; Distance += 25.0f)
		{
			Distances.Add(Distance);
		}
		for (const float Seam : { RoomSize * 0.5f, Length - RoomSize * 0.5f })
		{
			Distances.Append({ Seam - 5.0f, Seam + 5.0f });
		}
		for (const float Distance : Distances)
		{
			const FVector Point = From + Along * Distance;
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
				if (++Holes <= 5)
				{
					UE_LOG(LogNRMapGen, Warning, TEXT("  no floor between rooms %d and %d at %s"), A->RoomId, B->RoomId, *Point.ToCompactString());
				}
			}
		}
	}
	Faults += Holes > 0 || MaxZ - MinZ > 1.0f ? 1 : 0;
	UE_LOG(LogNRMapGen, Log, TEXT("  doorways checked: %d, floor samples: %d, holes: %d, height %.2f..%.2f, collision faults: %d"),
		Doorways, Samples, Holes, MinZ, MaxZ, Faults);

	// Dormant rooms' enemies should stay where they were placed: within 3/8 of a cell of the
	// centre (900 cm in a 24 m cell), plus half the span between the outer cells of a room of
	// several cells.
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
		const FNRGeneratedRoom* Room = ActiveLayout.FindRoom(It->GetGeneratedRoomId());
		const FVector Span = Room ? GetCellCenter(Room->Cell + Room->Footprint - FIntPoint(1, 1)) - GetCellCenter(Room->Cell) : FVector::ZeroVector;
		if (Farthest > RoomSize * 0.375f + 0.5f * Span.Size2D())
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
		case ENRGridRoomType::Hall: Color = FColor(90, 110, 160); TypeName = TEXT("Hall"); break;
		default: break;
		}
		// A room of several cells covers the corridor gaps between them too.
		const FVector Center = GetRoomCenter(Room) + Up;
		const FVector Extent = (GetCellCenter(Room.Cell + Room.Footprint - FIntPoint(1, 1)) - GetCellCenter(Room.Cell)) * 0.5f + FVector(Half, Half, 0.0f);
		DrawDebugBox(World, Center, FVector(Extent.X, Extent.Y, 50.0f), Color, Duration > 0.0f, Duration, 0, 40.0f);
		DrawDebugString(World, Center + FVector(0.0f, 0.0f, 150.0f),
			FString::Printf(TEXT("R%d %s %c%s"), Room.RoomId, TypeName, NRGrid::EnvironmentLetter(Room.Environment),
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
		DrawDebugLine(World, GetCellCenter(Connection.CellA) + Up, GetCellCenter(Connection.CellB) + Up,
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

	/**
	 * Reads a shape (Linear, Branching, Loop, Mixed), environment mode (IndoorOnly, OutdoorOnly,
	 * Hybrid; Indoor and Outdoor for short) or Hybrid assignment (PathSplit, DistanceFlood; Split
	 * and Flood for short) from one argument. False when it is none of them.
	 */
	bool ParseChoice(const FString& Arg, TOptional<ENRGridLayoutShape>& OutShape, TOptional<ENRMapEnvironmentMode>& OutEnvironment,
		TOptional<ENRHybridAssignment>& OutAssignment)
	{
		const int64 Assignment = StaticEnum<ENRHybridAssignment>()->GetValueByNameString(
			Arg.Equals(TEXT("Split"), ESearchCase::IgnoreCase) ? TEXT("PathSplit") : Arg.Equals(TEXT("Flood"), ESearchCase::IgnoreCase) ? TEXT("DistanceFlood") : *Arg);
		if (Assignment != INDEX_NONE)
		{
			OutAssignment = static_cast<ENRHybridAssignment>(Assignment);
			return true;
		}
		const int64 Shape = StaticEnum<ENRGridLayoutShape>()->GetValueByNameString(Arg);
		if (Shape != INDEX_NONE)
		{
			OutShape = static_cast<ENRGridLayoutShape>(Shape);
			return true;
		}
		const int64 Environment = StaticEnum<ENRMapEnvironmentMode>()->GetValueByNameString(
			Arg.Equals(TEXT("Indoor"), ESearchCase::IgnoreCase) ? TEXT("IndoorOnly") : Arg.Equals(TEXT("Outdoor"), ESearchCase::IgnoreCase) ? TEXT("OutdoorOnly") : *Arg);
		if (Environment != INDEX_NONE)
		{
			OutEnvironment = static_cast<ENRMapEnvironmentMode>(Environment);
			return true;
		}
		return false;
	}

	/**
	 * NR.Grid.Start [seed] [Linear|Branching|Loop|Mixed] [IndoorOnly|OutdoorOnly|Hybrid] [Split|Flood] [shuffle]:
	 * build a map; seed 0 or none for a random one; shape, environment mode and Hybrid assignment
	 * the Config's unless given.
	 */
	void Start(const TArray<FString>& Args, UWorld* World)
	{
		ANRMapGenerator* Generator = World ? Find(World) : nullptr;
		if (!Generator)
		{
			return;
		}
		int32 InSeed = 0;
		bool bShuffle = false;
		TOptional<ENRGridLayoutShape> Shape;
		TOptional<ENRMapEnvironmentMode> Environment;
		TOptional<ENRHybridAssignment> Assignment;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("shuffle"), ESearchCase::IgnoreCase))
			{
				bShuffle = true;
			}
			else if (!ParseChoice(Arg, Shape, Environment, Assignment))
			{
				if (!Arg.IsNumeric())
				{
					UE_LOG(LogNRMapGen, Warning, TEXT("NR.Grid.Start: %s is not a seed, a shape (Linear, Branching, Loop, Mixed) or an environment mode (IndoorOnly, OutdoorOnly, Hybrid)"), *Arg);
					return;
				}
				InSeed = FCString::Atoi(*Arg);
			}
		}
		Generator->SetShuffleLoadOrder(bShuffle);
		Generator->SetDebugMissingRoom(INDEX_NONE);
		Generator->StartMapWithChoices(InSeed, Shape, Environment, Assignment);
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
		const FVector Target = Generator->GetRoomCenter(*Room) + Offset;
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
		const FNRMapGenSettings Settings = Generator->GetEffectiveSettings();
		const TArray<FNRRoomTemplateDesc> Templates = Generator->DescribeTemplates();
		const TArray<FNRCorridorTemplateDesc> Corridors = Generator->DescribeCorridors();
		const int32 From = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1;
		const int32 Count = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 500;
		int32 MostRooms = INDEX_NONE;
		int32 MostLoops = INDEX_NONE;
		for (int32 Seed = From; Seed < From + Count && (MostRooms == INDEX_NONE || MostLoops == INDEX_NONE); ++Seed)
		{
			FNRGeneratedLayout Layout;
			FString Error;
			if (!FNRGridLayoutGenerator::Generate(Settings, Templates, Corridors, Seed, Layout, Error))
			{
				continue;
			}
			if (MostRooms == INDEX_NONE && Layout.Rooms.Num() - FNRGridLayoutGenerator::CountHalls(Layout) == Settings.MaxRooms)
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

	/**
	 * NR.Grid.Survey [count] [from]: generates Hybrid maps with the config and counts, for every
	 * Transition cell, the sides its Indoor corridors leave from. Measures a candidate rule: a
	 * Transition cell's Indoor corridors fit one corner (an L: at most two sides, never opposite).
	 * Read-only: no map is loaded.
	 */
	void Survey(const TArray<FString>& Args, UWorld* World)
	{
		const ANRMapGenerator* Generator = World ? Find(World) : nullptr;
		if (!Generator)
		{
			return;
		}
		FNRMapGenSettings Settings = Generator->GetEffectiveSettings();
		Settings.EnvironmentMode = ENRMapEnvironmentMode::Hybrid;
		const TArray<FNRRoomTemplateDesc> Templates = Generator->DescribeTemplates();
		const TArray<FNRCorridorTemplateDesc> Corridors = Generator->DescribeCorridors();
		const int32 Count = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 400;
		const int32 From = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 1;

		// Indoor sides of one Transition cell: 0, 1, 2 at a corner, 2 opposite, 3, 4.
		enum EPattern { None, One, Corner, Opposite, Three, Four, PatternCount };
		const TCHAR* PatternNames[PatternCount] = { TEXT("0"), TEXT("1"), TEXT("2 corner (L)"), TEXT("2 opposite"), TEXT("3"), TEXT("4") };
		int32 Cells[2][PatternCount] = {};
		int32 Maps = 0, Failed = 0, Tries = 0, LoosePass = 0, StrictPass = 0, TransitionCells = 0;
		// How a Transition cell's Indoor and Outdoor corridors sit against each other.
		enum EFace { FaceAcross, FaceAtAngle, FaceIndoorOnly, FaceOutdoorOnly, FaceNeither, FaceCount };
		const TCHAR* FaceNames[FaceCount] = { TEXT("Indoor across Outdoor"), TEXT("Indoor at right angles only"), TEXT("Indoor only"), TEXT("Outdoor only"), TEXT("neither") };
		int32 Facing[FaceCount] = {};
		int32 FaceLoosePass = 0, FaceStrictPass = 0;
		for (int32 Seed = From; Seed < From + Count; ++Seed)
		{
			FNRGeneratedLayout Layout;
			FString Error;
			if (!FNRGridLayoutGenerator::Generate(Settings, Templates, Corridors, Seed, Layout, Error))
			{
				++Failed;
				Tries += Settings.MaxAttempts;
				continue;
			}
			++Maps;
			Tries += Layout.Attempt + 1;
			bool bLoose = true;
			bool bStrict = true;
			bool bFaceLoose = true;
			bool bFaceStrict = true;
			for (const FNRGeneratedRoom& Room : Layout.Rooms)
			{
				if (Room.Environment != ENRRoomEnvironment::Transition)
				{
					continue;
				}
				++TransitionCells;
				uint8 Indoor = 0;
				uint8 Outdoor = 0;
				for (const FNRGeneratedConnection& Connection : Layout.Connections)
				{
					const bool bA = Connection.RoomA == Room.RoomId;
					if (!bA && Connection.RoomB != Room.RoomId)
					{
						continue;
					}
					const FNRGeneratedRoom* Other = Layout.FindRoom(bA ? Connection.RoomB : Connection.RoomA);
					if (!Other)
					{
						continue;
					}
					// Another Transition place counts as Outdoor: the corridor to it is an Outdoor one.
					const FIntPoint Step = bA ? Connection.CellB - Connection.CellA : Connection.CellA - Connection.CellB;
					const int32 Side = Step.Y > 0 ? 0 : Step.X > 0 ? 1 : Step.Y < 0 ? 2 : 3; // N, E, S, W
					(Other->Environment == ENRRoomEnvironment::Indoor ? Indoor : Outdoor) |= 1 << Side;
				}
				// Facing: some Indoor side has an Outdoor side straight across (sides two steps apart).
				const uint8 Across = static_cast<uint8>(((Outdoor << 2) | (Outdoor >> 2)) & 0b1111);
				const bool bFacing = (Indoor & Across) != 0;
				const int32 Face = !Indoor && !Outdoor ? FaceNeither : !Outdoor ? FaceIndoorOnly : !Indoor ? FaceOutdoorOnly : bFacing ? FaceAcross : FaceAtAngle;
				++Facing[Face];
				bFaceLoose &= !(Indoor && Outdoor) || bFacing;
				bFaceStrict &= FMath::CountBits(Indoor) == 1 && FMath::CountBits(Outdoor) == 1 && bFacing;
				const int32 Sides = FMath::CountBits(Indoor);
				const bool bOpposite = Indoor == 0b0101 || Indoor == 0b1010;
				const EPattern Pattern = Sides == 0 ? None : Sides == 1 ? One : Sides == 2 ? (bOpposite ? Opposite : Corner) : Sides == 3 ? Three : Four;
				++Cells[Room.RoomType == ENRGridRoomType::Hall ? 1 : 0][Pattern];
				bLoose &= Sides <= 2 && !bOpposite;
				bStrict &= Pattern == Corner;
			}
			LoosePass += bLoose;
			StrictPass += bStrict;
			FaceLoosePass += bFaceLoose;
			FaceStrictPass += bFaceStrict;
		}

		// A map fails only when all MaxAttempts tries fail. With the rule as one more check on a
		// finished try, a try succeeds with (maps / tries) * (share of maps passing the rule).
		const double TrySuccess = Tries > 0 ? static_cast<double>(Maps) / Tries : 0.0;
		auto FailAfterAll = [&](int32 Pass)
		{
			const double P = Maps > 0 ? TrySuccess * Pass / Maps : 0.0;
			return 100.0 * FMath::Pow(1.0 - P, static_cast<double>(Settings.MaxAttempts));
		};
		UE_LOG(LogNRMapGen, Log, TEXT("[Survey] seeds %d..%d: %d maps, %d failed, %d tries (%.1f%% of tries succeed), %d Transition cells"),
			From, From + Count - 1, Maps, Failed, Tries, 100.0 * TrySuccess, TransitionCells);
		for (int32 Pattern = 0; Pattern < PatternCount; ++Pattern)
		{
			UE_LOG(LogNRMapGen, Log, TEXT("[Survey]   Indoor sides %-13s: rooms %4d, hall cells %4d"), PatternNames[Pattern], Cells[0][Pattern], Cells[1][Pattern]);
		}
		UE_LOG(LogNRMapGen, Log, TEXT("[Survey] rule 'Indoor sides fit one corner' (<= 2, not opposite): %d/%d maps pass (%.1f%%), est. map failure after %d tries %.2f%%"),
			LoosePass, Maps, Maps ? 100.0 * LoosePass / Maps : 0.0, Settings.MaxAttempts, FailAfterAll(LoosePass));
		UE_LOG(LogNRMapGen, Log, TEXT("[Survey] rule 'Indoor sides exactly one L' (2 at a corner): %d/%d maps pass (%.1f%%), est. map failure after %d tries %.2f%%"),
			StrictPass, Maps, Maps ? 100.0 * StrictPass / Maps : 0.0, Settings.MaxAttempts, FailAfterAll(StrictPass));
		for (int32 Face = 0; Face < FaceCount; ++Face)
		{
			UE_LOG(LogNRMapGen, Log, TEXT("[Survey]   %-28s: %4d cells"), FaceNames[Face], Facing[Face]);
		}
		UE_LOG(LogNRMapGen, Log, TEXT("[Survey] rule 'touching both, Indoor faces Outdoor': %d/%d maps pass (%.1f%%), est. map failure after %d tries %.2f%%"),
			FaceLoosePass, Maps, Maps ? 100.0 * FaceLoosePass / Maps : 0.0, Settings.MaxAttempts, FailAfterAll(FaceLoosePass));
		UE_LOG(LogNRMapGen, Log, TEXT("[Survey] rule 'every cell: one Indoor straight across one Outdoor': %d/%d maps pass (%.1f%%), est. map failure after %d tries %.2f%%"),
			FaceStrictPass, Maps, Maps ? 100.0 * FaceStrictPass / Maps : 0.0, Settings.MaxAttempts, FailAfterAll(FaceStrictPass));
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

	/** NR.Grid.NavProbe: for every room, whether its centre is on the navmesh and how far a path
	 *  from the start room reaches; for every corridor, whether its two cells are joined. Works
	 *  while the generator still waits for navigation, to see where the map falls apart. */
	void NavProbe(const TArray<FString>& Args, UWorld* World)
	{
		const ANRMapGenerator* Generator = World ? Find(World) : nullptr;
		UNavigationSystemV1* Navigation = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (!Generator || !Navigation)
		{
			return;
		}
		const FNRGeneratedLayout& Layout = Generator->GetActiveLayout();
		const FNRGeneratedRoom* Start = Layout.FindRoom(Layout.StartRoomId);
		if (!Start)
		{
			return;
		}
		const FVector Up(0.0f, 0.0f, 50.0f);
		UE_LOG(LogNRMapGen, Log, TEXT("Nav probe (building %d):"), Navigation->IsNavigationBuildInProgress() ? 1 : 0);
		for (const FNRGeneratedRoom& Room : Layout.Rooms)
		{
			const FVector Center = Generator->GetRoomCenter(Room) + Up;
			FNavLocation OnMesh;
			const bool bOnMesh = Navigation->ProjectPointToNavigation(Center, OnMesh, FVector(300.0f, 300.0f, 300.0f));
			const UNavigationPath* Path = Navigation->FindPathToLocationSynchronously(World, Generator->GetRoomCenter(*Start) + Up, Center);
			const bool bWhole = Path && Path->IsValid() && !Path->IsPartial();
			const FVector End = Path && Path->PathPoints.Num() > 0 ? Path->PathPoints.Last() : FVector::ZeroVector;
			UE_LOG(LogNRMapGen, Log, TEXT("  room %d (template %d, cell %d,%d): centre %s navmesh, path from start %s, ends at %s"), Room.RoomId,
				Room.TemplateIndex, Room.Cell.X, Room.Cell.Y, bOnMesh ? TEXT("on") : TEXT("OFF"), bWhole ? TEXT("whole") : TEXT("PARTIAL"),
				*End.ToCompactString());
		}
		for (const FNRGeneratedConnection& Connection : Layout.Connections)
		{
			const UNavigationPath* Path = Navigation->FindPathToLocationSynchronously(World, Generator->GetCellCenter(Connection.CellA) + Up,
				Generator->GetCellCenter(Connection.CellB) + Up);
			if (!(Path && Path->IsValid() && !Path->IsPartial()))
			{
				UE_LOG(LogNRMapGen, Warning, TEXT("  corridor %d-%d: no whole path between cells %d,%d and %d,%d"), Connection.RoomA, Connection.RoomB,
					Connection.CellA.X, Connection.CellA.Y, Connection.CellB.X, Connection.CellB.Y);
			}
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

	/** NR.Grid.Generate [seed] [Linear|Branching|Loop|Mixed] [IndoorOnly|OutdoorOnly|Hybrid]: graph only, drawn for a minute. */
	void Generate(const TArray<FString>& Args, UWorld* World)
	{
		ANRMapGenerator* Generator = World ? Find(World) : nullptr;
		if (!Generator)
		{
			return;
		}
		const int32 InSeed = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : FMath::Rand();
		TOptional<ENRGridLayoutShape> Shape;
		TOptional<ENRMapEnvironmentMode> Environment;
		TOptional<ENRHybridAssignment> Assignment;
		for (int32 Index = 1; Index < Args.Num(); ++Index)
		{
			if (!ParseChoice(Args[Index], Shape, Environment, Assignment))
			{
				UE_LOG(LogNRMapGen, Warning, TEXT("Unknown shape, environment mode or assignment %s: Linear, Branching, Loop, Mixed, IndoorOnly, OutdoorOnly, Hybrid, Split or Flood"), *Args[Index]);
				return;
			}
		}
		FString Error;
		if (Generator->GenerateLayout(InSeed, Error, Shape, Environment, Assignment))
		{
			Generator->DrawLayout(Generator->GetGeneratedLayout(), 60.0f);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRGridStartCommand(TEXT("NR.Grid.Start"),
	TEXT("NR.Grid.Start [seed] [Linear|Branching|Loop|Mixed] [IndoorOnly|OutdoorOnly|Hybrid] [Split|Flood] [shuffle]: generate and load a map (seed 0 or none for random; shape, environment mode and Hybrid assignment the config's unless given; shuffle requests modules in random order)."),
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

static FAutoConsoleCommandWithWorldAndArgs GNRGridSurveyCommand(TEXT("NR.Grid.Survey"),
	TEXT("NR.Grid.Survey [count] [from]: generate Hybrid maps (nothing loaded) and log how Transition cells meet Indoor, and how often an L-corner rule would pass."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::Survey));

static FAutoConsoleCommandWithWorldAndArgs GNRGridTestMissingCommand(TEXT("NR.Grid.TestMissing"),
	TEXT("NR.Grid.TestMissing <seed> [roomId]: load a map with one room's level missing; it must fail as a whole."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::TestMissing));

static FAutoConsoleCommandWithWorldAndArgs GNRGridUnloadCommand(TEXT("NR.Grid.Unload"),
	TEXT("Unload every module the map generator loaded."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::Unload));

static FAutoConsoleCommandWithWorldAndArgs GNRGridStatusCommand(TEXT("NR.Grid.Status"),
	TEXT("Log the map generator's modules, collision and navigation checks and placement signature."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::Status));

static FAutoConsoleCommandWithWorldAndArgs GNRGridNavProbeCommand(TEXT("NR.Grid.NavProbe"),
	TEXT("Log, per room, whether its centre is on the navmesh and reachable from the start room, and every corridor whose cells are not joined."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::NavProbe));

static FAutoConsoleCommandWithWorldAndArgs GNRGridSignatureCommand(TEXT("NR.Grid.Signature"),
	TEXT("Log the full placement text of the loaded map."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::Signature));

static FAutoConsoleCommandWithWorldAndArgs GNRGridGenerateCommand(TEXT("NR.Grid.Generate"),
	TEXT("NR.Grid.Generate [seed] [Linear|Branching|Loop|Mixed] [IndoorOnly|OutdoorOnly|Hybrid] [Split|Flood]: generate a layout graph, log it and draw it for a minute."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRGridCommands::Generate));
