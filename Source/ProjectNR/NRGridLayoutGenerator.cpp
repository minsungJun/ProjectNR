// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRGridLayoutGenerator.h"

#include "Math/RandomStream.h"

namespace NRGridGen
{
	int32 RequiredMainPath(const FNRMapGenSettings& Settings)
	{
		return 2 + (Settings.bRequireSafe ? 1 : 0) + (Settings.bRequireElite ? 1 : 0);
	}

	bool NeedsBranch(ENRGridLayoutShape Shape)
	{
		return Shape == ENRGridLayoutShape::Branching || Shape == ENRGridLayoutShape::Mixed;
	}

	bool NeedsLoop(ENRGridLayoutShape Shape)
	{
		return Shape == ENRGridLayoutShape::Loop || Shape == ENRGridLayoutShape::Mixed;
	}

	/** Main path length range the shape allows: [OutMin, OutMax]. */
	void MainPathRange(const FNRMapGenSettings& Settings, int32& OutMin, int32& OutMax)
	{
		OutMin = FMath::Max(Settings.MinMainPathLength, RequiredMainPath(Settings));
		OutMax = Settings.MaxMainPathLength;
		if (Settings.Shape == ENRGridLayoutShape::Linear)
		{
			// Every room is on the main path.
			OutMin = FMath::Max(OutMin, Settings.MinRooms);
			OutMax = FMath::Min(OutMax, Settings.MaxRooms);
		}
		else
		{
			OutMax = FMath::Min(OutMax, Settings.MaxRooms - (NeedsBranch(Settings.Shape) ? 1 : 0));
		}
	}

	bool InGrid(const FNRMapGenSettings& Settings, const FIntPoint& Cell)
	{
		return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Settings.GridSize.X && Cell.Y < Settings.GridSize.Y;
	}

	/** Adjacency lists by room index, built from the corridors. */
	TArray<TArray<int32>> Adjacency(const FNRGeneratedLayout& Layout, const TMap<int32, int32>& IndexById)
	{
		TArray<TArray<int32>> Neighbours;
		Neighbours.SetNum(Layout.Rooms.Num());
		for (const FNRGeneratedConnection& Connection : Layout.Connections)
		{
			const int32* A = IndexById.Find(Connection.RoomA);
			const int32* B = IndexById.Find(Connection.RoomB);
			if (A && B)
			{
				Neighbours[*A].Add(*B);
				Neighbours[*B].Add(*A);
			}
		}
		return Neighbours;
	}

	/** Whether Goal can be reached from From without passing through Avoid (INDEX_NONE avoids nothing). */
	bool Reaches(const TArray<TArray<int32>>& Neighbours, int32 From, int32 Goal, int32 Avoid)
	{
		if (From == Avoid || !Neighbours.IsValidIndex(From) || !Neighbours.IsValidIndex(Goal))
		{
			return false;
		}
		TArray<bool> Seen;
		Seen.Init(false, Neighbours.Num());
		TArray<int32> Queue = { From };
		Seen[From] = true;
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 Current = Queue[Head];
			if (Current == Goal)
			{
				return true;
			}
			for (int32 Next : Neighbours[Current])
			{
				if (!Seen[Next] && Next != Avoid)
				{
					Seen[Next] = true;
					Queue.Add(Next);
				}
			}
		}
		return false;
	}

	TMap<int32, int32> IndexRooms(const FNRGeneratedLayout& Layout)
	{
		TMap<int32, int32> IndexById;
		for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
		{
			IndexById.Add(Layout.Rooms[Index].RoomId, Index);
		}
		return IndexById;
	}

	int32 FindOfType(const FNRGeneratedLayout& Layout, ENRGridRoomType Type)
	{
		return Layout.Rooms.IndexOfByPredicate([Type](const FNRGeneratedRoom& Room) { return Room.RoomType == Type; });
	}

	void AddConnection(FNRGeneratedLayout& Layout, int32 A, int32 B)
	{
		FNRGeneratedConnection& Connection = Layout.Connections.AddDefaulted_GetRef();
		Connection.RoomA = FMath::Min(A, B);
		Connection.RoomB = FMath::Max(A, B);
	}

	bool HasConnection(const FNRGeneratedLayout& Layout, int32 A, int32 B)
	{
		const int32 Low = FMath::Min(A, B);
		const int32 High = FMath::Max(A, B);
		return Layout.Connections.ContainsByPredicate([Low, High](const FNRGeneratedConnection& Connection)
		{
			return Connection.RoomA == Low && Connection.RoomB == High;
		});
	}

	const TCHAR* ShapeName(ENRGridLayoutShape Shape)
	{
		switch (Shape)
		{
		case ENRGridLayoutShape::Linear: return TEXT("Linear");
		case ENRGridLayoutShape::Branching: return TEXT("Branching");
		case ENRGridLayoutShape::Loop: return TEXT("Loop");
		case ENRGridLayoutShape::Mixed:
		default: return TEXT("Mixed");
		}
	}
}

bool FNRGridLayoutGenerator::CheckSettings(const FNRMapGenSettings& Settings, FString& OutError)
{
	using namespace NRGridGen;
	const int32 Cells = Settings.GridSize.X * Settings.GridSize.Y;
	int32 PathMin = 0;
	int32 PathMax = 0;
	MainPathRange(Settings, PathMin, PathMax);

	if (Settings.GridSize.X < 1 || Settings.GridSize.Y < 1)
	{
		OutError = TEXT("grid size must be at least 1x1");
	}
	else if (Settings.MinRooms > Settings.MaxRooms)
	{
		OutError = FString::Printf(TEXT("MinRooms %d is more than MaxRooms %d"), Settings.MinRooms, Settings.MaxRooms);
	}
	else if (Settings.MinRooms > Cells)
	{
		OutError = FString::Printf(TEXT("%d rooms cannot fit a %dx%d grid"), Settings.MinRooms, Settings.GridSize.X, Settings.GridSize.Y);
	}
	else if (Settings.MaxMainPathLength < RequiredMainPath(Settings))
	{
		OutError = FString::Printf(TEXT("a main path of at most %d rooms cannot hold start, %sboss"),
			Settings.MaxMainPathLength,
			Settings.bRequireSafe && Settings.bRequireElite ? TEXT("safe, elite and ") : Settings.bRequireSafe ? TEXT("safe and ")
			: Settings.bRequireElite ? TEXT("elite and ") : TEXT(""));
	}
	else if (PathMin > PathMax)
	{
		OutError = Settings.Shape == ENRGridLayoutShape::Linear
			? FString::Printf(TEXT("a Linear map puts every room on the main path, but rooms %d-%d and main path %d-%d do not overlap"),
				Settings.MinRooms, Settings.MaxRooms, Settings.MinMainPathLength, Settings.MaxMainPathLength)
			: FString::Printf(TEXT("no main path length fits: %d-%d with at most %d rooms%s"),
				Settings.MinMainPathLength, Settings.MaxMainPathLength, Settings.MaxRooms,
				NeedsBranch(Settings.Shape) ? TEXT(" and one branch room") : TEXT(""));
	}
	else if (PathMin > Cells)
	{
		OutError = FString::Printf(TEXT("a main path of %d rooms cannot fit a %dx%d grid"), PathMin, Settings.GridSize.X, Settings.GridSize.Y);
	}
	else if (NeedsLoop(Settings.Shape) && Settings.MinLoops < 1)
	{
		OutError = FString::Printf(TEXT("a %s map needs at least one loop, but MinLoops is %d"), ShapeName(Settings.Shape), Settings.MinLoops);
	}
	else if (NeedsLoop(Settings.Shape) && Settings.MinLoops > Settings.MaxLoops)
	{
		OutError = FString::Printf(TEXT("MinLoops %d is more than MaxLoops %d"), Settings.MinLoops, Settings.MaxLoops);
	}
	else if (NeedsLoop(Settings.Shape) && (Settings.GridSize.X < 2 || Settings.GridSize.Y < 2 || Settings.MaxRooms < 4))
	{
		OutError = TEXT("a loop needs at least four rooms in a 2x2 square");
	}
	else
	{
		return true;
	}
	return false;
}

bool FNRGridLayoutGenerator::Generate(const FNRMapGenSettings& Settings, int32 Seed, FNRGeneratedLayout& OutLayout, FString& OutError)
{
	if (!CheckSettings(Settings, OutError))
	{
		OutError = FString::Printf(TEXT("settings: %s"), *OutError);
		return false;
	}

	TMap<FString, int32> Reasons;
	for (int32 Attempt = 0; Attempt < Settings.MaxAttempts; ++Attempt)
	{
		const int32 AttemptSeed = Attempt == 0 ? Seed : static_cast<int32>(HashCombine(GetTypeHash(Seed), GetTypeHash(Attempt)));
		FNRGeneratedLayout Layout;
		FString Reason;
		if (TryGenerate(Settings, AttemptSeed, Layout, Reason))
		{
			Layout.Seed = Seed;
			Layout.Attempt = Attempt;
			OutLayout = MoveTemp(Layout);
			return true;
		}
		++Reasons.FindOrAdd(Reason);
	}

	TArray<FString> Parts;
	for (const TPair<FString, int32>& Pair : Reasons)
	{
		Parts.Add(FString::Printf(TEXT("%s x%d"), *Pair.Key, Pair.Value));
	}
	Parts.Sort();
	OutError = FString::Printf(TEXT("no valid %s layout for seed %d in %d attempts: %s"),
		NRGridGen::ShapeName(Settings.Shape), Seed, Settings.MaxAttempts, *FString::Join(Parts, TEXT("; ")));
	return false;
}

bool FNRGridLayoutGenerator::TryGenerate(const FNRMapGenSettings& Settings, int32 AttemptSeed, FNRGeneratedLayout& OutLayout, FString& OutReason)
{
	using namespace NRGridGen;
	FRandomStream Stream(AttemptSeed);

	int32 PathMin = 0;
	int32 PathMax = 0;
	MainPathRange(Settings, PathMin, PathMax);
	const int32 PathLength = Stream.RandRange(PathMin, PathMax);
	const int32 TargetRooms = Settings.Shape == ENRGridLayoutShape::Linear
		? PathLength
		: Stream.RandRange(FMath::Max(Settings.MinRooms, PathLength + (NeedsBranch(Settings.Shape) ? 1 : 0)), Settings.MaxRooms);

	// 1. Main path: depth-first self-avoiding walk, directions shuffled per cell.
	struct FFrame
	{
		FIntPoint Cell;
		int32 Order[4] = { 0, 1, 2, 3 };
		int32 Next = 0;
	};
	auto MakeFrame = [&Stream](const FIntPoint& Cell)
	{
		FFrame Frame;
		Frame.Cell = Cell;
		for (int32 Index = 3; Index > 0; --Index)
		{
			Swap(Frame.Order[Index], Frame.Order[Stream.RandRange(0, Index)]);
		}
		return Frame;
	};

	TSet<FIntPoint> Occupied;
	TArray<FFrame> Path;
	const FIntPoint StartCell(Stream.RandRange(0, Settings.GridSize.X - 1), Stream.RandRange(0, Settings.GridSize.Y - 1));
	Path.Add(MakeFrame(StartCell));
	Occupied.Add(StartCell);
	int32 Budget = Settings.MainPathSearchBudget;
	while (Path.Num() < PathLength)
	{
		if (Path.Num() == 0 || --Budget < 0)
		{
			OutReason = TEXT("main path walk ran out of room");
			return false;
		}
		FFrame& Top = Path.Last();
		if (Top.Next >= 4)
		{
			Occupied.Remove(Top.Cell);
			Path.Pop();
			continue;
		}
		const FIntPoint Next = Top.Cell + NRGrid::ToOffset(static_cast<ENRGridDirection>(Top.Order[Top.Next++]));
		if (InGrid(Settings, Next) && !Occupied.Contains(Next))
		{
			Occupied.Add(Next);
			Path.Add(MakeFrame(Next));
		}
	}

	FNRGeneratedLayout Layout;
	Layout.GeneratorVersion = Version;
	TArray<int32> Depth;
	TMap<FIntPoint, int32> RoomAt;
	for (int32 Index = 0; Index < PathLength; ++Index)
	{
		FNRGeneratedRoom& Room = Layout.Rooms.AddDefaulted_GetRef();
		Room.RoomId = Index;
		Room.Cell = Path[Index].Cell;
		Room.MainPathIndex = Index;
		Depth.Add(0);
		RoomAt.Add(Room.Cell, Index);
		if (Index > 0)
		{
			AddConnection(Layout, Index - 1, Index);
		}
	}
	const int32 BossIndex = PathLength - 1;
	Layout.Rooms[0].RoomType = ENRGridRoomType::Start;
	Layout.Rooms[BossIndex].RoomType = ENRGridRoomType::Boss;
	if (Settings.bRequireElite)
	{
		Layout.Rooms[BossIndex - 1].RoomType = ENRGridRoomType::Elite;
	}
	if (Settings.bRequireSafe)
	{
		// Middle of what lies between the start and the elite (or the boss).
		const int32 LastFree = BossIndex - (Settings.bRequireElite ? 2 : 1);
		Layout.Rooms[FMath::Clamp((1 + LastFree) / 2, 1, LastFree)].RoomType = ENRGridRoomType::Safe;
	}
	Layout.StartRoomId = 0;
	Layout.BossRoomId = BossIndex;

	// 2. Branches off any room but the boss, into free neighbouring cells.
	while (Layout.Rooms.Num() < TargetRooms)
	{
		TArray<TPair<int32, FIntPoint>> Candidates;
		for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
		{
			if (Index == BossIndex || Depth[Index] >= Settings.MaxBranchDepth)
			{
				continue;
			}
			for (int32 Dir = 0; Dir < 4; ++Dir)
			{
				const FIntPoint Cell = Layout.Rooms[Index].Cell + NRGrid::ToOffset(static_cast<ENRGridDirection>(Dir));
				if (InGrid(Settings, Cell) && !RoomAt.Contains(Cell))
				{
					Candidates.Emplace(Index, Cell);
				}
			}
		}
		if (Candidates.Num() == 0)
		{
			OutReason = TEXT("no free cell left for a branch");
			return false;
		}

		const TPair<int32, FIntPoint>& Pick = Candidates[Stream.RandRange(0, Candidates.Num() - 1)];
		const int32 NewIndex = Layout.Rooms.Num();
		FNRGeneratedRoom& Room = Layout.Rooms.AddDefaulted_GetRef();
		Room.RoomId = NewIndex;
		Room.Cell = Pick.Value;
		Depth.Add(Depth[Pick.Key] + 1);
		RoomAt.Add(Room.Cell, NewIndex);
		AddConnection(Layout, Pick.Key, NewIndex);
	}

	// 3. Loops: extra corridors between neighbours, never to the boss, never around a
	//    required room.
	if (NeedsLoop(Settings.Shape))
	{
		const int32 SafeIndex = FindOfType(Layout, ENRGridRoomType::Safe);
		const int32 EliteIndex = FindOfType(Layout, ENRGridRoomType::Elite);
		const int32 LoopTarget = Stream.RandRange(Settings.MinLoops, Settings.MaxLoops);
		for (int32 Loop = 0; Loop < LoopTarget; ++Loop)
		{
			TArray<TPair<int32, int32>> Candidates;
			for (int32 A = 0; A < Layout.Rooms.Num(); ++A)
			{
				for (int32 Dir = 0; Dir < 2; ++Dir)
				{
					// North and East only, so each neighbouring pair is seen once.
					const FIntPoint Cell = Layout.Rooms[A].Cell + NRGrid::ToOffset(Dir == 0 ? ENRGridDirection::North : ENRGridDirection::East);
					const int32* B = RoomAt.Find(Cell);
					if (!B || A == BossIndex || *B == BossIndex || HasConnection(Layout, A, *B))
					{
						continue;
					}

					FNRGeneratedLayout Trial = Layout;
					AddConnection(Trial, A, *B);
					const TArray<TArray<int32>> Neighbours = Adjacency(Trial, IndexRooms(Trial));
					if ((SafeIndex != INDEX_NONE && Reaches(Neighbours, 0, BossIndex, SafeIndex))
						|| (EliteIndex != INDEX_NONE && Reaches(Neighbours, 0, BossIndex, EliteIndex)))
					{
						continue;
					}
					Candidates.Emplace(A, *B);
				}
			}
			if (Candidates.Num() == 0)
			{
				if (Loop < Settings.MinLoops)
				{
					OutReason = TEXT("no corridor can close a loop without bypassing a required room");
					return false;
				}
				break;
			}
			const TPair<int32, int32>& Pick = Candidates[Stream.RandRange(0, Candidates.Num() - 1)];
			AddConnection(Layout, Pick.Key, Pick.Value);
		}
	}

	// Doorway masks for template selection.
	for (const FNRGeneratedConnection& Connection : Layout.Connections)
	{
		FNRGeneratedRoom& A = Layout.Rooms[Connection.RoomA];
		FNRGeneratedRoom& B = Layout.Rooms[Connection.RoomB];
		ENRGridDirection Direction;
		if (NRGrid::DirectionBetween(A.Cell, B.Cell, Direction))
		{
			A.ConnectionMask |= 1 << static_cast<int32>(Direction);
			B.ConnectionMask |= 1 << static_cast<int32>(NRGrid::Opposite(Direction));
		}
	}

	const TArray<FString> Problems = Validate(Settings, Layout);
	if (Problems.Num() > 0)
	{
		OutReason = FString::Printf(TEXT("invalid result: %s"), *Problems[0]);
		return false;
	}
	OutLayout = MoveTemp(Layout);
	return true;
}

TArray<FString> FNRGridLayoutGenerator::Validate(const FNRMapGenSettings& Settings, const FNRGeneratedLayout& Layout)
{
	using namespace NRGridGen;
	TArray<FString> Problems;
	const TMap<int32, int32> IndexById = IndexRooms(Layout);

	if (IndexById.Num() != Layout.Rooms.Num())
	{
		Problems.Add(TEXT("duplicate room id"));
	}
	TSet<FIntPoint> Cells;
	for (const FNRGeneratedRoom& Room : Layout.Rooms)
	{
		bool bDuplicate = false;
		Cells.Add(Room.Cell, &bDuplicate);
		if (bDuplicate)
		{
			Problems.Add(FString::Printf(TEXT("two rooms in cell %d,%d"), Room.Cell.X, Room.Cell.Y));
		}
		if (!InGrid(Settings, Room.Cell))
		{
			Problems.Add(FString::Printf(TEXT("room %d outside the grid"), Room.RoomId));
		}
	}
	if (Layout.Rooms.Num() < Settings.MinRooms || Layout.Rooms.Num() > Settings.MaxRooms)
	{
		Problems.Add(FString::Printf(TEXT("%d rooms, wanted %d-%d"), Layout.Rooms.Num(), Settings.MinRooms, Settings.MaxRooms));
	}

	// Corridors.
	TSet<TPair<int32, int32>> Pairs;
	for (const FNRGeneratedConnection& Connection : Layout.Connections)
	{
		const int32* A = IndexById.Find(Connection.RoomA);
		const int32* B = IndexById.Find(Connection.RoomB);
		ENRGridDirection Direction;
		if (!A || !B)
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d to a missing room"), Connection.RoomA, Connection.RoomB));
			continue;
		}
		if (Connection.RoomA >= Connection.RoomB)
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d not in order"), Connection.RoomA, Connection.RoomB));
		}
		if (!NRGrid::DirectionBetween(Layout.Rooms[*A].Cell, Layout.Rooms[*B].Cell, Direction))
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d joins rooms that are not neighbours"), Connection.RoomA, Connection.RoomB));
		}
		bool bDuplicate = false;
		Pairs.Add(TPair<int32, int32>(Connection.RoomA, Connection.RoomB), &bDuplicate);
		if (bDuplicate)
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d twice"), Connection.RoomA, Connection.RoomB));
		}
	}

	// Doorway masks agree with the corridors.
	TArray<uint8> Masks;
	Masks.Init(0, Layout.Rooms.Num());
	for (const FNRGeneratedConnection& Connection : Layout.Connections)
	{
		const int32* A = IndexById.Find(Connection.RoomA);
		const int32* B = IndexById.Find(Connection.RoomB);
		ENRGridDirection Direction;
		if (A && B && NRGrid::DirectionBetween(Layout.Rooms[*A].Cell, Layout.Rooms[*B].Cell, Direction))
		{
			Masks[*A] |= 1 << static_cast<int32>(Direction);
			Masks[*B] |= 1 << static_cast<int32>(NRGrid::Opposite(Direction));
		}
	}
	for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
	{
		if (Layout.Rooms[Index].ConnectionMask != Masks[Index])
		{
			Problems.Add(FString::Printf(TEXT("room %d doorway mask does not match its corridors"), Layout.Rooms[Index].RoomId));
		}
	}

	// Room types.
	auto Count = [&Layout](ENRGridRoomType Type)
	{
		return Layout.Rooms.FilterByPredicate([Type](const FNRGeneratedRoom& Room) { return Room.RoomType == Type; }).Num();
	};
	const int32* StartIndex = IndexById.Find(Layout.StartRoomId);
	const int32* BossIndex = IndexById.Find(Layout.BossRoomId);
	if (Count(ENRGridRoomType::Start) != 1 || !StartIndex || Layout.Rooms[*StartIndex].RoomType != ENRGridRoomType::Start)
	{
		Problems.Add(TEXT("not exactly one start room, or StartRoomId is not it"));
	}
	if (Count(ENRGridRoomType::Boss) != 1 || !BossIndex || Layout.Rooms[*BossIndex].RoomType != ENRGridRoomType::Boss)
	{
		Problems.Add(TEXT("not exactly one boss room, or BossRoomId is not it"));
	}
	if (Settings.bRequireSafe && Count(ENRGridRoomType::Safe) != 1)
	{
		Problems.Add(TEXT("not exactly one safe room"));
	}
	if (Settings.bRequireElite && Count(ENRGridRoomType::Elite) != 1)
	{
		Problems.Add(TEXT("not exactly one elite room"));
	}
	if (!StartIndex || !BossIndex)
	{
		return Problems;
	}

	// Reachability and the required rooms.
	const TArray<TArray<int32>> Neighbours = Adjacency(Layout, IndexById);
	for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
	{
		if (!Reaches(Neighbours, *StartIndex, Index, INDEX_NONE))
		{
			Problems.Add(FString::Printf(TEXT("room %d cannot be reached from the start"), Layout.Rooms[Index].RoomId));
		}
	}
	if (Neighbours[*BossIndex].Num() != 1)
	{
		Problems.Add(FString::Printf(TEXT("boss has %d corridors, wanted 1"), Neighbours[*BossIndex].Num()));
	}
	for (ENRGridRoomType Required : { ENRGridRoomType::Safe, ENRGridRoomType::Elite })
	{
		const int32 RequiredIndex = FindOfType(Layout, Required);
		if (RequiredIndex != INDEX_NONE && Reaches(Neighbours, *StartIndex, *BossIndex, RequiredIndex))
		{
			Problems.Add(FString::Printf(TEXT("the boss can be reached without passing the %s room"),
				Required == ENRGridRoomType::Safe ? TEXT("safe") : TEXT("elite")));
		}
	}
	if (Settings.bRequireElite && Neighbours[*BossIndex].Num() == 1
		&& Layout.Rooms[Neighbours[*BossIndex][0]].RoomType != ENRGridRoomType::Elite)
	{
		Problems.Add(TEXT("the boss does not hang off the elite room"));
	}

	// Main path: 0..N-1 in order, each joined to the next, start first and boss last.
	TArray<int32> MainPath;
	for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
	{
		const int32 PathIndex = Layout.Rooms[Index].MainPathIndex;
		if (PathIndex != INDEX_NONE)
		{
			if (MainPath.Num() <= PathIndex)
			{
				MainPath.SetNum(PathIndex + 1);
			}
			MainPath[PathIndex] = Index + 1;
		}
	}
	bool bPathOk = MainPath.Num() >= 2;
	for (int32 Step = 0; bPathOk && Step < MainPath.Num(); ++Step)
	{
		bPathOk = MainPath[Step] > 0 && (Step == 0 || Neighbours[MainPath[Step] - 1].Contains(MainPath[Step - 1] - 1));
	}
	if (!bPathOk || MainPath[0] - 1 != *StartIndex || MainPath.Last() - 1 != *BossIndex)
	{
		Problems.Add(TEXT("main path is broken or does not run from start to boss"));
	}
	else if (MainPath.Num() < Settings.MinMainPathLength || MainPath.Num() > Settings.MaxMainPathLength)
	{
		Problems.Add(FString::Printf(TEXT("main path of %d rooms, wanted %d-%d"), MainPath.Num(), Settings.MinMainPathLength, Settings.MaxMainPathLength));
	}

	// Branch depth: every room within MaxBranchDepth corridors of the main path.
	{
		TArray<int32> Distance;
		Distance.Init(MAX_int32, Layout.Rooms.Num());
		TArray<int32> Queue;
		for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
		{
			if (Layout.Rooms[Index].MainPathIndex != INDEX_NONE)
			{
				Distance[Index] = 0;
				Queue.Add(Index);
			}
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			for (int32 Next : Neighbours[Queue[Head]])
			{
				if (Distance[Next] == MAX_int32)
				{
					Distance[Next] = Distance[Queue[Head]] + 1;
					Queue.Add(Next);
				}
			}
		}
		for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
		{
			if (Distance[Index] != MAX_int32 && Distance[Index] > Settings.MaxBranchDepth)
			{
				Problems.Add(FString::Printf(TEXT("room %d is %d rooms off the main path, at most %d allowed"),
					Layout.Rooms[Index].RoomId, Distance[Index], Settings.MaxBranchDepth));
			}
		}
	}

	// Shape.
	const int32 Loops = CountLoops(Layout);
	const int32 Branches = CountBranchRooms(Layout);
	const bool bLoopsOk = NeedsLoop(Settings.Shape) ? (Loops >= Settings.MinLoops && Loops <= Settings.MaxLoops) : Loops == 0;
	const bool bBranchesOk = Settings.Shape == ENRGridLayoutShape::Linear ? Branches == 0
		: NeedsBranch(Settings.Shape) ? Branches >= 1 : true;
	if (!bLoopsOk || !bBranchesOk)
	{
		Problems.Add(FString::Printf(TEXT("%d loop(s) and %d branch room(s) do not make a %s map"), Loops, Branches, ShapeName(Settings.Shape)));
	}
	return Problems;
}

int32 FNRGridLayoutGenerator::CountLoops(const FNRGeneratedLayout& Layout)
{
	return Layout.Rooms.Num() > 0 ? FMath::Max(Layout.Connections.Num() - Layout.Rooms.Num() + 1, 0) : 0;
}

int32 FNRGridLayoutGenerator::CountBranchRooms(const FNRGeneratedLayout& Layout)
{
	return Layout.Rooms.FilterByPredicate([](const FNRGeneratedRoom& Room) { return Room.MainPathIndex == INDEX_NONE; }).Num();
}

FString FNRGridLayoutGenerator::ToAscii(const FNRMapGenSettings& Settings, const FNRGeneratedLayout& Layout)
{
	const int32 Width = Settings.GridSize.X * 2 - 1;
	const int32 Height = Settings.GridSize.Y * 2 - 1;
	TArray<FString> Rows;
	Rows.Init(FString::ChrN(Width, TEXT('.')), Height);
	for (FString& Row : Rows)
	{
		for (int32 X = 1; X < Width; X += 2)
		{
			Row[X] = TEXT(' ');
		}
	}
	for (int32 Y = 1; Y < Height; Y += 2)
	{
		Rows[Y] = FString::ChrN(Width, TEXT(' '));
	}

	// Row 0 of the text is the grid's top (largest Y), so north is up.
	auto At = [&Rows, Height](int32 X, int32 Y) -> TCHAR& { return Rows[Height - 1 - Y][X]; };
	for (const FNRGeneratedRoom& Room : Layout.Rooms)
	{
		TCHAR Mark = Room.MainPathIndex == INDEX_NONE ? TEXT('b') : TEXT('C');
		switch (Room.RoomType)
		{
		case ENRGridRoomType::Start: Mark = TEXT('S'); break;
		case ENRGridRoomType::Safe: Mark = TEXT('F'); break;
		case ENRGridRoomType::Elite: Mark = TEXT('E'); break;
		case ENRGridRoomType::Boss: Mark = TEXT('B'); break;
		default: break;
		}
		At(Room.Cell.X * 2, Room.Cell.Y * 2) = Mark;
	}
	for (const FNRGeneratedConnection& Connection : Layout.Connections)
	{
		const FNRGeneratedRoom* A = Layout.FindRoom(Connection.RoomA);
		const FNRGeneratedRoom* B = Layout.FindRoom(Connection.RoomB);
		if (A && B)
		{
			At(A->Cell.X + B->Cell.X, A->Cell.Y + B->Cell.Y) = A->Cell.X == B->Cell.X ? TEXT('|') : TEXT('-');
		}
	}
	return FString::Join(Rows, TEXT("\n"));
}

bool FNRGridLayoutGenerator::Identical(const FNRGeneratedLayout& A, const FNRGeneratedLayout& B)
{
	if (A.Rooms.Num() != B.Rooms.Num() || A.Connections.Num() != B.Connections.Num()
		|| A.StartRoomId != B.StartRoomId || A.BossRoomId != B.BossRoomId)
	{
		return false;
	}
	for (int32 Index = 0; Index < A.Rooms.Num(); ++Index)
	{
		const FNRGeneratedRoom& RoomA = A.Rooms[Index];
		const FNRGeneratedRoom& RoomB = B.Rooms[Index];
		if (RoomA.RoomId != RoomB.RoomId || RoomA.Cell != RoomB.Cell || RoomA.RoomType != RoomB.RoomType
			|| RoomA.ConnectionMask != RoomB.ConnectionMask || RoomA.MainPathIndex != RoomB.MainPathIndex)
		{
			return false;
		}
	}
	for (int32 Index = 0; Index < A.Connections.Num(); ++Index)
	{
		if (A.Connections[Index].RoomA != B.Connections[Index].RoomA || A.Connections[Index].RoomB != B.Connections[Index].RoomB)
		{
			return false;
		}
	}
	return true;
}

FString FNRGridLayoutGenerator::MaskToString(uint8 Mask)
{
	FString Text;
	for (int32 Dir = 0; Dir < 4; ++Dir)
	{
		Text += (Mask & (1 << Dir)) ? NRGrid::Letter(static_cast<ENRGridDirection>(Dir)) : TEXT("-");
	}
	return Text;
}

bool FNRGridLayoutGenerator::AssignTemplates(const TArray<FNRRoomTemplateDesc>& Templates, FNRGeneratedLayout& Layout, FString& OutError)
{
	// Its own stream, so choosing templates never changes the graph a seed gives.
	FRandomStream Stream(static_cast<int32>(HashCombine(HashCombine(GetTypeHash(Layout.Seed), GetTypeHash(Layout.Attempt)), GetTypeHash(0x7E3A11))));
	const UEnum* TypeEnum = StaticEnum<ENRGridRoomType>();

	for (FNRGeneratedRoom& Room : Layout.Rooms)
	{
		struct FOption
		{
			int32 Template;
			int32 Steps;
			float Weight;
		};
		TArray<FOption> Options;
		float Total = 0.0f;
		for (int32 Index = 0; Index < Templates.Num(); ++Index)
		{
			const FNRRoomTemplateDesc& Template = Templates[Index];
			if (Template.Weight <= 0.0f || !(Template.TypeMask & (1 << static_cast<int32>(Room.RoomType))))
			{
				continue;
			}
			for (int32 Steps = 0; Steps < 4; ++Steps)
			{
				const uint8 Turned = NRGrid::RotateMask(Template.PortMask, Steps);
				if ((Template.RotationMask & (1 << Steps)) && (Turned & Room.ConnectionMask) == Room.ConnectionMask)
				{
					Options.Add({ Index, Steps, Template.Weight });
					Total += Template.Weight;
				}
			}
		}

		if (Options.Num() == 0)
		{
			OutError = FString::Printf(TEXT("no room template fits room %d (%s, doorways %s)"), Room.RoomId,
				*TypeEnum->GetNameStringByValue(static_cast<int64>(Room.RoomType)), *MaskToString(Room.ConnectionMask));
			return false;
		}

		float Roll = Stream.FRand() * Total;
		const FOption* Pick = &Options.Last();
		for (const FOption& Option : Options)
		{
			if (Roll < Option.Weight)
			{
				Pick = &Option;
				break;
			}
			Roll -= Option.Weight;
		}
		Room.TemplateIndex = Pick->Template;
		Room.RotationSteps = Pick->Steps;
	}
	return true;
}

TArray<FString> FNRGridLayoutGenerator::ValidateTemplates(const TArray<FNRRoomTemplateDesc>& Templates, const FNRGeneratedLayout& Layout)
{
	TArray<FString> Problems;
	for (const FNRGeneratedRoom& Room : Layout.Rooms)
	{
		if (!Templates.IsValidIndex(Room.TemplateIndex))
		{
			Problems.Add(FString::Printf(TEXT("room %d has no template"), Room.RoomId));
			continue;
		}
		const FNRRoomTemplateDesc& Template = Templates[Room.TemplateIndex];
		const uint8 Turned = NRGrid::RotateMask(Template.PortMask, Room.RotationSteps);
		if (!(Template.TypeMask & (1 << static_cast<int32>(Room.RoomType))))
		{
			Problems.Add(FString::Printf(TEXT("room %d: template %d does not allow its type"), Room.RoomId, Room.TemplateIndex));
		}
		if (!(Template.RotationMask & (1 << Room.RotationSteps)))
		{
			Problems.Add(FString::Printf(TEXT("room %d: template %d does not allow %d quarter turn(s)"), Room.RoomId, Room.TemplateIndex, Room.RotationSteps));
		}
		if ((Turned & Room.ConnectionMask) != Room.ConnectionMask)
		{
			Problems.Add(FString::Printf(TEXT("room %d: template %d turned %d has doorways %s, needs %s"), Room.RoomId, Room.TemplateIndex,
				Room.RotationSteps, *MaskToString(Turned), *MaskToString(Room.ConnectionMask)));
		}
	}
	return Problems;
}
