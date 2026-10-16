// Copyright Epic Games, Inc. All Rights Reserved.

// Checks for FNRGridLayoutGenerator: many fixed seeds per shape (valid, reproducible, varied),
// with one-cell rooms and with rooms of several cells, with hall cells (straight, corner, T and
// crossing corridors between rooms), settings and templates that cannot be met (refused with a
// reason), and broken layouts Validate must catch. Runs as the automation
// test ProjectNR.Grid.LayoutGenerator and from the console as NR.Grid.Test [seeds].

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "NRGridLayoutGenerator.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRGridTest, Log, All);

namespace NRGridTests
{
	const ENRGridLayoutShape Shapes[] = { ENRGridLayoutShape::Linear, ENRGridLayoutShape::Branching, ENRGridLayoutShape::Loop, ENRGridLayoutShape::Mixed };

	FString ShapeName(ENRGridLayoutShape Shape)
	{
		return StaticEnum<ENRGridLayoutShape>()->GetNameStringByValue(static_cast<int64>(Shape));
	}

	/** The plan's first-version numbers, with the room counts a Linear map can actually have. */
	FNRMapGenSettings SettingsFor(ENRGridLayoutShape Shape, const FIntPoint& GridSize = FIntPoint(6, 6))
	{
		FNRMapGenSettings Settings;
		Settings.Shape = Shape;
		Settings.GridSize = GridSize;
		if (Shape == ENRGridLayoutShape::Linear)
		{
			// Every room is on the main path, so the path has to be the full 10-14.
			Settings.MinMainPathLength = 10;
			Settings.MaxMainPathLength = 14;
		}
		return Settings;
	}

	uint8 Bit(ENRGridRoomType Type)
	{
		return static_cast<uint8>(1 << static_cast<int32>(Type));
	}

	FNRRoomPort Port(int32 X, int32 Y, ENRGridDirection Side)
	{
		FNRRoomPort Made;
		Made.Cell = FIntPoint(X, Y);
		Made.Side = Side;
		return Made;
	}

	FNRRoomTemplateDesc WithPorts(const FIntPoint& Footprint, const TArray<FNRRoomPort>& Ports, uint8 Types, float Weight = 1.0f)
	{
		FNRRoomTemplateDesc Desc;
		Desc.Footprint = Footprint;
		Desc.Ports = Ports;
		Desc.TypeMask = Types;
		Desc.Weight = Weight;
		return Desc;
	}

	struct FTemplateSet
	{
		const TCHAR* Name;
		TArray<FNRRoomTemplateDesc> Templates;
		FIntPoint GridSize;
		/** Footprint cells the boss must cover, 0 for any. */
		int32 BossCells = 0;
	};

	TArray<FTemplateSet> TemplateSets()
	{
		const uint8 All = 0b11111;
		const uint8 N = 1 << 0, E = 1 << 1, S = 1 << 2, W = 1 << 3;
		const uint8 StartSafe = Bit(ENRGridRoomType::Start) | Bit(ENRGridRoomType::Safe);
		const uint8 Combat = Bit(ENRGridRoomType::Combat);
		const uint8 Elite = Bit(ENRGridRoomType::Elite);
		const uint8 Boss = Bit(ENRGridRoomType::Boss);
		const FIntPoint One(1, 1);

		TArray<FTemplateSet> Sets;
		Sets.Add({ TEXT("single template"), { FNRRoomTemplateDesc::Make(One, N | E | S | W, All) }, FIntPoint(6, 6) });
		Sets.Add({ TEXT("shaped templates"), {
			FNRRoomTemplateDesc::Make(One, N, All),             // dead end
			FNRRoomTemplateDesc::Make(One, N | S, All),         // straight
			FNRRoomTemplateDesc::Make(One, N | E, All),         // corner
			FNRRoomTemplateDesc::Make(One, N | E | S, All),     // T
			FNRRoomTemplateDesc::Make(One, N | E | S | W, All, 0.2f), // cross, rarely
		}, FIntPoint(6, 6) });
		// The plan's defaults: start and safe 1x1, combat 1x1 or 1x2, elite 1x2, boss 2x2.
		Sets.Add({ TEXT("multi-cell rooms"), {
			FNRRoomTemplateDesc::Make(One, N | E | S | W, StartSafe),
			FNRRoomTemplateDesc::Make(One, N | E | S | W, Combat),
			FNRRoomTemplateDesc::Make(FIntPoint(1, 2), N | E | S | W, Combat),
			FNRRoomTemplateDesc::Make(FIntPoint(1, 2), N | E | S | W, Elite),
			FNRRoomTemplateDesc::Make(FIntPoint(2, 2), N | E | S | W, Boss),
		}, FIntPoint(8, 8), 4 });
		// Rooms with only some places on their outline: a long room open at its two ends, an
		// elite open on three sides, a boss with one door.
		Sets.Add({ TEXT("sparse multi-cell rooms"), {
			FNRRoomTemplateDesc::Make(One, N | E | S | W, StartSafe),
			FNRRoomTemplateDesc::Make(One, N | E | S | W, Combat),
			WithPorts(FIntPoint(1, 2), { Port(0, 0, ENRGridDirection::South), Port(0, 1, ENRGridDirection::North) }, Combat),
			WithPorts(FIntPoint(2, 1), { Port(0, 0, ENRGridDirection::West), Port(1, 0, ENRGridDirection::East), Port(1, 0, ENRGridDirection::North) }, Elite),
			WithPorts(FIntPoint(2, 2), { Port(0, 0, ENRGridDirection::South) }, Boss),
		}, FIntPoint(8, 8), 4 });
		return Sets;
	}

	/** Corridors a room has. */
	int32 Degree(const FNRGeneratedLayout& Layout, int32 RoomId)
	{
		return Layout.Connections.FilterByPredicate([RoomId](const FNRGeneratedConnection& Connection)
		{
			return Connection.RoomA == RoomId || Connection.RoomB == RoomId;
		}).Num();
	}

	/** Runs every check; returns the failures, each naming its seed or settings. */
	TArray<FString> RunSuite(int32 SeedCount, FString& OutSummary)
	{
		TArray<FString> Failures;
		TArray<FString> Summary;

		// Start and boss corridors: the start room takes at most MaxStartConnections, the boss
		// exactly one, whatever the shape and templates.
		for (const int32 MaxStart : { 2, 1 })
		{
			int32 StartDegrees[5] = { 0, 0, 0, 0, 0 };
			int32 BossNotOne = 0;
			int32 TooClose = 0;
			TMap<int32, int32> StartBoss;
			int32 Generated = 0;
			for (ENRGridLayoutShape Shape : Shapes)
			{
				FNRMapGenSettings Settings = SettingsFor(Shape, FIntPoint(8, 8));
				Settings.MaxStartConnections = MaxStart;
				for (int32 Seed = 1; Seed <= SeedCount; ++Seed)
				{
					FNRGeneratedLayout Layout;
					FString Error;
					if (!FNRGridLayoutGenerator::Generate(Settings, TemplateSets()[2].Templates, Seed, Layout, Error))
					{
						Failures.Add(FString::Printf(TEXT("start limit %d, %s seed %d: %s"), MaxStart, *ShapeName(Shape), Seed, *Error));
						continue;
					}
					++Generated;
					const int32 StartDegree = Degree(Layout, Layout.StartRoomId);
					++StartDegrees[FMath::Clamp(StartDegree, 0, 4)];
					BossNotOne += Degree(Layout, Layout.BossRoomId) != 1 ? 1 : 0;
					const int32 Apart = FNRGridLayoutGenerator::CellDistance(*Layout.FindRoom(Layout.StartRoomId), *Layout.FindRoom(Layout.BossRoomId));
					++StartBoss.FindOrAdd(Apart);
					TooClose += Apart < Settings.MinStartBossDistance ? 1 : 0;
					if (StartDegree > MaxStart)
					{
						Failures.Add(FString::Printf(TEXT("start limit %d, %s seed %d: start has %d corridors"), MaxStart, *ShapeName(Shape), Seed, StartDegree));
					}
				}
			}
			if (TooClose > 0)
			{
				Failures.Add(FString::Printf(TEXT("start limit %d: %d layout(s) with start and boss closer than %d cells"), MaxStart, TooClose, FNRMapGenSettings().MinStartBossDistance));
			}
			StartBoss.KeySort(TLess<int32>());
			FString Apart;
			for (const TPair<int32, int32>& Pair : StartBoss)
			{
				Apart += FString::Printf(TEXT(" %d:%d"), Pair.Key, Pair.Value);
			}
			Summary.Add(FString::Printf(TEXT("start limit %d: start-boss cell distance (cells:layouts)%s"), MaxStart, *Apart));
			if (BossNotOne > 0)
			{
				Failures.Add(FString::Printf(TEXT("start limit %d: %d layout(s) with a boss not on exactly one corridor"), MaxStart, BossNotOne));
			}
			Summary.Add(FString::Printf(TEXT("start limit %d (multi-cell rooms, 4 shapes): %d generated; start corridors 1:%d 2:%d 3+:%d; boss not on 1 corridor: %d"),
				MaxStart, Generated, StartDegrees[1], StartDegrees[2], StartDegrees[3] + StartDegrees[4], BossNotOne));
		}

		// One-cell rooms, no templates.
		for (ENRGridLayoutShape Shape : Shapes)
		{
			const FNRMapGenSettings Settings = SettingsFor(Shape);
			const FString Name = ShapeName(Shape);
			int32 Generated = 0;
			int32 Retried = 0;
			int32 Rooms = 0;
			int32 Loops = 0;
			int32 Branches = 0;
			TSet<FString> Distinct;

			for (int32 Seed = 1; Seed <= SeedCount; ++Seed)
			{
				FNRGeneratedLayout Layout;
				FString Error;
				if (!FNRGridLayoutGenerator::Generate(Settings, Seed, Layout, Error))
				{
					Failures.Add(FString::Printf(TEXT("%s seed %d: %s"), *Name, Seed, *Error));
					continue;
				}

				for (const FString& Problem : FNRGridLayoutGenerator::Validate(Settings, {}, Layout))
				{
					Failures.Add(FString::Printf(TEXT("%s seed %d: %s"), *Name, Seed, *Problem));
				}

				FNRGeneratedLayout Again;
				FString AgainError;
				if (!FNRGridLayoutGenerator::Generate(Settings, Seed, Again, AgainError) || !FNRGridLayoutGenerator::Identical(Layout, Again))
				{
					Failures.Add(FString::Printf(TEXT("%s seed %d: not reproducible"), *Name, Seed));
				}
				if (Layout.Rooms.ContainsByPredicate([](const FNRGeneratedRoom& Room) { return Room.Footprint != FIntPoint(1, 1) || Room.TemplateIndex != INDEX_NONE; }))
				{
					Failures.Add(FString::Printf(TEXT("%s seed %d: a room without templates is not a plain one-cell room"), *Name, Seed));
				}

				++Generated;
				Retried += Layout.Attempt > 0 ? 1 : 0;
				Rooms += Layout.Rooms.Num();
				Loops += FNRGridLayoutGenerator::CountLoops(Layout);
				Branches += FNRGridLayoutGenerator::CountBranchRooms(Layout);
				Distinct.Add(FNRGridLayoutGenerator::ToAscii(Settings, Layout));
			}

			Summary.Add(FString::Printf(TEXT("%s: %d/%d generated, %d distinct, %d needed a retry, avg %.1f rooms, %.2f loops, %.1f branch rooms"),
				*Name, Generated, SeedCount, Distinct.Num(), Retried,
				Generated ? static_cast<float>(Rooms) / Generated : 0.0f,
				Generated ? static_cast<float>(Loops) / Generated : 0.0f,
				Generated ? static_cast<float>(Branches) / Generated : 0.0f));
			if (Generated > 1 && Distinct.Num() < 2)
			{
				Failures.Add(FString::Printf(TEXT("%s: every seed gave the same layout"), *Name));
			}
		}

		// Template sets: every seed of every shape must give a valid layout whose rooms all stand
		// in a template that allows their type and turn, cover what the turned template covers,
		// and meet every corridor with a doorway - the same way every time.
		for (const FTemplateSet& Set : TemplateSets())
		{
			TArray<int32> Uses;
			Uses.Init(0, Set.Templates.Num());
			int32 Turns[4] = { 0, 0, 0, 0 };
			int32 Rooms = 0;
			int32 Cells = 0;
			int32 BigRooms = 0;
			int32 Generated = 0;
			int32 Tried = 0;
			int32 Retried = 0;
			int32 Loops = 0;
			TSet<FString> Distinct;
			for (ENRGridLayoutShape Shape : Shapes)
			{
				const FNRMapGenSettings Settings = SettingsFor(Shape, Set.GridSize);
				for (int32 Seed = 1; Seed <= SeedCount; ++Seed)
				{
					++Tried;
					FNRGeneratedLayout Layout;
					FString Error;
					if (!FNRGridLayoutGenerator::Generate(Settings, Set.Templates, Seed, Layout, Error))
					{
						Failures.Add(FString::Printf(TEXT("%s, %s seed %d: %s"), Set.Name, *ShapeName(Shape), Seed, *Error));
						continue;
					}
					for (const FString& Problem : FNRGridLayoutGenerator::Validate(Settings, Set.Templates, Layout))
					{
						Failures.Add(FString::Printf(TEXT("%s, %s seed %d: %s"), Set.Name, *ShapeName(Shape), Seed, *Problem));
					}
					FNRGeneratedLayout Again;
					if (!FNRGridLayoutGenerator::Generate(Settings, Set.Templates, Seed, Again, Error) || !FNRGridLayoutGenerator::Identical(Layout, Again))
					{
						Failures.Add(FString::Printf(TEXT("%s, %s seed %d: not reproducible"), Set.Name, *ShapeName(Shape), Seed));
					}
					if (Set.BossCells > 0)
					{
						const FNRGeneratedRoom* Boss = Layout.FindRoom(Layout.BossRoomId);
						if (!Boss || Boss->Footprint.X * Boss->Footprint.Y != Set.BossCells)
						{
							Failures.Add(FString::Printf(TEXT("%s, %s seed %d: boss does not cover %d cells"), Set.Name, *ShapeName(Shape), Seed, Set.BossCells));
						}
					}

					++Generated;
					Retried += Layout.Attempt > 0 ? 1 : 0;
					Loops += FNRGridLayoutGenerator::CountLoops(Layout);
					Distinct.Add(FNRGridLayoutGenerator::ToAscii(Settings, Layout));
					for (const FNRGeneratedRoom& Room : Layout.Rooms)
					{
						if (Uses.IsValidIndex(Room.TemplateIndex))
						{
							++Uses[Room.TemplateIndex];
						}
						++Turns[Room.RotationSteps & 3];
						++Rooms;
						Cells += Room.Footprint.X * Room.Footprint.Y;
						BigRooms += Room.Footprint.X * Room.Footprint.Y > 1 ? 1 : 0;
					}
				}
			}
			FString UseText;
			for (int32 Index = 0; Index < Uses.Num(); ++Index)
			{
				UseText += FString::Printf(TEXT(" t%d=%d"), Index, Uses[Index]);
				if (Uses[Index] == 0 && Generated > 0)
				{
					Failures.Add(FString::Printf(TEXT("%s: template %d never used"), Set.Name, Index));
				}
			}
			Summary.Add(FString::Printf(TEXT("%s: %d/%d generated, %d distinct, %d needed a retry, %d rooms (%d of several cells, %d cells), %.2f loops; turns %d/%d/%d/%d; uses%s"),
				Set.Name, Generated, Tried, Distinct.Num(), Retried, Rooms, BigRooms, Cells,
				Generated ? static_cast<float>(Loops) / Generated : 0.0f, Turns[0], Turns[1], Turns[2], Turns[3], *UseText));
		}

		// Templates that cannot make a map must be refused before anything is placed.
		{
			const uint8 All = 0b11111;
			const FIntPoint One(1, 1);
			struct FRefusal
			{
				const TCHAR* Label;
				TArray<FNRRoomTemplateDesc> Templates;
			};
			const TArray<FRefusal> Refusals = {
				{ TEXT("no boss template"), { FNRRoomTemplateDesc::Make(One, 0b1111, All & ~Bit(ENRGridRoomType::Boss)) } },
				{ TEXT("boss template 7x1 in a 6x6 grid"), {
					FNRRoomTemplateDesc::Make(One, 0b1111, All & ~Bit(ENRGridRoomType::Boss)),
					FNRRoomTemplateDesc::Make(FIntPoint(7, 1), 0b1111, Bit(ENRGridRoomType::Boss)) } },
				{ TEXT("doorway facing into its own room"), {
					FNRRoomTemplateDesc::Make(One, 0b1111, All),
					WithPorts(FIntPoint(2, 1), { Port(0, 0, ENRGridDirection::East) }, Bit(ENRGridRoomType::Combat)) } },
			};
			for (const FRefusal& Refusal : Refusals)
			{
				FNRGeneratedLayout Layout;
				FString Error;
				if (FNRGridLayoutGenerator::Generate(SettingsFor(ENRGridLayoutShape::Mixed), Refusal.Templates, 5, Layout, Error))
				{
					Failures.Add(FString::Printf(TEXT("templates with '%s' were accepted"), Refusal.Label));
				}
				else
				{
					Summary.Add(FString::Printf(TEXT("refusal '%s': %s"), Refusal.Label, *Error));
				}
			}
		}

		// Broken layouts Validate must catch.
		{
			const FTemplateSet Set = TemplateSets()[2];
			const FNRMapGenSettings Settings = SettingsFor(ENRGridLayoutShape::Mixed, Set.GridSize);
			FNRGeneratedLayout Good;
			FString Error;
			if (!FNRGridLayoutGenerator::Generate(Settings, Set.Templates, 3, Good, Error))
			{
				Failures.Add(FString::Printf(TEXT("broken-layout checks: seed 3 did not generate: %s"), *Error));
			}
			else
			{
				struct FBreak
				{
					const TCHAR* Label;
					const TCHAR* Expect;
					TFunction<void(FNRGeneratedLayout&)> Apply;
				};
				const int32 BossIndex = Good.Rooms.IndexOfByPredicate([&Good](const FNRGeneratedRoom& Room) { return Room.RoomId == Good.BossRoomId; });
				const TArray<FBreak> Breaks = {
					{ TEXT("two rooms on one cell"), TEXT("overlap"), [](FNRGeneratedLayout& Layout) { Layout.Rooms[1].Cell = Layout.Rooms[0].Cell; } },
					{ TEXT("boss footprint shrunk"), TEXT("covers"), [BossIndex](FNRGeneratedLayout& Layout) { Layout.Rooms[BossIndex].Footprint = FIntPoint(1, 1); } },
					{ TEXT("corridor from a cell outside its room"), TEXT("outside its rooms"), [](FNRGeneratedLayout& Layout) { Layout.Connections[0].CellA += FIntPoint(5, 5); } },
					{ TEXT("room pushed off the grid"), TEXT("outside the grid"), [](FNRGeneratedLayout& Layout) { Layout.Rooms.Last().Cell = FIntPoint(7, 7) + Layout.Rooms.Last().Footprint; } },
				};
				if (!FNRGridLayoutGenerator::Validate(Settings, Set.Templates, Good).IsEmpty())
				{
					Failures.Add(TEXT("broken-layout checks: the unbroken layout is not valid"));
				}
				for (const FBreak& Break : Breaks)
				{
					FNRGeneratedLayout Broken = Good;
					Break.Apply(Broken);
					const TArray<FString> Problems = FNRGridLayoutGenerator::Validate(Settings, Set.Templates, Broken);
					if (!Problems.ContainsByPredicate([&Break](const FString& Problem) { return Problem.Contains(Break.Expect); }))
					{
						Failures.Add(FString::Printf(TEXT("broken layout '%s' not caught (%s)"), Break.Label,
							Problems.Num() > 0 ? *FString::Join(Problems, TEXT("; ")) : TEXT("no problem reported")));
					}
					else
					{
						Summary.Add(FString::Printf(TEXT("caught '%s': %s"), Break.Label, *Problems[0]));
					}
				}
				TArray<FString> Lines;
				FNRGridLayoutGenerator::ToAscii(Settings, Good).ParseIntoArrayLines(Lines, false);
				Summary.Add(TEXT("multi-cell rooms, Mixed seed 3:"));
				for (const FString& Line : Lines)
				{
					Summary.Add(FString::Printf(TEXT("    %s"), *Line));
				}
			}
		}

		// Hall cells: corridor cells between rooms, on the main path and on branches - never a
		// dead end, always the template with exactly the corridors they join.
		{
			const uint8 Hall = Bit(ENRGridRoomType::Hall);
			const uint8 N = 1 << 0, E = 1 << 1, S = 1 << 2, W = 1 << 3;
			const TCHAR* KindNames[] = { TEXT("straight"), TEXT("corner"), TEXT("T"), TEXT("crossing") };
			TArray<FNRRoomTemplateDesc> Templates = TemplateSets()[2].Templates;
			const int32 FirstHall = Templates.Num();
			Templates.Add(FNRRoomTemplateDesc::Make(FIntPoint(1, 1), N | S, Hall));
			Templates.Add(FNRRoomTemplateDesc::Make(FIntPoint(1, 1), N | E, Hall));
			Templates.Add(FNRRoomTemplateDesc::Make(FIntPoint(1, 1), N | E | S, Hall));
			Templates.Add(FNRRoomTemplateDesc::Make(FIntPoint(1, 1), N | E | S | W, Hall));

			for (ENRGridLayoutShape Shape : Shapes)
			{
				FNRMapGenSettings Settings = SettingsFor(Shape, FIntPoint(8, 8));
				Settings.MinHalls = 3;
				Settings.MaxHalls = 6;
				const FString Name = ShapeName(Shape);
				int32 Generated = 0;
				int32 Retried = 0;
				int32 Halls = 0;
				int32 OnPath = 0;
				int32 Kinds[4] = { 0, 0, 0, 0 };
				for (int32 Seed = 1; Seed <= SeedCount; ++Seed)
				{
					FNRGeneratedLayout Layout;
					FString Error;
					if (!FNRGridLayoutGenerator::Generate(Settings, Templates, Seed, Layout, Error))
					{
						Failures.Add(FString::Printf(TEXT("hall cells, %s seed %d: %s"), *Name, Seed, *Error));
						continue;
					}
					++Generated;
					Retried += Layout.Attempt > 0 ? 1 : 0;
					FNRGeneratedLayout Again;
					FString AgainError;
					if (!FNRGridLayoutGenerator::Generate(Settings, Templates, Seed, Again, AgainError) || !FNRGridLayoutGenerator::Identical(Layout, Again))
					{
						Failures.Add(FString::Printf(TEXT("hall cells, %s seed %d: not reproducible"), *Name, Seed));
					}
					for (const FNRGeneratedRoom& Room : Layout.Rooms)
					{
						if (Room.RoomType != ENRGridRoomType::Hall)
						{
							if (Room.TemplateIndex >= FirstHall)
							{
								Failures.Add(FString::Printf(TEXT("hall cells, %s seed %d: room %d is a hall template"), *Name, Seed, Room.RoomId));
							}
							continue;
						}
						++Halls;
						OnPath += Room.MainPathIndex != INDEX_NONE ? 1 : 0;
						const int32 Corridors = Degree(Layout, Room.RoomId);
						const bool bStraight = Room.ConnectionMask == (N | S) || Room.ConnectionMask == (E | W);
						const int32 Kind = Corridors <= 2 ? (bStraight ? 0 : 1) : Corridors == 3 ? 2 : 3;
						++Kinds[Kind];
						if (Room.TemplateIndex - FirstHall != Kind)
						{
							Failures.Add(FString::Printf(TEXT("hall cells, %s seed %d: hall cell %d joins %s (%s) but is template %d"),
								*Name, Seed, Room.RoomId, KindNames[Kind], *FNRGridLayoutGenerator::MaskToString(Room.ConnectionMask), Room.TemplateIndex));
						}
					}
					if (Shape == ENRGridLayoutShape::Mixed && Seed == 1)
					{
						Summary.Add(TEXT("hall cells, Mixed seed 1:"));
						TArray<FString> Lines;
						FNRGridLayoutGenerator::ToAscii(Settings, Layout).ParseIntoArrayLines(Lines);
						for (const FString& Line : Lines)
						{
							Summary.Add(FString::Printf(TEXT("    %s"), *Line));
						}
					}
				}
				Summary.Add(FString::Printf(TEXT("hall cells, %s: %d/%d generated, %d needed a retry, %.1f hall cells a map (%.0f%% on the main path); straight %d, corner %d, T %d, crossing %d"),
					*Name, Generated, SeedCount, Retried, Generated > 0 ? static_cast<float>(Halls) / Generated : 0.0f,
					Halls > 0 ? 100.0f * OnPath / Halls : 0.0f, Kinds[0], Kinds[1], Kinds[2], Kinds[3]));
				if (Shape != ENRGridLayoutShape::Linear && Generated > 0 && Kinds[2] + Kinds[3] == 0)
				{
					Failures.Add(FString::Printf(TEXT("hall cells, %s: no hall cell ever became a T or a crossing"), *Name));
				}
			}

			// Hall cells asked for with no hall template: refused, not filled with rooms.
			FNRMapGenSettings Settings = SettingsFor(ENRGridLayoutShape::Branching, FIntPoint(8, 8));
			Settings.MinHalls = 3;
			Settings.MaxHalls = 6;
			FNRGeneratedLayout Layout;
			FString Error;
			if (FNRGridLayoutGenerator::Generate(Settings, TemplateSets()[2].Templates, 7, Layout, Error))
			{
				Failures.Add(TEXT("hall cells without a hall template were accepted"));
			}
			else
			{
				Summary.Add(FString::Printf(TEXT("refusal 'hall cells without a hall template': %s"), *Error));
			}
		}

		// Settings that cannot be met must be refused, with a reason, and never half-filled.
		struct FImpossible
		{
			const TCHAR* Label;
			FNRMapGenSettings Settings;
		};
		TArray<FImpossible> Impossible;
		{
			FImpossible& Case = Impossible.AddDefaulted_GetRef();
			Case.Label = TEXT("10 rooms in a 2x2 grid");
			Case.Settings.GridSize = FIntPoint(2, 2);
		}
		{
			FImpossible& Case = Impossible.AddDefaulted_GetRef();
			Case.Label = TEXT("Linear with rooms 10-14 but main path 6-8");
			Case.Settings.Shape = ENRGridLayoutShape::Linear;
		}
		{
			FImpossible& Case = Impossible.AddDefaulted_GetRef();
			Case.Label = TEXT("MinRooms above MaxRooms");
			Case.Settings.MinRooms = 15;
		}
		{
			FImpossible& Case = Impossible.AddDefaulted_GetRef();
			Case.Label = TEXT("Loop shape with MaxLoops 0");
			Case.Settings.Shape = ENRGridLayoutShape::Loop;
			Case.Settings.MinLoops = 0;
			Case.Settings.MaxLoops = 0;
		}
		{
			FImpossible& Case = Impossible.AddDefaulted_GetRef();
			Case.Label = TEXT("Branching with no room left for a branch");
			Case.Settings.Shape = ENRGridLayoutShape::Branching;
			Case.Settings.MinRooms = 6;
			Case.Settings.MaxRooms = 6;
			Case.Settings.MinMainPathLength = 6;
			Case.Settings.MaxMainPathLength = 6;
		}
		{
			FImpossible& Case = Impossible.AddDefaulted_GetRef();
			Case.Label = TEXT("main path of 3 with safe and elite required");
			Case.Settings.MinMainPathLength = 3;
			Case.Settings.MaxMainPathLength = 3;
		}
		{
			FImpossible& Case = Impossible.AddDefaulted_GetRef();
			Case.Label = TEXT("Linear 36 rooms filling a 6x6 grid (fits on paper, hard to walk)");
			Case.Settings.Shape = ENRGridLayoutShape::Linear;
			Case.Settings.MinRooms = 36;
			Case.Settings.MaxRooms = 36;
			Case.Settings.MinMainPathLength = 36;
			Case.Settings.MaxMainPathLength = 36;
			Case.Settings.MaxAttempts = 5;
			Case.Settings.MainPathSearchBudget = 2000;
		}

		{
			FImpossible& Case = Impossible.AddDefaulted_GetRef();
			Case.Label = TEXT("MinHalls above MaxHalls");
			Case.Settings.MinHalls = 4;
			Case.Settings.MaxHalls = 3;
		}
		{
			FImpossible& Case = Impossible.AddDefaulted_GetRef();
			Case.Label = TEXT("Linear with more hall cells than fit between its rooms");
			Case.Settings.Shape = ENRGridLayoutShape::Linear;
			Case.Settings.MinMainPathLength = 10;
			Case.Settings.MaxMainPathLength = 14;
			Case.Settings.GridSize = FIntPoint(10, 10);
			Case.Settings.MinHalls = 30;
			Case.Settings.MaxHalls = 30;
		}

		for (const FImpossible& Case : Impossible)
		{
			FNRGeneratedLayout Layout;
			FString Error;
			if (FNRGridLayoutGenerator::Generate(Case.Settings, 7, Layout, Error))
			{
				// The last case may legitimately succeed (a Hamiltonian walk exists); it is here
				// to show the search gives up with a reason rather than hanging.
				if (FCString::Strstr(Case.Label, TEXT("hard to walk")) == nullptr)
				{
					Failures.Add(FString::Printf(TEXT("impossible settings '%s' produced a layout"), Case.Label));
				}
				Summary.Add(FString::Printf(TEXT("refusal '%s': generated anyway (allowed)"), Case.Label));
			}
			else if (Error.IsEmpty())
			{
				Failures.Add(FString::Printf(TEXT("impossible settings '%s' failed without a reason"), Case.Label));
			}
			else
			{
				Summary.Add(FString::Printf(TEXT("refusal '%s': %s"), Case.Label, *Error));
			}
		}

		OutSummary = FString::Join(Summary, TEXT("\n"));
		return Failures;
	}

	void Run(const TArray<FString>& Args)
	{
		const int32 SeedCount = Args.Num() > 0 ? FMath::Max(FCString::Atoi(*Args[0]), 1) : 100;
		FString Summary;
		const TArray<FString> Failures = RunSuite(SeedCount, Summary);
		TArray<FString> Lines;
		Summary.ParseIntoArrayLines(Lines, false);
		for (const FString& Line : Lines)
		{
			UE_LOG(LogNRGridTest, Log, TEXT("%s"), *Line);
		}
		for (const FString& Failure : Failures)
		{
			UE_LOG(LogNRGridTest, Error, TEXT("FAIL %s"), *Failure);
		}
		UE_LOG(LogNRGridTest, Log, TEXT("Grid layout test: %d seed(s) per shape, %d failure(s)"), SeedCount, Failures.Num());
	}
}

static FAutoConsoleCommand GNRGridTestCommand(TEXT("NR.Grid.Test"),
	TEXT("NR.Grid.Test [seeds]: generate and validate that many seeds per shape and template set (default 100), and check refusals."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&NRGridTests::Run));

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNRGridLayoutGeneratorTest, "ProjectNR.Grid.LayoutGenerator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNRGridLayoutGeneratorTest::RunTest(const FString& Parameters)
{
	FString Summary;
	const TArray<FString> Failures = NRGridTests::RunSuite(100, Summary);
	AddInfo(Summary);
	for (const FString& Failure : Failures)
	{
		AddError(Failure);
	}
	return Failures.Num() == 0;
}

#endif
