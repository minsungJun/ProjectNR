// Copyright Epic Games, Inc. All Rights Reserved.

// Checks for FNRGridLayoutGenerator: many fixed seeds per shape (valid, reproducible, varied),
// with one-cell rooms and with rooms of several cells, with hall cells (straight, corner, T and
// crossing corridors between rooms), with Indoor, Outdoor and Transition environments and their
// corridors, settings and templates that cannot be met (refused with a reason), and broken
// layouts Validate must catch. Runs as the automation
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

	FString EnvName(ENRRoomEnvironment Environment)
	{
		return FNRGridLayoutGenerator::EnvironmentName(Environment);
	}

	FString ModeName(ENRMapEnvironmentMode Mode)
	{
		return StaticEnum<ENRMapEnvironmentMode>()->GetNameStringByValue(static_cast<int64>(Mode));
	}

	/** The plan's minimum asset set: the multi-cell rooms and the four halls both Indoor and Outdoor,
	 *  a one-cell Transition combat room and four Transition halls. */
	TArray<FNRRoomTemplateDesc> EnvironmentTemplates()
	{
		const uint8 N = 1 << 0, E = 1 << 1, S = 1 << 2, W = 1 << 3;
		const uint8 Hall = Bit(ENRGridRoomType::Hall);
		const TArray<FNRRoomTemplateDesc> Rooms = TemplateSets()[2].Templates;
		TArray<FNRRoomTemplateDesc> Templates;
		for (ENRRoomEnvironment Environment : { ENRRoomEnvironment::Indoor, ENRRoomEnvironment::Outdoor })
		{
			for (FNRRoomTemplateDesc Template : Rooms)
			{
				Template.Environment = Environment;
				Templates.Add(Template);
			}
		}
		Templates.Add(FNRRoomTemplateDesc::Make(FIntPoint(1, 1), N | E | S | W, Bit(ENRGridRoomType::Combat), 1.0f, ENRRoomEnvironment::Transition));
		for (ENRRoomEnvironment Environment : { ENRRoomEnvironment::Indoor, ENRRoomEnvironment::Outdoor, ENRRoomEnvironment::Transition })
		{
			for (const uint8 Mask : { uint8(N | S), uint8(N | E), uint8(N | E | S), uint8(N | E | S | W) })
			{
				Templates.Add(FNRRoomTemplateDesc::Make(FIntPoint(1, 1), Mask, Hall, 1.0f, Environment));
			}
		}
		return Templates;
	}

	/** One corridor per pair that may meet, and a second, rarer Indoor one. */
	TArray<FNRCorridorTemplateDesc> EnvironmentCorridors()
	{
		using E = ENRRoomEnvironment;
		return {
			FNRCorridorTemplateDesc::Make(E::Indoor, E::Indoor),
			FNRCorridorTemplateDesc::Make(E::Indoor, E::Transition),
			FNRCorridorTemplateDesc::Make(E::Transition, E::Transition),
			FNRCorridorTemplateDesc::Make(E::Transition, E::Outdoor),
			FNRCorridorTemplateDesc::Make(E::Outdoor, E::Outdoor),
			FNRCorridorTemplateDesc::Make(E::Indoor, E::Indoor, 0.3f),
		};
	}

	/**
	 * Environments: every mode, shape and seed gives a valid layout whose rooms and hall cells keep
	 * to the mode, never join Indoor straight to Outdoor (main path, branches and loops alike),
	 * stand in templates of their own environment and are joined by corridors made for their two
	 * environments - the same way every time. Settings or templates that cannot make such a map are
	 * refused up front, and Validate catches layouts that break the rules.
	 */
	void EnvironmentSuite(int32 SeedCount, TArray<FString>& Failures, TArray<FString>& Summary)
	{
		const TArray<FNRRoomTemplateDesc> Templates = EnvironmentTemplates();
		const TArray<FNRCorridorTemplateDesc> Corridors = EnvironmentCorridors();
		auto MakeSettings = [](ENRGridLayoutShape Shape, ENRMapEnvironmentMode Mode)
		{
			FNRMapGenSettings Settings = SettingsFor(Shape, FIntPoint(8, 8));
			Settings.EnvironmentMode = Mode;
			Settings.MinHalls = 3;
			Settings.MaxHalls = 6;
			return Settings;
		};

		struct FVariant
		{
			const TCHAR* Label;
			ENRMapEnvironmentMode Mode;
			TFunction<void(FNRMapGenSettings&)> Adjust;
		};
		const TArray<FVariant> Variants = {
			{ TEXT("IndoorOnly"), ENRMapEnvironmentMode::IndoorOnly, nullptr },
			{ TEXT("OutdoorOnly"), ENRMapEnvironmentMode::OutdoorOnly, nullptr },
			{ TEXT("Hybrid"), ENRMapEnvironmentMode::Hybrid, nullptr },
			{ TEXT("Hybrid from Outdoor, 1-2 transition rooms"), ENRMapEnvironmentMode::Hybrid, [](FNRMapGenSettings& Settings)
				{
					Settings.HybridStartEnvironment = ENRRoomEnvironment::Outdoor;
					Settings.MaxTransitionRooms = 2;
				} },
			{ TEXT("Hybrid, random start"), ENRMapEnvironmentMode::Hybrid, [](FNRMapGenSettings& Settings) { Settings.bRandomizeHybridStart = true; } },
			{ TEXT("Hybrid DistanceFlood"), ENRMapEnvironmentMode::Hybrid, [](FNRMapGenSettings& Settings)
				{
					Settings.HybridAssignment = ENRHybridAssignment::DistanceFlood;
				} },
			{ TEXT("Hybrid DistanceFlood, by cells"), ENRMapEnvironmentMode::Hybrid, [](FNRMapGenSettings& Settings)
				{
					Settings.HybridAssignment = ENRHybridAssignment::DistanceFlood;
					Settings.HybridBalance = ENRHybridBalance::Cells;
				} },
			{ TEXT("Hybrid DistanceFlood, split 0.3"), ENRMapEnvironmentMode::Hybrid, [](FNRMapGenSettings& Settings)
				{
					Settings.HybridAssignment = ENRHybridAssignment::DistanceFlood;
					Settings.HybridSplit = 0.3f;
				} },
			{ TEXT("Hybrid DistanceFlood, split 0.7, random start"), ENRMapEnvironmentMode::Hybrid, [](FNRMapGenSettings& Settings)
				{
					Settings.HybridAssignment = ENRHybridAssignment::DistanceFlood;
					Settings.HybridSplit = 0.7f;
					Settings.bRandomizeHybridStart = true;
				} },
		};
		// DistanceFlood: the share of the map on the start's side, by split.
		TMap<float, float> StartShareBySplit;

		for (const FVariant& Variant : Variants)
		{
			int32 Generated = 0;
			int32 Tried = 0;
			int32 Retried = 0;
			int32 Rooms[3] = { 0, 0, 0 };
			int32 Starts[3] = { 0, 0, 0 };
			int32 Pairs[3][3] = {};
			int32 TransitionRuns[4] = { 0, 0, 0, 0 };
			int32 CorridorUses[6] = { 0, 0, 0, 0, 0, 0 };
			int32 TransitionCells[5] = { 0, 0, 0, 0, 0 };
			double StartShare = 0.0;
			double MinShare = 1.0;
			double MaxShare = 0.0;
			int32 WithinFive = 0;
			int32 Split3 = 0;
			bool bFlood = false;
			float Split = 0.5f;
			for (ENRGridLayoutShape Shape : Shapes)
			{
				FNRMapGenSettings Settings = MakeSettings(Shape, Variant.Mode);
				if (Variant.Adjust)
				{
					Variant.Adjust(Settings);
				}
				bFlood = Settings.EnvironmentMode == ENRMapEnvironmentMode::Hybrid && Settings.HybridAssignment == ENRHybridAssignment::DistanceFlood;
				Split = Settings.HybridSplit;
				for (int32 Seed = 1; Seed <= SeedCount; ++Seed)
				{
					++Tried;
					const FString Where = FString::Printf(TEXT("%s, %s seed %d"), Variant.Label, *ShapeName(Shape), Seed);
					FNRGeneratedLayout Layout;
					FString Error;
					if (!FNRGridLayoutGenerator::Generate(Settings, Templates, Corridors, Seed, Layout, Error))
					{
						Failures.Add(FString::Printf(TEXT("%s: %s"), *Where, *Error));
						continue;
					}
					++Generated;
					Retried += Layout.Attempt > 0 ? 1 : 0;
					for (const FString& Problem : FNRGridLayoutGenerator::Validate(Settings, Templates, Corridors, Layout))
					{
						Failures.Add(FString::Printf(TEXT("%s: %s"), *Where, *Problem));
					}

					// 9. The same seed, settings and templates give the same environments and corridors.
					FNRGeneratedLayout Again;
					if (!FNRGridLayoutGenerator::Generate(Settings, Templates, Corridors, Seed, Again, Error) || !FNRGridLayoutGenerator::Identical(Layout, Again))
					{
						Failures.Add(FString::Printf(TEXT("%s: not reproducible"), *Where));
					}

					// 1-3. Environments the mode allows: one only, or all three.
					bool bHas[3] = { false, false, false };
					for (const FNRGeneratedRoom& Room : Layout.Rooms)
					{
						const int32 Environment = static_cast<int32>(Room.Environment);
						bHas[Environment] = true;
						++Rooms[Environment];
						// 7. The template a room stands in is of its environment.
						if (!Templates.IsValidIndex(Room.TemplateIndex) || Templates[Room.TemplateIndex].Environment != Room.Environment)
						{
							Failures.Add(FString::Printf(TEXT("%s: room %d is %s but stands in template %d"), *Where, Room.RoomId, *EnvName(Room.Environment), Room.TemplateIndex));
						}
					}
					const bool bModeOk = Variant.Mode == ENRMapEnvironmentMode::IndoorOnly ? (bHas[0] && !bHas[1] && !bHas[2])
						: Variant.Mode == ENRMapEnvironmentMode::OutdoorOnly ? (!bHas[0] && bHas[1] && !bHas[2])
						: (bHas[0] && bHas[1] && bHas[2]);
					if (!bModeOk)
					{
						Failures.Add(FString::Printf(TEXT("%s: environments I%d O%d T%d do not suit %s"), *Where, bHas[0], bHas[1], bHas[2], *ModeName(Variant.Mode)));
					}

					// 4, 6, 8. Every corridor - main path, branch or loop - joins environments that may
					// meet, with a corridor template made for them.
					for (const FNRGeneratedConnection& Connection : Layout.Connections)
					{
						const ENRRoomEnvironment A = Layout.FindRoom(Connection.RoomA)->Environment;
						const ENRRoomEnvironment B = Layout.FindRoom(Connection.RoomB)->Environment;
						++Pairs[FMath::Min(static_cast<int32>(A), static_cast<int32>(B))][FMath::Max(static_cast<int32>(A), static_cast<int32>(B))];
						if (!NRGrid::EnvironmentsMeet(A, B))
						{
							Failures.Add(FString::Printf(TEXT("%s: corridor %d-%d joins %s straight to %s"), *Where, Connection.RoomA, Connection.RoomB, *EnvName(A), *EnvName(B)));
						}
						if (!Corridors.IsValidIndex(Connection.TemplateIndex) || !Corridors[Connection.TemplateIndex].Joins(A, B))
						{
							Failures.Add(FString::Printf(TEXT("%s: corridor %d-%d (%s-%s) got corridor template %d"), *Where, Connection.RoomA, Connection.RoomB,
								*EnvName(A), *EnvName(B), Connection.TemplateIndex));
						}
						else
						{
							++CorridorUses[Connection.TemplateIndex];
						}
					}

					// 5. Hybrid main path: from the start's side to the other side only through Transition.
					const FNRGeneratedRoom* Start = Layout.FindRoom(Layout.StartRoomId);
					const FNRGeneratedRoom* Boss = Layout.FindRoom(Layout.BossRoomId);
					++Starts[static_cast<int32>(Start->Environment)];
					if (Variant.Mode == ENRMapEnvironmentMode::Hybrid)
					{
						TArray<const FNRGeneratedRoom*> Path;
						for (const FNRGeneratedRoom& Room : Layout.Rooms)
						{
							if (Room.MainPathIndex != INDEX_NONE)
							{
								Path.SetNum(FMath::Max(Path.Num(), Room.MainPathIndex + 1));
								Path[Room.MainPathIndex] = &Room;
							}
						}
						int32 Transitions = 0;
						bool bThrough = Start->Environment != ENRRoomEnvironment::Transition && Boss->Environment != ENRRoomEnvironment::Transition
							&& Start->Environment != Boss->Environment;
						for (int32 Step = 0; Step < Path.Num(); ++Step)
						{
							bThrough &= Step == 0 || NRGrid::EnvironmentsMeet(Path[Step - 1]->Environment, Path[Step]->Environment);
							Transitions += Path[Step]->Environment == ENRRoomEnvironment::Transition && Path[Step]->RoomType != ENRGridRoomType::Hall ? 1 : 0;
						}
						++TransitionRuns[FMath::Clamp(Transitions, 0, 3)];
						// PathSplit counts Transition rooms in a row on the main path; DistanceFlood every
						// Transition room and hall cell of the map.
						const int32 Cells = Layout.Rooms.FilterByPredicate([](const FNRGeneratedRoom& Room) { return Room.Environment == ENRRoomEnvironment::Transition; }).Num();
						++TransitionCells[FMath::Clamp(Cells, 0, 4)];
						const bool bCountOk = bFlood
							? (Cells >= Settings.MinTransitionCells && Cells <= Settings.MaxTransitionCells)
							: (Transitions >= Settings.MinTransitionRooms && Transitions <= Settings.MaxTransitionRooms);
						if (!bThrough || !bCountOk)
						{
							Failures.Add(FString::Printf(TEXT("%s: main path from %s to %s with %d transition room(s), %d in the map, does not cross through Transition as asked"),
								*Where, *EnvName(Start->Environment), *EnvName(Boss->Environment), Transitions, Cells));
						}
						// The start's side as a share of the rooms (or cells) outside Transition rooms.
						int32 Mine = 0;
						int32 Sides = 0;
						for (const FNRGeneratedRoom& Room : Layout.Rooms)
						{
							if (Room.Environment != ENRRoomEnvironment::Transition)
							{
								const int32 Weight = Settings.HybridBalance == ENRHybridBalance::Cells ? Room.Footprint.X * Room.Footprint.Y : 1;
								Sides += Weight;
								Mine += Room.Environment == Start->Environment ? Weight : 0;
							}
						}
						const double Share = static_cast<double>(Mine) / FMath::Max(Sides, 1);

						// Each side in one piece: its rooms reach each other without leaving it.
						int32 Pieces = 0;
						TSet<int32> Seen;
						for (const FNRGeneratedRoom& Room : Layout.Rooms)
						{
							if (Room.Environment == ENRRoomEnvironment::Transition || Seen.Contains(Room.RoomId))
							{
								continue;
							}
							++Pieces;
							TArray<int32> Queue = { Room.RoomId };
							Seen.Add(Room.RoomId);
							for (int32 Head = 0; Head < Queue.Num(); ++Head)
							{
								for (const FNRGeneratedConnection& Connection : Layout.Connections)
								{
									const int32 Other = Connection.RoomA == Queue[Head] ? Connection.RoomB : Connection.RoomB == Queue[Head] ? Connection.RoomA : INDEX_NONE;
									if (Other != INDEX_NONE && !Seen.Contains(Other) && Layout.FindRoom(Other)->Environment == Room.Environment)
									{
										Seen.Add(Other);
										Queue.Add(Other);
									}
								}
							}
						}
						Split3 += Pieces > 2 ? 1 : 0;
						if (bFlood && Pieces > 2)
						{
							Failures.Add(FString::Printf(TEXT("%s: the Indoor and Outdoor sides fall into %d pieces, not 2"), *Where, Pieces));
						}
						StartShare += Share;
						MinShare = FMath::Min(MinShare, Share);
						MaxShare = FMath::Max(MaxShare, Share);
						WithinFive += FMath::Abs(Share - Settings.HybridSplit) <= 0.05 + UE_KINDA_SMALL_NUMBER ? 1 : 0;
						if (bFlood && FMath::Abs(Share - Settings.HybridSplit) > Settings.MaxHybridImbalance + UE_KINDA_SMALL_NUMBER)
						{
							Failures.Add(FString::Printf(TEXT("%s: the start's side holds %.0f%% of the map, wanted %.0f%% +-%.0f"),
								*Where, 100.0 * Share, 100.0 * Settings.HybridSplit, 100.0 * Settings.MaxHybridImbalance));
						}
					}
				}
			}
			Summary.Add(FString::Printf(TEXT("environments %s: %d/%d generated, %d needed a retry; rooms and halls I %d, O %d, T %d; starts I %d, O %d; corridors I-I %d, I-T %d, T-T %d, T-O %d, O-O %d, I-O %d; transition rooms on the main path 1:%d 2:%d; transition cells in the map 1:%d 2:%d 3:%d 4+:%d; start side %.0f%% of the rest (by HybridBalance) (%.0f-%.0f%%, %d within 5 points of the split); %d with a side in pieces; corridor templates %d/%d/%d/%d/%d/%d"),
				Variant.Label, Generated, Tried, Retried, Rooms[0], Rooms[1], Rooms[2], Starts[0], Starts[1],
				Pairs[0][0], Pairs[0][2], Pairs[2][2], Pairs[1][2], Pairs[1][1], Pairs[0][1], TransitionRuns[1], TransitionRuns[2],
				TransitionCells[1], TransitionCells[2], TransitionCells[3], TransitionCells[4],
				Generated > 0 ? 100.0 * StartShare / Generated : 0.0, 100.0 * MinShare, 100.0 * MaxShare, WithinFive, Split3,
				CorridorUses[0], CorridorUses[1], CorridorUses[2], CorridorUses[3], CorridorUses[4], CorridorUses[5]));
			if (bFlood && Generated > 0)
			{
				StartShareBySplit.Add(Split, static_cast<float>(StartShare / Generated));
			}
			if (Variant.Mode == ENRMapEnvironmentMode::Hybrid && !bFlood && Generated > 0 && Pairs[2][2] == 0)
			{
				Failures.Add(FString::Printf(TEXT("environments %s: no two Transition rooms or halls were ever joined"), Variant.Label));
			}
			if (Generated > 0 && CorridorUses[0] > 0 && CorridorUses[5] == 0)
			{
				Failures.Add(FString::Printf(TEXT("environments %s: the second Indoor corridor was never chosen"), Variant.Label));
			}
		}

		// HybridSplit moves the line: the lower it is, the less of the map is on the start's side.
		if (StartShareBySplit.Num() == 3 && !(StartShareBySplit.FindRef(0.3f) < StartShareBySplit.FindRef(0.5f)
			&& StartShareBySplit.FindRef(0.5f) < StartShareBySplit.FindRef(0.7f)))
		{
			Failures.Add(FString::Printf(TEXT("environments: HybridSplit 0.3/0.5/0.7 gave the start's side %.0f%%/%.0f%%/%.0f%%, not rising"),
				100.0f * StartShareBySplit.FindRef(0.3f), 100.0f * StartShareBySplit.FindRef(0.5f), 100.0f * StartShareBySplit.FindRef(0.7f)));
		}

		// Hybrid examples of seed 1 both ways, types left and environments right.
		for (const ENRHybridAssignment Assignment : { ENRHybridAssignment::PathSplit, ENRHybridAssignment::DistanceFlood })
		{
			FNRMapGenSettings Settings = MakeSettings(ENRGridLayoutShape::Mixed, ENRMapEnvironmentMode::Hybrid);
			Settings.HybridAssignment = Assignment;
			FNRGeneratedLayout Layout;
			FString Error;
			if (FNRGridLayoutGenerator::Generate(Settings, Templates, Corridors, 1, Layout, Error))
			{
				TArray<FString> Types;
				TArray<FString> Environments;
				FNRGridLayoutGenerator::ToAscii(Settings, Layout).ParseIntoArrayLines(Types, false);
				FNRGridLayoutGenerator::ToAscii(Settings, Layout, true).ParseIntoArrayLines(Environments, false);
				Summary.Add(FString::Printf(TEXT("environments, Hybrid %s Mixed seed 1:"),
					*StaticEnum<ENRHybridAssignment>()->GetNameStringByValue(static_cast<int64>(Assignment))));
				for (int32 Line = 0; Line < Types.Num(); ++Line)
				{
					Summary.Add(FString::Printf(TEXT("    %s    %s"), *Types[Line], *Environments[Line]));
				}
			}
		}

		// The plain sets (no environments given) stay Indoor and keep the fallback corridor.
		{
			FNRGeneratedLayout Layout;
			FString Error;
			if (!FNRGridLayoutGenerator::Generate(SettingsFor(ENRGridLayoutShape::Mixed, FIntPoint(8, 8)), TemplateSets()[2].Templates, 3, Layout, Error))
			{
				Failures.Add(FString::Printf(TEXT("environments, default settings: %s"), *Error));
			}
			else if (Layout.Rooms.ContainsByPredicate([](const FNRGeneratedRoom& Room) { return Room.Environment != ENRRoomEnvironment::Indoor; })
				|| Layout.Connections.ContainsByPredicate([](const FNRGeneratedConnection& Connection) { return Connection.TemplateIndex != INDEX_NONE; }))
			{
				Failures.Add(TEXT("environments, default settings: not every room Indoor on the fallback corridor"));
			}
		}

		// 10. What cannot make a map is refused before anything is placed, with the reason.
		{
			using E = ENRRoomEnvironment;
			struct FRefusal
			{
				const TCHAR* Label;
				const TCHAR* Expect;
				TFunction<void(FNRMapGenSettings&, TArray<FNRRoomTemplateDesc>&, TArray<FNRCorridorTemplateDesc>&)> Apply;
			};
			auto Without = [](TArray<FNRRoomTemplateDesc>& Set, TFunction<bool(const FNRRoomTemplateDesc&)> Drop)
			{
				Set.RemoveAll([&Drop](const FNRRoomTemplateDesc& Template) { return Drop(Template); });
			};
			const TArray<FRefusal> Refusals = {
				{ TEXT("Hybrid without Transition rooms"), TEXT("Transition templates stand for no room type"),
					[&Without](FNRMapGenSettings& Settings, TArray<FNRRoomTemplateDesc>& Set, TArray<FNRCorridorTemplateDesc>&)
					{
						Settings.MinHalls = Settings.MaxHalls = 0;
						Without(Set, [](const FNRRoomTemplateDesc& T) { return T.Environment == E::Transition; });
					} },
				{ TEXT("Hybrid without Transition halls"), TEXT("no Transition room template for Hall"),
					[&Without](FNRMapGenSettings&, TArray<FNRRoomTemplateDesc>& Set, TArray<FNRCorridorTemplateDesc>&)
					{
						Without(Set, [](const FNRRoomTemplateDesc& T) { return T.Environment == E::Transition && (T.TypeMask & Bit(ENRGridRoomType::Hall)); });
					} },
				{ TEXT("Hybrid without an Outdoor boss"), TEXT("no Outdoor room template for Boss"),
					[&Without](FNRMapGenSettings&, TArray<FNRRoomTemplateDesc>& Set, TArray<FNRCorridorTemplateDesc>&)
					{
						Without(Set, [](const FNRRoomTemplateDesc& T) { return T.Environment == E::Outdoor && (T.TypeMask & Bit(ENRGridRoomType::Boss)); });
					} },
				{ TEXT("random Hybrid start without an Outdoor start room"), TEXT("no Outdoor room template for Start"),
					[&Without](FNRMapGenSettings& Settings, TArray<FNRRoomTemplateDesc>& Set, TArray<FNRCorridorTemplateDesc>&)
					{
						Settings.bRandomizeHybridStart = true;
						Without(Set, [](const FNRRoomTemplateDesc& T) { return T.Environment == E::Outdoor && (T.TypeMask & Bit(ENRGridRoomType::Start)); });
					} },
				{ TEXT("OutdoorOnly with Indoor templates only"), TEXT("no Outdoor room template"),
					[&Without](FNRMapGenSettings& Settings, TArray<FNRRoomTemplateDesc>& Set, TArray<FNRCorridorTemplateDesc>&)
					{
						Settings.EnvironmentMode = ENRMapEnvironmentMode::OutdoorOnly;
						Without(Set, [](const FNRRoomTemplateDesc& T) { return T.Environment != E::Indoor; });
					} },
				{ TEXT("Hybrid without a Transition-Outdoor corridor"), TEXT("no corridor template joins Outdoor and Transition"),
					[](FNRMapGenSettings&, TArray<FNRRoomTemplateDesc>&, TArray<FNRCorridorTemplateDesc>& Set)
					{
						Set.RemoveAll([](const FNRCorridorTemplateDesc& C) { return C.Joins(E::Transition, E::Outdoor); });
					} },
				{ TEXT("an Indoor-Outdoor corridor"), TEXT("joins Indoor and Outdoor"),
					[](FNRMapGenSettings&, TArray<FNRRoomTemplateDesc>&, TArray<FNRCorridorTemplateDesc>& Set)
					{
						Set.Add(FNRCorridorTemplateDesc::Make(E::Indoor, E::Outdoor));
					} },
				{ TEXT("Hybrid starting in Transition"), TEXT("not Transition"),
					[](FNRMapGenSettings& Settings, TArray<FNRRoomTemplateDesc>&, TArray<FNRCorridorTemplateDesc>&)
					{
						Settings.HybridStartEnvironment = E::Transition;
					} },
				{ TEXT("Hybrid main path too short for its transition rooms"), TEXT("cannot hold 2 transition room(s)"),
					[](FNRMapGenSettings& Settings, TArray<FNRRoomTemplateDesc>&, TArray<FNRCorridorTemplateDesc>&)
					{
						Settings.bRequireSafe = false;
						Settings.bRequireElite = false;
						Settings.MinMainPathLength = Settings.MaxMainPathLength = 3;
						Settings.MinTransitionRooms = Settings.MaxTransitionRooms = 2;
					} },
				{ TEXT("DistanceFlood without any Transition template"), TEXT("no Transition room or hall template"),
					[&Without](FNRMapGenSettings& Settings, TArray<FNRRoomTemplateDesc>& Set, TArray<FNRCorridorTemplateDesc>&)
					{
						Settings.HybridAssignment = ENRHybridAssignment::DistanceFlood;
						Without(Set, [](const FNRRoomTemplateDesc& T) { return T.Environment == E::Transition; });
					} },
				{ TEXT("DistanceFlood with an Indoor elite only"), TEXT("no Elite room size both an Indoor and an Outdoor"),
					[&Without](FNRMapGenSettings& Settings, TArray<FNRRoomTemplateDesc>& Set, TArray<FNRCorridorTemplateDesc>&)
					{
						Settings.HybridAssignment = ENRHybridAssignment::DistanceFlood;
						Without(Set, [](const FNRRoomTemplateDesc& T) { return T.Environment == E::Outdoor && (T.TypeMask & Bit(ENRGridRoomType::Elite)); });
					} },
				{ TEXT("DistanceFlood with transition cells 3-1"), TEXT("transition cells 3-1"),
					[](FNRMapGenSettings& Settings, TArray<FNRRoomTemplateDesc>&, TArray<FNRCorridorTemplateDesc>&)
					{
						Settings.HybridAssignment = ENRHybridAssignment::DistanceFlood;
						Settings.MinTransitionCells = 3;
						Settings.MaxTransitionCells = 1;
					} },
				{ TEXT("DistanceFlood with no imbalance allowed"), TEXT("MaxHybridImbalance 0.00"),
					[](FNRMapGenSettings& Settings, TArray<FNRRoomTemplateDesc>&, TArray<FNRCorridorTemplateDesc>&)
					{
						Settings.HybridAssignment = ENRHybridAssignment::DistanceFlood;
						Settings.MaxHybridImbalance = 0.0f;
					} },
				{ TEXT("DistanceFlood with HybridSplit 1"), TEXT("HybridSplit 1.00"),
					[](FNRMapGenSettings& Settings, TArray<FNRRoomTemplateDesc>&, TArray<FNRCorridorTemplateDesc>&)
					{
						Settings.HybridAssignment = ENRHybridAssignment::DistanceFlood;
						Settings.HybridSplit = 1.0f;
					} },
				{ TEXT("Transition only for safe rooms, two in a row wanted"), TEXT("Transition templates stand for Safe"),
					[](FNRMapGenSettings& Settings, TArray<FNRRoomTemplateDesc>& Set, TArray<FNRCorridorTemplateDesc>&)
					{
						Settings.MinTransitionRooms = Settings.MaxTransitionRooms = 2;
						for (FNRRoomTemplateDesc& T : Set)
						{
							if (T.Environment == E::Transition && (T.TypeMask & Bit(ENRGridRoomType::Combat)))
							{
								T.TypeMask = Bit(ENRGridRoomType::Safe);
							}
						}
					} },
			};
			for (const FRefusal& Refusal : Refusals)
			{
				FNRMapGenSettings Settings = MakeSettings(ENRGridLayoutShape::Mixed, ENRMapEnvironmentMode::Hybrid);
				TArray<FNRRoomTemplateDesc> Set = Templates;
				TArray<FNRCorridorTemplateDesc> CorridorSet = Corridors;
				Refusal.Apply(Settings, Set, CorridorSet);
				FString Error;
				FNRGeneratedLayout Layout;
				if (FNRGridLayoutGenerator::Generate(Settings, Set, CorridorSet, 5, Layout, Error))
				{
					Failures.Add(FString::Printf(TEXT("environments: '%s' was accepted"), Refusal.Label));
				}
				else if (!Error.Contains(Refusal.Expect))
				{
					Failures.Add(FString::Printf(TEXT("environments: '%s' refused for another reason: %s"), Refusal.Label, *Error));
				}
				else
				{
					Summary.Add(FString::Printf(TEXT("refusal '%s': %s"), Refusal.Label, *Error));
				}
			}
		}

		// Broken layouts Validate must catch.
		{
			const FNRMapGenSettings Settings = MakeSettings(ENRGridLayoutShape::Mixed, ENRMapEnvironmentMode::Hybrid);
			FNRGeneratedLayout Good;
			FString Error;
			if (!FNRGridLayoutGenerator::Generate(Settings, Templates, Corridors, 3, Good, Error))
			{
				Failures.Add(FString::Printf(TEXT("environment breaks: seed 3 did not generate: %s"), *Error));
				return;
			}
			// A corridor between an Indoor room and a Transition one, and its index.
			const int32 Mixed = Good.Connections.IndexOfByPredicate([&Good](const FNRGeneratedConnection& Connection)
			{
				const ENRRoomEnvironment A = Good.FindRoom(Connection.RoomA)->Environment;
				const ENRRoomEnvironment B = Good.FindRoom(Connection.RoomB)->Environment;
				return A != B && (A == ENRRoomEnvironment::Indoor || B == ENRRoomEnvironment::Indoor);
			});
			if (Mixed == INDEX_NONE)
			{
				Failures.Add(TEXT("environment breaks: seed 3 has no Indoor-Transition corridor"));
				return;
			}
			struct FBreak
			{
				const TCHAR* Label;
				const TCHAR* Expect;
				TFunction<void(FNRGeneratedLayout&)> Apply;
				ENRMapEnvironmentMode Mode;
			};
			const TArray<FBreak> Breaks = {
				{ TEXT("Transition room turned Outdoor next to an Indoor one"), TEXT("straight to"), [Mixed](FNRGeneratedLayout& Layout)
					{
						const FNRGeneratedConnection& Connection = Layout.Connections[Mixed];
						FNRGeneratedRoom* A = Layout.Rooms.FindByPredicate([&Connection](const FNRGeneratedRoom& Room) { return Room.RoomId == Connection.RoomA; });
						FNRGeneratedRoom* B = Layout.Rooms.FindByPredicate([&Connection](const FNRGeneratedRoom& Room) { return Room.RoomId == Connection.RoomB; });
						(A->Environment == ENRRoomEnvironment::Transition ? A : B)->Environment = ENRRoomEnvironment::Outdoor;
					}, ENRMapEnvironmentMode::Hybrid },
				{ TEXT("room environment not its template's"), TEXT("but its template"), [](FNRGeneratedLayout& Layout)
					{
						Layout.Rooms[0].Environment = ENRRoomEnvironment::Transition;
					}, ENRMapEnvironmentMode::Hybrid },
				{ TEXT("corridor template for other environments"), TEXT("uses corridor template"), [Mixed](FNRGeneratedLayout& Layout)
					{
						Layout.Connections[Mixed].TemplateIndex = 4;
					}, ENRMapEnvironmentMode::Hybrid },
				{ TEXT("Hybrid layout checked as IndoorOnly"), TEXT("which a IndoorOnly map does not have"), [](FNRGeneratedLayout&) {},
					ENRMapEnvironmentMode::IndoorOnly },
			};
			for (const FBreak& Break : Breaks)
			{
				FNRGeneratedLayout Broken = Good;
				Break.Apply(Broken);
				FNRMapGenSettings Checked = Settings;
				Checked.EnvironmentMode = Break.Mode;
				const TArray<FString> Problems = FNRGridLayoutGenerator::Validate(Checked, Templates, Corridors, Broken);
				const FString* Found = Problems.FindByPredicate([&Break](const FString& Problem) { return Problem.Contains(Break.Expect); });
				if (!Found)
				{
					Failures.Add(FString::Printf(TEXT("environment break '%s' not caught (%s)"), Break.Label,
						Problems.Num() > 0 ? *FString::Join(Problems, TEXT("; ")) : TEXT("no problem reported")));
				}
				else
				{
					Summary.Add(FString::Printf(TEXT("caught '%s': %s"), Break.Label, **Found));
				}
			}
		}
	}

	/** Runs every check; returns the failures, each naming its seed or settings. */
	TArray<FString> RunSuite(int32 SeedCount, FString& OutSummary)
	{
		TArray<FString> Failures;
		TArray<FString> Summary;

		EnvironmentSuite(SeedCount, Failures, Summary);

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
