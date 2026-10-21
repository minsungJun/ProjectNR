// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRGridLayoutGenerator.h"

#include "Algo/Sort.h"
#include "Math/RandomStream.h"

namespace NRGridGen
{
	/** Values of ENRGridRoomType. */
	constexpr int32 RoomTypeCount = 6;
	constexpr uint8 AllTypes = (1 << RoomTypeCount) - 1;

	/** Values of ENRRoomEnvironment. */
	constexpr int32 EnvironmentCount = 3;

	bool IsHall(const FNRGeneratedRoom& Room)
	{
		return Room.RoomType == ENRGridRoomType::Hall;
	}

	int32 RequiredMainPath(const FNRMapGenSettings& Settings)
	{
		return 2 + (Settings.bRequireSafe ? 1 : 0) + (Settings.bRequireElite ? 1 : 0);
	}

	bool IsHybrid(const FNRMapGenSettings& Settings)
	{
		return Settings.EnvironmentMode == ENRMapEnvironmentMode::Hybrid;
	}

	/** The one environment of an IndoorOnly or OutdoorOnly map. */
	ENRRoomEnvironment SingleEnvironment(const FNRMapGenSettings& Settings)
	{
		return Settings.EnvironmentMode == ENRMapEnvironmentMode::OutdoorOnly ? ENRRoomEnvironment::Outdoor : ENRRoomEnvironment::Indoor;
	}

	/** Environments rooms of this map may have. */
	TArray<ENRRoomEnvironment> ModeEnvironments(const FNRMapGenSettings& Settings)
	{
		return IsHybrid(Settings)
			? TArray<ENRRoomEnvironment>({ ENRRoomEnvironment::Indoor, ENRRoomEnvironment::Outdoor, ENRRoomEnvironment::Transition })
			: TArray<ENRRoomEnvironment>({ SingleEnvironment(Settings) });
	}

	/** Indoor for Outdoor and the other way round. */
	ENRRoomEnvironment OtherSide(ENRRoomEnvironment Environment)
	{
		return Environment == ENRRoomEnvironment::Indoor ? ENRRoomEnvironment::Outdoor : ENRRoomEnvironment::Indoor;
	}

	/** Start environments a Hybrid map may draw. */
	TArray<ENRRoomEnvironment> HybridStarts(const FNRMapGenSettings& Settings)
	{
		return Settings.bRandomizeHybridStart
			? TArray<ENRRoomEnvironment>({ ENRRoomEnvironment::Indoor, ENRRoomEnvironment::Outdoor })
			: TArray<ENRRoomEnvironment>({ Settings.HybridStartEnvironment });
	}

	/** The environment of hall cells between two rooms: theirs when they share one, otherwise Transition. */
	ENRRoomEnvironment BetweenEnvironment(ENRRoomEnvironment A, ENRRoomEnvironment B)
	{
		return A == B ? A : ENRRoomEnvironment::Transition;
	}

	FString EnvName(ENRRoomEnvironment Environment)
	{
		return FNRGridLayoutGenerator::EnvironmentName(Environment);
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

	void AddConnection(FNRGeneratedLayout& Layout, int32 A, int32 B, const FIntPoint& CellA, const FIntPoint& CellB)
	{
		FNRGeneratedConnection& Connection = Layout.Connections.AddDefaulted_GetRef();
		const bool bSwap = B < A;
		Connection.RoomA = bSwap ? B : A;
		Connection.RoomB = bSwap ? A : B;
		Connection.CellA = bSwap ? CellB : CellA;
		Connection.CellB = bSwap ? CellA : CellB;
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

	FString TypeName(ENRGridRoomType Type)
	{
		return StaticEnum<ENRGridRoomType>()->GetNameStringByValue(static_cast<int64>(Type));
	}

	uint8 TypeBit(ENRGridRoomType Type)
	{
		return static_cast<uint8>(1 << static_cast<int32>(Type));
	}

	FNRRoomPort MakePort(const FIntPoint& Cell, ENRGridDirection Side)
	{
		FNRRoomPort Port;
		Port.Cell = Cell;
		Port.Side = Side;
		return Port;
	}

	/** The room every cell gets when there are no templates: one cell, four doorways, never turned. */
	const FNRRoomTemplateDesc& ImplicitTemplate()
	{
		static const FNRRoomTemplateDesc Desc = []
		{
			FNRRoomTemplateDesc Made = FNRRoomTemplateDesc::Make(FIntPoint(1, 1), 0b1111, AllTypes);
			Made.RotationMask = 0b0001;
			return Made;
		}();
		return Desc;
	}

	/** The templates rooms are placed from: the given ones, or the implicit one-cell room. */
	TArray<FNRRoomTemplateDesc> Effective(const TArray<FNRRoomTemplateDesc>& Templates)
	{
		return Templates.Num() > 0 ? Templates : TArray<FNRRoomTemplateDesc>({ ImplicitTemplate() });
	}

	/** A template at one of its turns: its size and doorways in world directions, cells counted
	 *  from the turned footprint's south-west corner. */
	struct FTurnedRoom
	{
		/** Index into the effective template list. */
		int32 Template = INDEX_NONE;
		int32 Steps = 0;
		FIntPoint Size = FIntPoint(1, 1);
		TArray<FNRRoomPort> Ports;

		bool HasPort(const FIntPoint& Cell, ENRGridDirection Side) const
		{
			return Ports.Contains(MakePort(Cell, Side));
		}
	};

	FTurnedRoom Turn(const FNRRoomTemplateDesc& Template, int32 TemplateIndex, int32 Steps)
	{
		FTurnedRoom Variant;
		Variant.Template = TemplateIndex;
		Variant.Steps = Steps;
		Variant.Size = NRGrid::TurnFootprint(Template.Footprint, Steps);
		for (const FNRRoomPort& Port : Template.Ports)
		{
			Variant.Ports.AddUnique(MakePort(NRGrid::TurnCell(Port.Cell, Template.Footprint, Steps), NRGrid::LocalToWorld(Port.Side, Steps)));
		}
		return Variant;
	}

	/**
	 * One way a room of some type can stand on the grid: a size, and every doorway place that
	 * any template of that size has at any of its allowed turns. Which template (and turn) the
	 * room really is gets chosen at the end, among the turns that have every doorway the room
	 * ended up using; a doorway is only used while some turn still has all of them.
	 */
	struct FShape
	{
		FIntPoint Size = FIntPoint(1, 1);
		TArray<FNRRoomPort> Ports;
		TArray<const FTurnedRoom*> Turns;
		float Weight = 0.0f;

		bool HasPort(const FNRRoomPort& Port) const
		{
			return Ports.Contains(Port);
		}

		/** Whether some turn of some template has every one of Used. */
		bool Covers(const TArray<FNRRoomPort>& Used) const
		{
			return Turns.ContainsByPredicate([&Used](const FTurnedRoom* Turned)
			{
				return !Used.ContainsByPredicate([Turned](const FNRRoomPort& Port) { return !Turned->HasPort(Port.Cell, Port.Side); });
			});
		}
	};

	/** Every allowed turn of every usable template, and from them the shapes each room type can take
	 *  in each environment. */
	struct FCatalog
	{
		TArray<FTurnedRoom> Turns;
		/** By ENRGridRoomType, then ENRRoomEnvironment. */
		TArray<FShape> Shapes[RoomTypeCount][EnvironmentCount];

		const TArray<FShape>& For(ENRGridRoomType Type, ENRRoomEnvironment Environment) const
		{
			return Shapes[static_cast<int32>(Type)][static_cast<int32>(Environment)];
		}

		bool Has(ENRGridRoomType Type, ENRRoomEnvironment Environment) const
		{
			return For(Type, Environment).Num() > 0;
		}

		/** bAnyEnvironment: every template stands in for every environment (the implicit one-cell room). */
		FCatalog(const TArray<FNRRoomTemplateDesc>& Templates, bool bAnyEnvironment)
		{
			for (int32 Index = 0; Index < Templates.Num(); ++Index)
			{
				const FNRRoomTemplateDesc& Template = Templates[Index];
				if (Template.Weight <= 0.0f || FNRGridLayoutGenerator::CheckTemplate(Template).Num() > 0)
				{
					continue;
				}
				for (int32 Steps = 0; Steps < 4; ++Steps)
				{
					if (Template.RotationMask & (1 << Steps))
					{
						Turns.Add(Turn(Template, Index, Steps));
					}
				}
			}

			// Turns is complete, so pointers into it stay valid from here on.
			for (int32 Slot = 0; Slot < RoomTypeCount * EnvironmentCount; ++Slot)
			{
				const int32 Type = Slot / EnvironmentCount;
				const int32 Environment = Slot % EnvironmentCount;
				TArray<FShape>& TypeShapes = Shapes[Type][Environment];
				for (const FTurnedRoom& Turned : Turns)
				{
					if (!(Templates[Turned.Template].TypeMask & (1 << Type))
						|| (!bAnyEnvironment && static_cast<int32>(Templates[Turned.Template].Environment) != Environment))
					{
						continue;
					}
					FShape* Shape = TypeShapes.FindByPredicate([&Turned](const FShape& Other) { return Other.Size == Turned.Size; });
					if (!Shape)
					{
						Shape = &TypeShapes.AddDefaulted_GetRef();
						Shape->Size = Turned.Size;
					}
					Shape->Turns.Add(&Turned);
					for (const FNRRoomPort& Port : Turned.Ports)
					{
						Shape->Ports.AddUnique(Port);
					}
				}

				// A template's weight is split over the sizes it comes in (a 1x2 room is also 2x1
				// turned), so a long room is not twice as likely as a square one of equal weight.
				for (FShape& Shape : TypeShapes)
				{
					TArray<int32> Seen;
					for (const FTurnedRoom* Turned : Shape.Turns)
					{
						if (Seen.Contains(Turned->Template))
						{
							continue;
						}
						Seen.Add(Turned->Template);
						int32 Sizes = 0;
						for (const FShape& Other : TypeShapes)
						{
							Sizes += Other.Turns.ContainsByPredicate([Turned](const FTurnedRoom* T) { return T->Template == Turned->Template; }) ? 1 : 0;
						}
						Shape.Weight += Templates[Turned->Template].Weight / FMath::Max(Sizes, 1);
					}
				}
			}
		}

		FCatalog(const FCatalog&) = delete;
		FCatalog& operator=(const FCatalog&) = delete;
	};

	/** Cells between two footprints' nearest cells, across plus down. */
	int32 FootprintDistance(const FIntPoint& AnchorA, const FIntPoint& SizeA, const FIntPoint& AnchorB, const FIntPoint& SizeB)
	{
		const int32 DX = FMath::Max3(0, AnchorA.X - (AnchorB.X + SizeB.X - 1), AnchorB.X - (AnchorA.X + SizeA.X - 1));
		const int32 DY = FMath::Max3(0, AnchorA.Y - (AnchorB.Y + SizeB.Y - 1), AnchorB.Y - (AnchorA.Y + SizeA.Y - 1));
		return DX + DY;
	}

	/** A room put down: its shape, where its south-west cell is, and the corridor that joins it
	 *  to the room it was placed against (doorways relative to each room's south-west cell). */
	struct FPlacement
	{
		const FShape* Shape = nullptr;
		FIntPoint Anchor = FIntPoint::ZeroValue;
		int32 Parent = INDEX_NONE;
		FNRRoomPort Exit;
		FNRRoomPort Entry;
		FIntPoint FromCell = FIntPoint::ZeroValue;
		FIntPoint ToCell = FIntPoint::ZeroValue;
	};

	/** Each option's share: its shape's weight split over that shape's options, so a big room
	 *  with many ways to fit is not more likely than its weight says. */
	TArray<float> OptionWeights(const TArray<FPlacement>& Options)
	{
		TMap<const FShape*, int32> Counts;
		for (const FPlacement& Option : Options)
		{
			++Counts.FindOrAdd(Option.Shape);
		}
		TArray<float> Weights;
		for (const FPlacement& Option : Options)
		{
			Weights.Add(Option.Shape->Weight / Counts[Option.Shape]);
		}
		return Weights;
	}

	int32 PickWeighted(const TArray<float>& Weights, FRandomStream& Stream)
	{
		float Total = 0.0f;
		for (float Weight : Weights)
		{
			Total += Weight;
		}
		float Roll = Stream.FRand() * Total;
		for (int32 Index = 0; Index < Weights.Num(); ++Index)
		{
			if (Roll < Weights[Index])
			{
				return Index;
			}
			Roll -= Weights[Index];
		}
		return Weights.Num() - 1;
	}

	/** Options in a weighted random order (each drawn ahead of the rest by its share), for the
	 *  depth-first main path. */
	void WeightedOrder(TArray<FPlacement>& Options, FRandomStream& Stream)
	{
		const TArray<float> Weights = OptionWeights(Options);
		TArray<TPair<double, int32>> Keys;
		for (int32 Index = 0; Index < Options.Num(); ++Index)
		{
			const double Roll = FMath::Max(static_cast<double>(Stream.FRand()), 1e-12);
			Keys.Emplace(FMath::Loge(Roll) / FMath::Max(static_cast<double>(Weights[Index]), 1e-9), Index);
		}
		Algo::Sort(Keys, [](const TPair<double, int32>& A, const TPair<double, int32>& B)
		{
			return A.Key != B.Key ? A.Key > B.Key : A.Value < B.Value;
		});
		TArray<FPlacement> Ordered;
		for (const TPair<double, int32>& Key : Keys)
		{
			Ordered.Add(Options[Key.Value]);
		}
		Options = MoveTemp(Ordered);
	}

	/** The turned template a room becomes: among its shape's turns that have every doorway it
	 *  uses (and no other, when bExact), by template weight (split over that template's fitting turns). */
	const FTurnedRoom* PickTurn(const FShape& Shape, const TArray<FNRRoomPort>& Used, const TArray<FNRRoomTemplateDesc>& Templates,
		bool bExact, FRandomStream& Stream)
	{
		TArray<const FTurnedRoom*> Fitting;
		for (const FTurnedRoom* Turned : Shape.Turns)
		{
			if (!Used.ContainsByPredicate([Turned](const FNRRoomPort& Port) { return !Turned->HasPort(Port.Cell, Port.Side); })
				&& (!bExact || Turned->Ports.Num() == Used.Num()))
			{
				Fitting.Add(Turned);
			}
		}
		if (Fitting.Num() == 0)
		{
			return nullptr;
		}
		TArray<float> Weights;
		for (const FTurnedRoom* Turned : Fitting)
		{
			const int32 Same = Fitting.FilterByPredicate([Turned](const FTurnedRoom* Other) { return Other->Template == Turned->Template; }).Num();
			Weights.Add(Templates[Turned->Template].Weight / Same);
		}
		return Fitting[PickWeighted(Weights, Stream)];
	}

	/** Room types along a main path of PathRooms rooms: start, safe halfway to the elite, elite,
	 *  boss, combat in between. */
	TArray<ENRGridRoomType> MainPathTypes(const FNRMapGenSettings& Settings, int32 PathRooms)
	{
		TArray<ENRGridRoomType> PathTypes;
		PathTypes.Init(ENRGridRoomType::Combat, PathRooms);
		PathTypes[0] = ENRGridRoomType::Start;
		PathTypes[PathRooms - 1] = ENRGridRoomType::Boss;
		if (Settings.bRequireElite)
		{
			PathTypes[PathRooms - 2] = ENRGridRoomType::Elite;
		}
		if (Settings.bRequireSafe)
		{
			// Middle of what lies between the start and the elite (or the boss).
			const int32 LastFree = PathRooms - 1 - (Settings.bRequireElite ? 2 : 1);
			PathTypes[FMath::Clamp((1 + LastFree) / 2, 1, LastFree)] = ENRGridRoomType::Safe;
		}
		return PathTypes;
	}

	/**
	 * Where a run of Count Transition rooms can start on a Hybrid main path: at least one room of
	 * the start environment before it and one of the other after it, and every room's type
	 * available (Has) in the environment it would get.
	 */
	TArray<int32> TransitionStarts(const TArray<ENRGridRoomType>& PathTypes, ENRRoomEnvironment From, int32 Count,
		TFunctionRef<bool(ENRGridRoomType, ENRRoomEnvironment)> Has)
	{
		TArray<int32> Starts;
		for (int32 First = 1; First + Count <= PathTypes.Num() - 1; ++First)
		{
			bool bFits = true;
			for (int32 Index = 0; bFits && Index < PathTypes.Num(); ++Index)
			{
				const ENRRoomEnvironment Environment = Index < First ? From
					: Index < First + Count ? ENRRoomEnvironment::Transition : OtherSide(From);
				bFits = Has(PathTypes[Index], Environment);
			}
			if (bFits)
			{
				Starts.Add(First);
			}
		}
		return Starts;
	}

	/** Whether some usable template stands for Type in Environment and fits the grid at an allowed turn. */
	bool Placeable(const TArray<FNRRoomTemplateDesc>& Templates, ENRGridRoomType Type, ENRRoomEnvironment Environment, const FIntPoint& GridSize)
	{
		for (const FNRRoomTemplateDesc& Template : Templates)
		{
			if (Template.Weight <= 0.0f || !(Template.TypeMask & TypeBit(Type)) || Template.Environment != Environment)
			{
				continue;
			}
			for (int32 Steps = 0; Steps < 4; ++Steps)
			{
				const FIntPoint Size = NRGrid::TurnFootprint(Template.Footprint, Steps);
				if ((Template.RotationMask & (1 << Steps)) && Size.X <= GridSize.X && Size.Y <= GridSize.Y)
				{
					return true;
				}
			}
		}
		return false;
	}
}

TArray<FNRRoomPort> FNRRoomTemplateDesc::OutlinePorts(const FIntPoint& Footprint, uint8 PortMask)
{
	TArray<FNRRoomPort> Ports;
	for (int32 Dir = 0; Dir < 4; ++Dir)
	{
		if (!(PortMask & (1 << Dir)))
		{
			continue;
		}
		const ENRGridDirection Side = static_cast<ENRGridDirection>(Dir);
		const FIntPoint Offset = NRGrid::ToOffset(Side);
		for (int32 Y = 0; Y < Footprint.Y; ++Y)
		{
			for (int32 X = 0; X < Footprint.X; ++X)
			{
				const FIntPoint Out(X + Offset.X, Y + Offset.Y);
				if (Out.X < 0 || Out.Y < 0 || Out.X >= Footprint.X || Out.Y >= Footprint.Y)
				{
					Ports.Add(NRGridGen::MakePort(FIntPoint(X, Y), Side));
				}
			}
		}
	}
	return Ports;
}

FNRRoomTemplateDesc FNRRoomTemplateDesc::Make(const FIntPoint& Footprint, uint8 PortMask, uint8 TypeMask, float Weight, ENRRoomEnvironment Environment)
{
	FNRRoomTemplateDesc Desc;
	Desc.Footprint = Footprint;
	Desc.Ports = OutlinePorts(Footprint, PortMask);
	Desc.TypeMask = TypeMask;
	Desc.Weight = Weight;
	Desc.Environment = Environment;
	return Desc;
}

TArray<FString> FNRGridLayoutGenerator::CheckTemplate(const FNRRoomTemplateDesc& Template)
{
	TArray<FString> Problems;
	if (Template.Footprint.X < 1 || Template.Footprint.Y < 1)
	{
		Problems.Add(FString::Printf(TEXT("footprint %dx%d"), Template.Footprint.X, Template.Footprint.Y));
		return Problems;
	}
	if (Template.Ports.Num() == 0)
	{
		Problems.Add(TEXT("no doorway"));
	}
	for (const FNRRoomPort& Port : Template.Ports)
	{
		const FIntPoint Out = Port.Cell + NRGrid::ToOffset(Port.Side);
		const bool bInside = Port.Cell.X >= 0 && Port.Cell.Y >= 0 && Port.Cell.X < Template.Footprint.X && Port.Cell.Y < Template.Footprint.Y;
		const bool bOutward = Out.X < 0 || Out.Y < 0 || Out.X >= Template.Footprint.X || Out.Y >= Template.Footprint.Y;
		if (!bInside || !bOutward)
		{
			Problems.Add(FString::Printf(TEXT("doorway %d,%d %s is not on the outline of a %dx%d footprint"),
				Port.Cell.X, Port.Cell.Y, NRGrid::Letter(Port.Side), Template.Footprint.X, Template.Footprint.Y));
		}
	}
	if ((Template.RotationMask & 0b1111) == 0)
	{
		Problems.Add(TEXT("no allowed turn"));
	}
	if ((Template.TypeMask & NRGridGen::AllTypes) == 0)
	{
		Problems.Add(TEXT("no room type"));
	}
	return Problems;
}

bool FNRGridLayoutGenerator::CheckSettings(const FNRMapGenSettings& Settings, const TArray<FNRRoomTemplateDesc>& Templates,
	const TArray<FNRCorridorTemplateDesc>& Corridors, FString& OutError)
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
	else if (Settings.MinStartBossDistance > Settings.GridSize.X + Settings.GridSize.Y - 2)
	{
		OutError = FString::Printf(TEXT("start and boss %d cells apart cannot fit a %dx%d grid"),
			Settings.MinStartBossDistance, Settings.GridSize.X, Settings.GridSize.Y);
	}
	else if (Settings.MaxStartConnections < 1)
	{
		OutError = FString::Printf(TEXT("MaxStartConnections %d leaves the start room no way out"), Settings.MaxStartConnections);
	}
	else if (Settings.MinHalls > Settings.MaxHalls)
	{
		OutError = FString::Printf(TEXT("MinHalls %d is more than MaxHalls %d"), Settings.MinHalls, Settings.MaxHalls);
	}
	else if (Settings.MaxHalls > 0 && Settings.MaxHallsInRow < 1)
	{
		OutError = TEXT("MaxHallsInRow must be at least 1 for hall cells");
	}
	else if (Settings.MinRooms + Settings.MinHalls > Cells)
	{
		OutError = FString::Printf(TEXT("%d rooms and %d hall cells cannot fit a %dx%d grid"), Settings.MinRooms, Settings.MinHalls,
			Settings.GridSize.X, Settings.GridSize.Y);
	}
	else if (Settings.Shape == ENRGridLayoutShape::Linear && Settings.MinHalls > FMath::Max(PathMax - 2, 0) * Settings.MaxHallsInRow)
	{
		OutError = FString::Printf(TEXT("a Linear map puts every hall cell between main path rooms: %d do not fit %d room(s), %d in a row"),
			Settings.MinHalls, PathMax, Settings.MaxHallsInRow);
	}
	else if (IsHybrid(Settings) && !Settings.bRandomizeHybridStart && Settings.HybridStartEnvironment == ENRRoomEnvironment::Transition)
	{
		OutError = TEXT("a Hybrid map starts Indoor or Outdoor, not Transition");
	}
	else if (IsHybrid(Settings) && (Settings.MinTransitionRooms < 1 || Settings.MinTransitionRooms > Settings.MaxTransitionRooms))
	{
		OutError = FString::Printf(TEXT("transition rooms %d-%d: at least one, and Min no more than Max"), Settings.MinTransitionRooms, Settings.MaxTransitionRooms);
	}
	else if (IsHybrid(Settings) && PathMax < Settings.MinTransitionRooms + 2)
	{
		OutError = FString::Printf(TEXT("a main path of at most %d rooms cannot hold %d transition room(s) with a room of each side around them"),
			PathMax, Settings.MinTransitionRooms);
	}
	if (!OutError.IsEmpty())
	{
		return false;
	}

	// Corridors: every pair of environments that can meet in this map, and never Indoor to Outdoor.
	if (Corridors.Num() > 0)
	{
		for (int32 Index = 0; Index < Corridors.Num(); ++Index)
		{
			if (Corridors[Index].Weight > 0.0f && !NRGrid::EnvironmentsMeet(Corridors[Index].EndpointA, Corridors[Index].EndpointB))
			{
				OutError = FString::Printf(TEXT("corridor template %d joins Indoor and Outdoor, which never meet"), Index);
				return false;
			}
		}
		const TArray<ENRRoomEnvironment> Environments = ModeEnvironments(Settings);
		for (int32 A = 0; A < Environments.Num(); ++A)
		{
			for (int32 B = A; B < Environments.Num(); ++B)
			{
				if (!NRGrid::EnvironmentsMeet(Environments[A], Environments[B]))
				{
					continue;
				}
				if (!Corridors.ContainsByPredicate([&](const FNRCorridorTemplateDesc& Corridor) { return Corridor.Weight > 0.0f && Corridor.Joins(Environments[A], Environments[B]); }))
				{
					OutError = FString::Printf(TEXT("no corridor template joins %s and %s"), *EnvName(Environments[A]), *EnvName(Environments[B]));
					return false;
				}
			}
		}
	}

	if (Templates.Num() == 0)
	{
		return true;
	}

	// Templates: each usable one well formed, and every room type a map needs placeable.
	for (int32 Index = 0; Index < Templates.Num(); ++Index)
	{
		if (Templates[Index].Weight > 0.0f)
		{
			const TArray<FString> Problems = CheckTemplate(Templates[Index]);
			if (Problems.Num() > 0)
			{
				OutError = FString::Printf(TEXT("room template %d: %s"), Index, *FString::Join(Problems, TEXT(", ")));
				return false;
			}
		}
	}

	// Every room type a map needs, in every environment it can stand in.
	const bool bCombat = PathMin > RequiredMainPath(Settings) || NeedsBranch(Settings.Shape) || Settings.MinRooms > PathMax;
	TArray<TPair<ENRGridRoomType, ENRRoomEnvironment>> Needed;
	if (!IsHybrid(Settings))
	{
		const ENRRoomEnvironment Environment = SingleEnvironment(Settings);
		Needed.Add({ ENRGridRoomType::Start, Environment });
		Needed.Add({ ENRGridRoomType::Boss, Environment });
		if (Settings.bRequireSafe)
		{
			Needed.Add({ ENRGridRoomType::Safe, Environment });
		}
		if (Settings.bRequireElite)
		{
			Needed.Add({ ENRGridRoomType::Elite, Environment });
		}
		if (bCombat)
		{
			Needed.Add({ ENRGridRoomType::Combat, Environment });
		}
	}
	else
	{
		// The main path's own types are checked below; branches keep to the side they grow on.
		for (ENRRoomEnvironment From : HybridStarts(Settings))
		{
			Needed.Add({ ENRGridRoomType::Start, From });
			Needed.Add({ ENRGridRoomType::Boss, OtherSide(From) });
		}
		if (bCombat)
		{
			Needed.Add({ ENRGridRoomType::Combat, ENRRoomEnvironment::Indoor });
			Needed.Add({ ENRGridRoomType::Combat, ENRRoomEnvironment::Outdoor });
		}
	}
	if (Settings.MaxHalls > 0)
	{
		for (ENRRoomEnvironment Environment : ModeEnvironments(Settings))
		{
			Needed.Add({ ENRGridRoomType::Hall, Environment });
		}
	}
	for (const TPair<ENRGridRoomType, ENRRoomEnvironment>& Need : Needed)
	{
		const bool bListed = Templates.ContainsByPredicate([&Need](const FNRRoomTemplateDesc& Template)
		{
			return Template.Weight > 0.0f && (Template.TypeMask & TypeBit(Need.Key)) && Template.Environment == Need.Value;
		});
		if (!bListed || !Placeable(Templates, Need.Key, Need.Value, Settings.GridSize))
		{
			OutError = !bListed
				? FString::Printf(TEXT("no %s room template for %s rooms"), *EnvName(Need.Value), *TypeName(Need.Key))
				: FString::Printf(TEXT("no %s %s room template fits a %dx%d grid at any allowed turn"), *EnvName(Need.Value), *TypeName(Need.Key),
					Settings.GridSize.X, Settings.GridSize.Y);
			return false;
		}
	}

	// Hybrid: some main path length must take a run of Transition rooms, from every start side.
	if (IsHybrid(Settings))
	{
		auto Has = [&Templates, &Settings](ENRGridRoomType Type, ENRRoomEnvironment Environment)
		{
			return Placeable(Templates, Type, Environment, Settings.GridSize);
		};
		for (ENRRoomEnvironment From : HybridStarts(Settings))
		{
			bool bAny = false;
			for (int32 Length = FMath::Max(PathMin, 2); !bAny && Length <= PathMax; ++Length)
			{
				for (int32 Count = Settings.MinTransitionRooms; !bAny && Count <= Settings.MaxTransitionRooms; ++Count)
				{
					bAny = TransitionStarts(MainPathTypes(Settings, Length), From, Count, Has).Num() > 0;
				}
			}
			if (!bAny)
			{
				TArray<FString> TransitionTypes;
				for (int32 Type = 0; Type < RoomTypeCount; ++Type)
				{
					const ENRGridRoomType RoomType = static_cast<ENRGridRoomType>(Type);
					if (RoomType != ENRGridRoomType::Hall && Has(RoomType, ENRRoomEnvironment::Transition))
					{
						TransitionTypes.Add(TypeName(RoomType));
					}
				}
				OutError = FString::Printf(TEXT("no main path of %d-%d rooms can run from %s through %d-%d Transition room(s) to %s: Transition templates stand for %s, and every other main path room needs a template of its side"),
					PathMin, PathMax, *EnvName(From), Settings.MinTransitionRooms, Settings.MaxTransitionRooms, *EnvName(OtherSide(From)),
					TransitionTypes.Num() > 0 ? *FString::Join(TransitionTypes, TEXT(", ")) : TEXT("no room type"));
				return false;
			}
		}
	}
	return true;
}

bool FNRGridLayoutGenerator::Generate(const FNRMapGenSettings& Settings, const TArray<FNRRoomTemplateDesc>& Templates,
	const TArray<FNRCorridorTemplateDesc>& Corridors, int32 Seed, FNRGeneratedLayout& OutLayout, FString& OutError)
{
	if (!CheckSettings(Settings, Templates, Corridors, OutError))
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
		if (TryGenerate(Settings, Templates, Corridors, AttemptSeed, Layout, Reason))
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

bool FNRGridLayoutGenerator::TryGenerate(const FNRMapGenSettings& Settings, const TArray<FNRRoomTemplateDesc>& InTemplates,
	const TArray<FNRCorridorTemplateDesc>& Corridors, int32 AttemptSeed, FNRGeneratedLayout& OutLayout, FString& OutReason)
{
	using namespace NRGridGen;
	FRandomStream Stream(AttemptSeed);
	const TArray<FNRRoomTemplateDesc> Templates = Effective(InTemplates);
	const FCatalog Catalog(Templates, InTemplates.Num() == 0);

	int32 PathMin = 0;
	int32 PathMax = 0;
	MainPathRange(Settings, PathMin, PathMax);
	const int32 PathRooms = Stream.RandRange(PathMin, PathMax);
	const int32 TargetRooms = Settings.Shape == ENRGridLayoutShape::Linear
		? PathRooms
		: Stream.RandRange(FMath::Max(Settings.MinRooms, PathRooms + (NeedsBranch(Settings.Shape) ? 1 : 0)), Settings.MaxRooms);

	// Room types along the main path.
	const TArray<ENRGridRoomType> PathTypes = MainPathTypes(Settings, PathRooms);

	// Environments along the main path, before anything is placed: one throughout, or (Hybrid) the
	// start side, a run of Transition rooms, and the other side up to the boss.
	TArray<ENRRoomEnvironment> PathEnvironments;
	PathEnvironments.Init(SingleEnvironment(Settings), PathRooms);
	if (IsHybrid(Settings))
	{
		const ENRRoomEnvironment From = Settings.bRandomizeHybridStart
			? (Stream.FRand() < 0.5f ? ENRRoomEnvironment::Indoor : ENRRoomEnvironment::Outdoor)
			: Settings.HybridStartEnvironment;
		const int32 TransitionRooms = Stream.RandRange(Settings.MinTransitionRooms, Settings.MaxTransitionRooms);
		const TArray<int32> Starts = TransitionStarts(PathTypes, From, TransitionRooms,
			[&Catalog](ENRGridRoomType Type, ENRRoomEnvironment Environment) { return Catalog.Has(Type, Environment); });
		if (Starts.Num() == 0)
		{
			OutReason = FString::Printf(TEXT("no place on a main path of %d rooms for %d transition room(s)"), PathRooms, TransitionRooms);
			return false;
		}
		const int32 First = Starts[Stream.RandRange(0, Starts.Num() - 1)];
		for (int32 Room = 0; Room < PathRooms; ++Room)
		{
			PathEnvironments[Room] = Room < First ? From : Room < First + TransitionRooms ? ENRRoomEnvironment::Transition : OtherSide(From);
		}
	}
	auto GapEnvironment = [&PathEnvironments](int32 Gap) { return BetweenEnvironment(PathEnvironments[Gap], PathEnvironments[Gap + 1]); };

	// Hall cells: some between rooms of the main path (none right before the boss, and only where
	// a hall template of that gap's environment exists), the rest on side branches, each of those
	// with a room beyond it - so no more there than branch rooms.
	const int32 HallTarget = Settings.MaxHalls > 0 ? Stream.RandRange(Settings.MinHalls, Settings.MaxHalls) : 0;
	const int32 Gaps = FMath::Max(PathRooms - 2, 0);
	int32 HallGaps = 0;
	for (int32 Gap = 0; Gap < Gaps; ++Gap)
	{
		HallGaps += Catalog.Has(ENRGridRoomType::Hall, GapEnvironment(Gap)) ? 1 : 0;
	}
	const int32 MainHallsMax = FMath::Min(HallTarget, HallGaps * Settings.MaxHallsInRow);
	const int32 MainHallsMin = FMath::Max(HallTarget - (TargetRooms - PathRooms), FMath::Min((HallTarget + 1) / 2, MainHallsMax));
	if (MainHallsMin > MainHallsMax)
	{
		OutReason = TEXT("no place for every hall cell");
		return false;
	}
	const int32 MainHalls = Stream.RandRange(MainHallsMin, MainHallsMax);
	TArray<int32> HallsInGap;
	HallsInGap.Init(0, Gaps);
	for (int32 Hall = 0; Hall < MainHalls; ++Hall)
	{
		TArray<int32> Open;
		for (int32 Gap = 0; Gap < Gaps; ++Gap)
		{
			if (HallsInGap[Gap] < Settings.MaxHallsInRow && Catalog.Has(ENRGridRoomType::Hall, GapEnvironment(Gap)))
			{
				Open.Add(Gap);
			}
		}
		++HallsInGap[Open[Stream.RandRange(0, Open.Num() - 1)]];
	}
	// Types and environments by room index (= room id), main path first.
	TArray<ENRGridRoomType> Types;
	TArray<ENRRoomEnvironment> Environments;
	for (int32 Room = 0; Room < PathRooms; ++Room)
	{
		Types.Add(PathTypes[Room]);
		Environments.Add(PathEnvironments[Room]);
		for (int32 Hall = 0; Room < Gaps && Hall < HallsInGap[Room]; ++Hall)
		{
			Types.Add(ENRGridRoomType::Hall);
			Environments.Add(GapEnvironment(Room));
		}
	}
	const int32 PathLength = Types.Num();
	const int32 BossIndex = PathLength - 1;

	// Rooms placed so far (index = room id), the doorways each one uses, and which room covers
	// each cell.
	TArray<FPlacement> Placed;
	TArray<TArray<FNRRoomPort>> Used;
	TMap<FIntPoint, int32> RoomAt;
	auto Fits = [&](const FShape& Shape, const FIntPoint& Anchor)
	{
		for (int32 Y = 0; Y < Shape.Size.Y; ++Y)
		{
			for (int32 X = 0; X < Shape.Size.X; ++X)
			{
				const FIntPoint Cell(Anchor.X + X, Anchor.Y + Y);
				if (!InGrid(Settings, Cell) || RoomAt.Contains(Cell))
				{
					return false;
				}
			}
		}
		return true;
	};
	auto Place = [&](const FPlacement& Placement)
	{
		const int32 Index = Placed.Add(Placement);
		TArray<FNRRoomPort>& Mine = Used.AddDefaulted_GetRef();
		if (Placement.Parent != INDEX_NONE)
		{
			Mine.Add(Placement.Entry);
			Used[Placement.Parent].Add(Placement.Exit);
		}
		for (int32 Y = 0; Y < Placement.Shape->Size.Y; ++Y)
		{
			for (int32 X = 0; X < Placement.Shape->Size.X; ++X)
			{
				RoomAt.Add(FIntPoint(Placement.Anchor.X + X, Placement.Anchor.Y + Y), Index);
			}
		}
	};
	// Takes back the last room placed (the main path search only ever removes its newest room).
	auto Unplace = [&]()
	{
		const FPlacement Last = Placed.Pop();
		Used.Pop();
		if (Last.Parent != INDEX_NONE)
		{
			Used[Last.Parent].Pop();
		}
		for (int32 Y = 0; Y < Last.Shape->Size.Y; ++Y)
		{
			for (int32 X = 0; X < Last.Shape->Size.X; ++X)
			{
				RoomAt.Remove(FIntPoint(Last.Anchor.X + X, Last.Anchor.Y + Y));
			}
		}
	};
	// Every way a room of Type in Environment can stand against one doorway of a placed room,
	// meeting it with a doorway of its own - as long as the two environments may meet and some
	// template of the placed room still has every doorway it would then use.
	auto OptionsAt = [&](int32 Parent, const FNRRoomPort& Exit, ENRGridRoomType Type, ENRRoomEnvironment Environment, TArray<FPlacement>& Out)
	{
		if (!NRGrid::EnvironmentsMeet(Environments[Parent], Environment))
		{
			return;
		}
		const FIntPoint FromCell = Placed[Parent].Anchor + Exit.Cell;
		const FIntPoint ToCell = FromCell + NRGrid::ToOffset(Exit.Side);
		if (!InGrid(Settings, ToCell) || RoomAt.Contains(ToCell))
		{
			return;
		}
		TArray<FNRRoomPort> ParentUsed = Used[Parent];
		ParentUsed.Add(Exit);
		if (!Placed[Parent].Shape->Covers(ParentUsed))
		{
			return;
		}
		const ENRGridDirection EntrySide = NRGrid::Opposite(Exit.Side);
		for (const FShape& Shape : Catalog.For(Type, Environment))
		{
			for (const FNRRoomPort& Entry : Shape.Ports)
			{
				const FIntPoint Anchor = ToCell - Entry.Cell;
				// The boss keeps its distance from the start room.
				if (Type == ENRGridRoomType::Boss && Placed.Num() > 0
					&& FootprintDistance(Anchor, Shape.Size, Placed[0].Anchor, Placed[0].Shape->Size) < Settings.MinStartBossDistance)
				{
					continue;
				}
				if (Entry.Side == EntrySide && Fits(Shape, Anchor))
				{
					FPlacement& Option = Out.AddDefaulted_GetRef();
					Option.Shape = &Shape;
					Option.Anchor = Anchor;
					Option.Parent = Parent;
					Option.Exit = Exit;
					Option.Entry = Entry;
					Option.FromCell = FromCell;
					Option.ToCell = ToCell;
				}
			}
		}
	};

	// 1. Main path: depth-first, one frame per room with its options in weighted random order.
	struct FFrame
	{
		TArray<FPlacement> Options;
		int32 Next = 0;
		bool bPlaced = false;
	};
	TArray<FFrame> Frames;
	{
		FFrame& First = Frames.AddDefaulted_GetRef();
		for (const FShape& Shape : Catalog.For(ENRGridRoomType::Start, Environments[0]))
		{
			for (int32 Y = 0; Y + Shape.Size.Y <= Settings.GridSize.Y; ++Y)
			{
				for (int32 X = 0; X + Shape.Size.X <= Settings.GridSize.X; ++X)
				{
					FPlacement& Option = First.Options.AddDefaulted_GetRef();
					Option.Shape = &Shape;
					Option.Anchor = FIntPoint(X, Y);
				}
			}
		}
		WeightedOrder(First.Options, Stream);
	}
	int32 Budget = Settings.MainPathSearchBudget;
	while (true)
	{
		if (Frames.Num() == 0 || --Budget < 0)
		{
			OutReason = TEXT("main path walk ran out of room");
			return false;
		}
		const int32 Index = Frames.Num() - 1;
		FFrame& Top = Frames.Last();
		if (Top.bPlaced)
		{
			Unplace();
			Top.bPlaced = false;
		}
		if (Top.Next >= Top.Options.Num())
		{
			Frames.Pop();
			continue;
		}
		const FPlacement Option = Top.Options[Top.Next++];
		if (!Fits(*Option.Shape, Option.Anchor))
		{
			continue;
		}
		Place(Option);
		Top.bPlaced = true;
		if (Index + 1 == PathLength)
		{
			break;
		}

		FFrame Next;
		for (const FNRRoomPort& Exit : Option.Shape->Ports)
		{
			OptionsAt(Index, Exit, Types[Index + 1], Environments[Index + 1], Next.Options);
		}
		WeightedOrder(Next.Options, Stream);
		Frames.Add(MoveTemp(Next));
	}

	TArray<int32> Depth;
	Depth.Init(0, PathLength);

	// 2. Branches against free doorways of any room but the boss: a room, or a hall cell with a
	//    room beyond it while hall cells are left (always once there are as many as rooms left),
	//    in the environment of the room they grow from.
	int32 RoomsPlaced = PathRooms;
	int32 HallsLeft = HallTarget - MainHalls;
	while (RoomsPlaced < TargetRooms)
	{
		const bool bWithHall = HallsLeft > 0 && (HallsLeft >= TargetRooms - RoomsPlaced || Stream.FRand() < 0.5f);
		struct FExit
		{
			int32 Room;
			FNRRoomPort Port;
		};
		TArray<FExit> Exits;
		for (int32 Room = 0; Room < Placed.Num(); ++Room)
		{
			// The boss keeps its one corridor; the start room stops at its limit.
			if (Room == BossIndex || Depth[Room] >= Settings.MaxBranchDepth
				|| (Room == 0 && Used[Room].Num() >= Settings.MaxStartConnections))
			{
				continue;
			}
			for (const FNRRoomPort& Port : Placed[Room].Shape->Ports)
			{
				const FIntPoint Cell = Placed[Room].Anchor + Port.Cell + NRGrid::ToOffset(Port.Side);
				if (InGrid(Settings, Cell) && !RoomAt.Contains(Cell))
				{
					Exits.Add({ Room, Port });
				}
			}
		}

		bool bPlaced = false;
		while (Exits.Num() > 0 && !bPlaced)
		{
			// Off a hall cell first, now and then: that is what makes T junctions and crossings.
			TArray<int32> Pool;
			if (Stream.FRand() < Settings.HallBranchBias)
			{
				for (int32 Index = 0; Index < Exits.Num(); ++Index)
				{
					if (Types[Exits[Index].Room] == ENRGridRoomType::Hall)
					{
						Pool.Add(Index);
					}
				}
			}
			const int32 Pick = Pool.Num() > 0 ? Pool[Stream.RandRange(0, Pool.Num() - 1)] : Stream.RandRange(0, Exits.Num() - 1);
			const int32 From = Exits[Pick].Room;

			// A room against the doorway, in the environment of the room (or hall cell) it grows from.
			const ENRRoomEnvironment Environment = Environments[From];
			auto PlaceRoom = [&](int32 Parent, const TArray<FPlacement>& Options)
			{
				const FPlacement Choice = Options[PickWeighted(OptionWeights(Options), Stream)];
				Place(Choice);
				Types.Add(ENRGridRoomType::Combat);
				Environments.Add(Environment);
				const int32 RoomDepth = Depth[Parent] + 1;
				Depth.Add(RoomDepth);
			};
			if (!bWithHall)
			{
				TArray<FPlacement> Options;
				OptionsAt(From, Exits[Pick].Port, ENRGridRoomType::Combat, Environment, Options);
				if (Options.Num() == 0)
				{
					Exits.RemoveAt(Pick);
					continue;
				}
				PlaceRoom(From, Options);
				bPlaced = true;
				break;
			}

			// A hall cell against the doorway and a room against one of its other sides; a hall
			// cell with no room beyond it is taken back.
			TArray<FPlacement> HallOptions;
			OptionsAt(From, Exits[Pick].Port, ENRGridRoomType::Hall, Environment, HallOptions);
			WeightedOrder(HallOptions, Stream);
			for (const FPlacement& HallChoice : HallOptions)
			{
				Place(HallChoice);
				const int32 HallIndex = Placed.Num() - 1;
				Types.Add(ENRGridRoomType::Hall);
				Environments.Add(Environment);
				const int32 HallDepth = Depth[From];
				Depth.Add(HallDepth);
				TArray<FPlacement> RoomOptions;
				for (const FNRRoomPort& Port : HallChoice.Shape->Ports)
				{
					OptionsAt(HallIndex, Port, ENRGridRoomType::Combat, Environment, RoomOptions);
				}
				if (RoomOptions.Num() > 0)
				{
					PlaceRoom(HallIndex, RoomOptions);
					bPlaced = true;
					break;
				}
				Unplace();
				Types.Pop();
				Environments.Pop();
				Depth.Pop();
			}
			if (!bPlaced)
			{
				Exits.RemoveAt(Pick);
				continue;
			}
			--HallsLeft;
		}
		if (!bPlaced)
		{
			OutReason = bWithHall ? TEXT("no free place left for a hall branch") : TEXT("no free place left for a branch");
			return false;
		}
		++RoomsPlaced;
	}

	FNRGeneratedLayout Layout;
	Layout.GeneratorVersion = Version;
	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		const FPlacement& Placement = Placed[Index];
		FNRGeneratedRoom& Room = Layout.Rooms.AddDefaulted_GetRef();
		Room.RoomId = Index;
		Room.Cell = Placement.Anchor;
		Room.Footprint = Placement.Shape->Size;
		Room.RoomType = Types[Index];
		Room.Environment = Environments[Index];
		Room.MainPathIndex = Index < PathLength ? Index : INDEX_NONE;
		if (Placement.Parent != INDEX_NONE)
		{
			AddConnection(Layout, Placement.Parent, Index, Placement.FromCell, Placement.ToCell);
		}
	}
	Layout.StartRoomId = 0;
	Layout.BossRoomId = BossIndex;

	// 3. Loops: extra corridors between rooms facing each other doorway to doorway, never to
	//    the boss, never around a required room, never from Indoor straight to Outdoor.
	if (NeedsLoop(Settings.Shape))
	{
		const int32 SafeIndex = FindOfType(Layout, ENRGridRoomType::Safe);
		const int32 EliteIndex = FindOfType(Layout, ENRGridRoomType::Elite);
		const int32 LoopTarget = Stream.RandRange(Settings.MinLoops, Settings.MaxLoops);
		for (int32 Loop = 0; Loop < LoopTarget; ++Loop)
		{
			struct FLink
			{
				int32 A;
				int32 B;
				FNRRoomPort PortA;
				FNRRoomPort PortB;
				FIntPoint CellA;
				FIntPoint CellB;
			};
			TArray<FLink> Candidates;
			for (int32 A = 0; A < Placed.Num(); ++A)
			{
				for (const FNRRoomPort& PortA : Placed[A].Shape->Ports)
				{
					const FIntPoint CellA = Placed[A].Anchor + PortA.Cell;
					const FIntPoint CellB = CellA + NRGrid::ToOffset(PortA.Side);
					const int32* B = RoomAt.Find(CellB);
					// Each pair once, from its lower room.
					if (!B || *B <= A || A == BossIndex || *B == BossIndex || HasConnection(Layout, A, *B)
						|| (A == 0 && Used[A].Num() >= Settings.MaxStartConnections)
						|| !NRGrid::EnvironmentsMeet(Environments[A], Environments[*B]))
					{
						continue;
					}
					const FNRRoomPort PortB = MakePort(CellB - Placed[*B].Anchor, NRGrid::Opposite(PortA.Side));
					TArray<FNRRoomPort> UsedA = Used[A];
					TArray<FNRRoomPort> UsedB = Used[*B];
					UsedA.Add(PortA);
					UsedB.Add(PortB);
					if (!Placed[*B].Shape->HasPort(PortB) || !Placed[A].Shape->Covers(UsedA) || !Placed[*B].Shape->Covers(UsedB))
					{
						continue;
					}

					FNRGeneratedLayout Trial = Layout;
					AddConnection(Trial, A, *B, CellA, CellB);
					const TArray<TArray<int32>> Neighbours = Adjacency(Trial, IndexRooms(Trial));
					if ((SafeIndex != INDEX_NONE && Reaches(Neighbours, 0, BossIndex, SafeIndex))
						|| (EliteIndex != INDEX_NONE && Reaches(Neighbours, 0, BossIndex, EliteIndex)))
					{
						continue;
					}
					Candidates.Add({ A, *B, PortA, PortB, CellA, CellB });
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
			const FLink Pick = Candidates[Stream.RandRange(0, Candidates.Num() - 1)];
			AddConnection(Layout, Pick.A, Pick.B, Pick.CellA, Pick.CellB);
			Used[Pick.A].Add(Pick.PortA);
			Used[Pick.B].Add(Pick.PortB);
		}
	}

	// 4. Templates: for every room, a turn of a template of its shape with every doorway it uses.
	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		// A hall cell is exactly the corridor it carries: no blocked stub.
		const bool bHall = Types[Index] == ENRGridRoomType::Hall;
		const FTurnedRoom* Turned = PickTurn(*Placed[Index].Shape, Used[Index], Templates, bHall && InTemplates.Num() > 0, Stream);
		if (!Turned)
		{
			OutReason = bHall
				? FString::Printf(TEXT("no hall template has exactly the doorways hall cell %d uses"), Index)
				: FString::Printf(TEXT("no template has every doorway room %d uses"), Index);
			return false;
		}
		Layout.Rooms[Index].TemplateIndex = InTemplates.Num() > 0 ? Turned->Template : INDEX_NONE;
		Layout.Rooms[Index].RotationSteps = Turned->Steps;
	}

	// Doorway directions, for logs and one-cell rooms.
	for (const FNRGeneratedConnection& Connection : Layout.Connections)
	{
		ENRGridDirection Direction;
		if (NRGrid::DirectionBetween(Connection.CellA, Connection.CellB, Direction))
		{
			Layout.Rooms[Connection.RoomA].ConnectionMask |= 1 << static_cast<int32>(Direction);
			Layout.Rooms[Connection.RoomB].ConnectionMask |= 1 << static_cast<int32>(NRGrid::Opposite(Direction));
		}
	}

	// 5. Corridors: for every connection, one that joins its two rooms' environments.
	if (Corridors.Num() > 0)
	{
		for (FNRGeneratedConnection& Connection : Layout.Connections)
		{
			const ENRRoomEnvironment A = Environments[Connection.RoomA];
			const ENRRoomEnvironment B = Environments[Connection.RoomB];
			TArray<int32> Fitting;
			TArray<float> Weights;
			for (int32 Index = 0; Index < Corridors.Num(); ++Index)
			{
				if (Corridors[Index].Weight > 0.0f && Corridors[Index].Joins(A, B))
				{
					Fitting.Add(Index);
					Weights.Add(Corridors[Index].Weight);
				}
			}
			if (Fitting.Num() == 0)
			{
				OutReason = FString::Printf(TEXT("no corridor template joins %s and %s"), *EnvName(A), *EnvName(B));
				return false;
			}
			Connection.TemplateIndex = Fitting[PickWeighted(Weights, Stream)];
		}
	}

	const TArray<FString> Problems = Validate(Settings, InTemplates, Corridors, Layout);
	if (Problems.Num() > 0)
	{
		OutReason = FString::Printf(TEXT("invalid result: %s"), *Problems[0]);
		return false;
	}
	OutLayout = MoveTemp(Layout);
	return true;
}

TArray<FString> FNRGridLayoutGenerator::Validate(const FNRMapGenSettings& Settings, const TArray<FNRRoomTemplateDesc>& InTemplates,
	const TArray<FNRCorridorTemplateDesc>& Corridors, const FNRGeneratedLayout& Layout)
{
	using namespace NRGridGen;
	TArray<FString> Problems;
	const TMap<int32, int32> IndexById = IndexRooms(Layout);
	const TArray<FNRRoomTemplateDesc> Templates = Effective(InTemplates);

	if (IndexById.Num() != Layout.Rooms.Num())
	{
		Problems.Add(TEXT("duplicate room id"));
	}

	// Cells and templates.
	TMap<FIntPoint, int32> RoomAt;
	TArray<TOptional<FTurnedRoom>> Turned;
	Turned.SetNum(Layout.Rooms.Num());
	for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
	{
		const FNRGeneratedRoom& Room = Layout.Rooms[Index];
		if (Room.Footprint.X < 1 || Room.Footprint.Y < 1)
		{
			Problems.Add(FString::Printf(TEXT("room %d has footprint %dx%d"), Room.RoomId, Room.Footprint.X, Room.Footprint.Y));
			continue;
		}
		for (int32 Y = 0; Y < Room.Footprint.Y; ++Y)
		{
			for (int32 X = 0; X < Room.Footprint.X; ++X)
			{
				const FIntPoint Cell(Room.Cell.X + X, Room.Cell.Y + Y);
				if (!InGrid(Settings, Cell))
				{
					Problems.Add(FString::Printf(TEXT("room %d reaches outside the grid at %d,%d"), Room.RoomId, Cell.X, Cell.Y));
				}
				if (const int32* Other = RoomAt.Find(Cell))
				{
					Problems.Add(FString::Printf(TEXT("rooms %d and %d overlap at %d,%d"), Layout.Rooms[*Other].RoomId, Room.RoomId, Cell.X, Cell.Y));
				}
				else
				{
					RoomAt.Add(Cell, Index);
				}
			}
		}

		const int32 TemplateIndex = InTemplates.Num() > 0 ? Room.TemplateIndex : 0;
		if (InTemplates.Num() == 0 && Room.TemplateIndex != INDEX_NONE)
		{
			Problems.Add(FString::Printf(TEXT("room %d names template %d but there are no templates"), Room.RoomId, Room.TemplateIndex));
			continue;
		}
		if (!Templates.IsValidIndex(TemplateIndex))
		{
			Problems.Add(FString::Printf(TEXT("room %d has no template"), Room.RoomId));
			continue;
		}
		const FNRRoomTemplateDesc& Template = Templates[TemplateIndex];
		if (!(Template.TypeMask & TypeBit(Room.RoomType)))
		{
			Problems.Add(FString::Printf(TEXT("room %d: template %d does not allow %s"), Room.RoomId, TemplateIndex, *TypeName(Room.RoomType)));
		}
		if (InTemplates.Num() > 0 && Template.Environment != Room.Environment)
		{
			Problems.Add(FString::Printf(TEXT("room %d is %s but its template %d is %s"), Room.RoomId, *EnvName(Room.Environment),
				TemplateIndex, *EnvName(Template.Environment)));
		}
		if (Room.RotationSteps < 0 || Room.RotationSteps > 3 || !(Template.RotationMask & (1 << Room.RotationSteps)))
		{
			Problems.Add(FString::Printf(TEXT("room %d: template %d does not allow %d quarter turn(s)"), Room.RoomId, TemplateIndex, Room.RotationSteps));
			continue;
		}
		const FIntPoint Size = NRGrid::TurnFootprint(Template.Footprint, Room.RotationSteps);
		if (Size != Room.Footprint)
		{
			Problems.Add(FString::Printf(TEXT("room %d covers %dx%d, its template turned %d covers %dx%d"), Room.RoomId,
				Room.Footprint.X, Room.Footprint.Y, Room.RotationSteps, Size.X, Size.Y));
			continue;
		}
		Turned[Index] = Turn(Template, TemplateIndex, Room.RotationSteps);
	}
	const int32 Halls = CountHalls(Layout);
	const int32 RoomCount = Layout.Rooms.Num() - Halls;
	if (RoomCount < Settings.MinRooms || RoomCount > Settings.MaxRooms)
	{
		Problems.Add(FString::Printf(TEXT("%d rooms, wanted %d-%d"), RoomCount, Settings.MinRooms, Settings.MaxRooms));
	}
	if (Halls < Settings.MinHalls || Halls > Settings.MaxHalls)
	{
		Problems.Add(FString::Printf(TEXT("%d hall cells, wanted %d-%d"), Halls, Settings.MinHalls, Settings.MaxHalls));
	}

	// Environments the mode allows, and in a Hybrid map every one of them.
	{
		const TArray<ENRRoomEnvironment> Allowed = ModeEnvironments(Settings);
		const FString ModeName = StaticEnum<ENRMapEnvironmentMode>()->GetNameStringByValue(static_cast<int64>(Settings.EnvironmentMode));
		int32 Counts[EnvironmentCount] = { 0, 0, 0 };
		for (const FNRGeneratedRoom& Room : Layout.Rooms)
		{
			++Counts[static_cast<int32>(Room.Environment)];
			if (!Allowed.Contains(Room.Environment))
			{
				Problems.Add(FString::Printf(TEXT("room %d is %s, which a %s map does not have"), Room.RoomId, *EnvName(Room.Environment), *ModeName));
			}
		}
		for (ENRRoomEnvironment Environment : Allowed)
		{
			if (IsHybrid(Settings) && Counts[static_cast<int32>(Environment)] == 0)
			{
				Problems.Add(FString::Printf(TEXT("a Hybrid map without %s rooms"), *EnvName(Environment)));
			}
		}
	}

	// Corridors: between neighbouring cells of the two rooms, doorway to doorway.
	TSet<TPair<int32, int32>> Pairs;
	TArray<uint8> Masks;
	Masks.Init(0, Layout.Rooms.Num());
	for (const FNRGeneratedConnection& Connection : Layout.Connections)
	{
		const int32* A = IndexById.Find(Connection.RoomA);
		const int32* B = IndexById.Find(Connection.RoomB);
		if (!A || !B)
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d to a missing room"), Connection.RoomA, Connection.RoomB));
			continue;
		}
		if (Connection.RoomA >= Connection.RoomB)
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d not in order"), Connection.RoomA, Connection.RoomB));
		}
		bool bDuplicate = false;
		Pairs.Add(TPair<int32, int32>(Connection.RoomA, Connection.RoomB), &bDuplicate);
		if (bDuplicate)
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d twice"), Connection.RoomA, Connection.RoomB));
		}

		const FNRGeneratedRoom& RoomA = Layout.Rooms[*A];
		const FNRGeneratedRoom& RoomB = Layout.Rooms[*B];
		if (!NRGrid::EnvironmentsMeet(RoomA.Environment, RoomB.Environment))
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d joins %s straight to %s"), Connection.RoomA, Connection.RoomB,
				*EnvName(RoomA.Environment), *EnvName(RoomB.Environment)));
		}
		if (Corridors.Num() == 0 ? Connection.TemplateIndex != INDEX_NONE : !Corridors.IsValidIndex(Connection.TemplateIndex))
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d names corridor template %d of %d"), Connection.RoomA, Connection.RoomB,
				Connection.TemplateIndex, Corridors.Num()));
		}
		else if (Corridors.Num() > 0 && !Corridors[Connection.TemplateIndex].Joins(RoomA.Environment, RoomB.Environment))
		{
			const FNRCorridorTemplateDesc& Corridor = Corridors[Connection.TemplateIndex];
			Problems.Add(FString::Printf(TEXT("corridor %d-%d between %s and %s uses corridor template %d, which joins %s and %s"),
				Connection.RoomA, Connection.RoomB, *EnvName(RoomA.Environment), *EnvName(RoomB.Environment), Connection.TemplateIndex,
				*EnvName(Corridor.EndpointA), *EnvName(Corridor.EndpointB)));
		}
		ENRGridDirection Direction;
		if (!RoomA.Covers(Connection.CellA) || !RoomB.Covers(Connection.CellB))
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d runs from cells %d,%d to %d,%d outside its rooms"), Connection.RoomA, Connection.RoomB,
				Connection.CellA.X, Connection.CellA.Y, Connection.CellB.X, Connection.CellB.Y));
			continue;
		}
		if (!NRGrid::DirectionBetween(Connection.CellA, Connection.CellB, Direction))
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d joins cells that are not neighbours"), Connection.RoomA, Connection.RoomB));
			continue;
		}
		Masks[*A] |= 1 << static_cast<int32>(Direction);
		Masks[*B] |= 1 << static_cast<int32>(NRGrid::Opposite(Direction));
		if (Turned[*A].IsSet() && !Turned[*A]->HasPort(Connection.CellA - RoomA.Cell, Direction))
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d leaves room %d at %d,%d %s where it has no doorway"), Connection.RoomA, Connection.RoomB,
				RoomA.RoomId, Connection.CellA.X, Connection.CellA.Y, NRGrid::Letter(Direction)));
		}
		if (Turned[*B].IsSet() && !Turned[*B]->HasPort(Connection.CellB - RoomB.Cell, NRGrid::Opposite(Direction)))
		{
			Problems.Add(FString::Printf(TEXT("corridor %d-%d enters room %d at %d,%d %s where it has no doorway"), Connection.RoomA, Connection.RoomB,
				RoomB.RoomId, Connection.CellB.X, Connection.CellB.Y, NRGrid::Letter(NRGrid::Opposite(Direction))));
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
	const int32 StartBoss = CellDistance(Layout.Rooms[*StartIndex], Layout.Rooms[*BossIndex]);
	if (StartBoss < Settings.MinStartBossDistance)
	{
		Problems.Add(FString::Printf(TEXT("start and boss are %d cell(s) apart, at least %d wanted"), StartBoss, Settings.MinStartBossDistance));
	}
	if (Neighbours[*StartIndex].Num() > Settings.MaxStartConnections)
	{
		Problems.Add(FString::Printf(TEXT("start has %d corridors, at most %d allowed"), Neighbours[*StartIndex].Num(), Settings.MaxStartConnections));
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
	if (Neighbours[*BossIndex].ContainsByPredicate([&Layout](int32 Next) { return IsHall(Layout.Rooms[Next]); }))
	{
		Problems.Add(TEXT("the boss hangs off a hall cell"));
	}

	// Hall cells: a way through (two corridors or more), with no doorway they do not use.
	for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
	{
		const FNRGeneratedRoom& Room = Layout.Rooms[Index];
		if (!IsHall(Room))
		{
			continue;
		}
		if (Neighbours[Index].Num() < 2)
		{
			Problems.Add(FString::Printf(TEXT("hall cell %d is a dead end"), Room.RoomId));
		}
		if (InTemplates.Num() > 0 && Turned[Index].IsSet() && Turned[Index]->Ports.Num() != Neighbours[Index].Num())
		{
			Problems.Add(FString::Printf(TEXT("hall cell %d has %d doorway(s) but %d corridor(s)"), Room.RoomId,
				Turned[Index]->Ports.Num(), Neighbours[Index].Num()));
		}
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
	else
	{
		const int32 PathRooms = MainPath.FilterByPredicate([&Layout](int32 Entry) { return !IsHall(Layout.Rooms[Entry - 1]); }).Num();
		if (PathRooms < Settings.MinMainPathLength || PathRooms > Settings.MaxMainPathLength)
		{
			Problems.Add(FString::Printf(TEXT("main path of %d rooms, wanted %d-%d"), PathRooms, Settings.MinMainPathLength, Settings.MaxMainPathLength));
		}

		// Hybrid: the main path's rooms run start side, Transition, other side - in that order.
		if (IsHybrid(Settings))
		{
			TArray<ENRRoomEnvironment> Sequence;
			FString Letters;
			for (int32 Entry : MainPath)
			{
				if (!IsHall(Layout.Rooms[Entry - 1]))
				{
					Sequence.Add(Layout.Rooms[Entry - 1].Environment);
					Letters.AppendChar(NRGrid::EnvironmentLetter(Sequence.Last()));
				}
			}
			const ENRRoomEnvironment From = Settings.bRandomizeHybridStart ? Sequence[0] : Settings.HybridStartEnvironment;
			int32 Index = 0;
			while (Index < Sequence.Num() && Sequence[Index] == From)
			{
				++Index;
			}
			const int32 Before = Index;
			while (Index < Sequence.Num() && Sequence[Index] == ENRRoomEnvironment::Transition)
			{
				++Index;
			}
			const int32 Across = Index - Before;
			const int32 After = Index;
			while (Index < Sequence.Num() && Sequence[Index] == OtherSide(From))
			{
				++Index;
			}
			if (From == ENRRoomEnvironment::Transition || Before == 0 || Index == After || Index != Sequence.Num()
				|| Across < Settings.MinTransitionRooms || Across > Settings.MaxTransitionRooms)
			{
				Problems.Add(FString::Printf(TEXT("main path rooms %s do not run from %s through %d-%d Transition room(s) to %s"), *Letters,
					*EnvName(From), Settings.MinTransitionRooms, Settings.MaxTransitionRooms, *EnvName(OtherSide(From))));
			}
		}
	}

	// Branch depth: every room within MaxBranchDepth rooms of the main path (hall cells on the
	// way not counted).
	{
		TArray<int32> Distance;
		Distance.Init(MAX_int32, Layout.Rooms.Num());
		for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
		{
			if (Layout.Rooms[Index].MainPathIndex != INDEX_NONE)
			{
				Distance[Index] = 0;
			}
		}
		for (bool bChanged = true; bChanged;)
		{
			bChanged = false;
			for (int32 Index = 0; Index < Layout.Rooms.Num(); ++Index)
			{
				if (Distance[Index] == MAX_int32)
				{
					continue;
				}
				for (int32 Next : Neighbours[Index])
				{
					const int32 Step = Distance[Index] + (IsHall(Layout.Rooms[Next]) ? 0 : 1);
					if (Step < Distance[Next])
					{
						Distance[Next] = Step;
						bChanged = true;
					}
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

int32 FNRGridLayoutGenerator::CellDistance(const FNRGeneratedRoom& A, const FNRGeneratedRoom& B)
{
	return NRGridGen::FootprintDistance(A.Cell, A.Footprint, B.Cell, B.Footprint);
}

int32 FNRGridLayoutGenerator::CountLoops(const FNRGeneratedLayout& Layout)
{
	return Layout.Rooms.Num() > 0 ? FMath::Max(Layout.Connections.Num() - Layout.Rooms.Num() + 1, 0) : 0;
}

int32 FNRGridLayoutGenerator::CountBranchRooms(const FNRGeneratedLayout& Layout)
{
	return Layout.Rooms.FilterByPredicate([](const FNRGeneratedRoom& Room) { return Room.MainPathIndex == INDEX_NONE && !NRGridGen::IsHall(Room); }).Num();
}

int32 FNRGridLayoutGenerator::CountHalls(const FNRGeneratedLayout& Layout)
{
	return Layout.Rooms.FilterByPredicate([](const FNRGeneratedRoom& Room) { return NRGridGen::IsHall(Room); }).Num();
}

FString FNRGridLayoutGenerator::ToAscii(const FNRMapGenSettings& Settings, const FNRGeneratedLayout& Layout, bool bEnvironment)
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
	auto Put = [&Rows, Width, Height](int32 X, int32 Y, TCHAR Mark)
	{
		if (X >= 0 && Y >= 0 && X < Width && Y < Height)
		{
			Rows[Height - 1 - Y][X] = Mark;
		}
	};
	for (const FNRGeneratedRoom& Room : Layout.Rooms)
	{
		TCHAR Mark = Room.MainPathIndex == INDEX_NONE ? TEXT('b') : TEXT('C');
		switch (Room.RoomType)
		{
		case ENRGridRoomType::Start: Mark = TEXT('S'); break;
		case ENRGridRoomType::Safe: Mark = TEXT('F'); break;
		case ENRGridRoomType::Elite: Mark = TEXT('E'); break;
		case ENRGridRoomType::Boss: Mark = TEXT('B'); break;
		case ENRGridRoomType::Hall: Mark = TEXT('+'); break;
		default: break;
		}
		if (bEnvironment)
		{
			Mark = NRGrid::EnvironmentLetter(Room.Environment);
		}
		// The room's cells, and = over the gaps between them.
		for (int32 Y = 0; Y < Room.Footprint.Y * 2 - 1; ++Y)
		{
			for (int32 X = 0; X < Room.Footprint.X * 2 - 1; ++X)
			{
				Put(Room.Cell.X * 2 + X, Room.Cell.Y * 2 + Y, (X % 2 == 0 && Y % 2 == 0) ? Mark : TEXT('='));
			}
		}
	}
	for (const FNRGeneratedConnection& Connection : Layout.Connections)
	{
		Put(Connection.CellA.X + Connection.CellB.X, Connection.CellA.Y + Connection.CellB.Y,
			Connection.CellA.X == Connection.CellB.X ? TEXT('|') : TEXT('-'));
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
		if (RoomA.RoomId != RoomB.RoomId || RoomA.Cell != RoomB.Cell || RoomA.Footprint != RoomB.Footprint || RoomA.RoomType != RoomB.RoomType
			|| RoomA.Environment != RoomB.Environment || RoomA.ConnectionMask != RoomB.ConnectionMask || RoomA.MainPathIndex != RoomB.MainPathIndex
			|| RoomA.TemplateIndex != RoomB.TemplateIndex || RoomA.RotationSteps != RoomB.RotationSteps)
		{
			return false;
		}
	}
	for (int32 Index = 0; Index < A.Connections.Num(); ++Index)
	{
		const FNRGeneratedConnection& LinkA = A.Connections[Index];
		const FNRGeneratedConnection& LinkB = B.Connections[Index];
		if (LinkA.RoomA != LinkB.RoomA || LinkA.RoomB != LinkB.RoomB || LinkA.CellA != LinkB.CellA || LinkA.CellB != LinkB.CellB
			|| LinkA.TemplateIndex != LinkB.TemplateIndex)
		{
			return false;
		}
	}
	return true;
}

FString FNRGridLayoutGenerator::EnvironmentName(ENRRoomEnvironment Environment)
{
	return StaticEnum<ENRRoomEnvironment>()->GetNameStringByValue(static_cast<int64>(Environment));
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
