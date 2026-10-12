// Copyright Epic Games, Inc. All Rights Reserved.

// Checks for FNRGridLayoutGenerator: many fixed seeds per shape (valid, reproducible, varied)
// and settings that cannot be met (refused with a reason). Runs as the automation test
// ProjectNR.Grid.LayoutGenerator and from the console as NR.Grid.Test [seeds].

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "NRGridLayoutGenerator.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRGridTest, Log, All);

namespace NRGridTests
{
	/** The plan's first-version numbers, with the room counts a Linear map can actually have. */
	FNRMapGenSettings SettingsFor(ENRGridLayoutShape Shape)
	{
		FNRMapGenSettings Settings;
		Settings.Shape = Shape;
		if (Shape == ENRGridLayoutShape::Linear)
		{
			// Every room is on the main path, so the path has to be the full 10-14.
			Settings.MinMainPathLength = 10;
			Settings.MaxMainPathLength = 14;
		}
		return Settings;
	}

	/** Runs every check; returns the failures, each naming its seed or settings. */
	TArray<FString> RunSuite(int32 SeedCount, FString& OutSummary)
	{
		TArray<FString> Failures;
		TArray<FString> Summary;

		for (ENRGridLayoutShape Shape : { ENRGridLayoutShape::Linear, ENRGridLayoutShape::Branching, ENRGridLayoutShape::Loop, ENRGridLayoutShape::Mixed })
		{
			const FNRMapGenSettings Settings = SettingsFor(Shape);
			const FString Name = StaticEnum<ENRGridLayoutShape>()->GetNameStringByValue(static_cast<int64>(Shape));
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

				const TArray<FString> Problems = FNRGridLayoutGenerator::Validate(Settings, Layout);
				for (const FString& Problem : Problems)
				{
					Failures.Add(FString::Printf(TEXT("%s seed %d: %s"), *Name, Seed, *Problem));
				}

				FNRGeneratedLayout Again;
				FString AgainError;
				if (!FNRGridLayoutGenerator::Generate(Settings, Seed, Again, AgainError) || !FNRGridLayoutGenerator::Identical(Layout, Again))
				{
					Failures.Add(FString::Printf(TEXT("%s seed %d: not reproducible"), *Name, Seed));
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

		// Template choice: with one all-round room every room must get it; with a set of shaped
		// rooms (dead end, straight, corner, T, cross) every room must get one whose turned
		// doorways cover its own, of an allowed type and turn, the same way every time.
		{
			const uint8 AllTypes = 0b11111;
			auto Make = [](uint8 Ports, uint8 Types, float Weight)
			{
				FNRRoomTemplateDesc Desc;
				Desc.PortMask = Ports;
				Desc.TypeMask = Types;
				Desc.Weight = Weight;
				return Desc;
			};
			const uint8 N = 1 << 0, E = 1 << 1, S = 1 << 2, W = 1 << 3;
			const TArray<FNRRoomTemplateDesc> Single = { Make(N | E | S | W, AllTypes, 1.0f) };
			const TArray<FNRRoomTemplateDesc> Shaped = {
				Make(N, AllTypes, 1.0f),             // dead end
				Make(N | S, AllTypes, 1.0f),         // straight
				Make(N | E, AllTypes, 1.0f),         // corner
				Make(N | E | S, AllTypes, 1.0f),     // T
				Make(N | E | S | W, AllTypes, 0.2f), // cross, rarely
			};

			for (const TArray<FNRRoomTemplateDesc>* Set : { &Single, &Shaped })
			{
				const TCHAR* SetName = Set == &Single ? TEXT("single template") : TEXT("shaped templates");
				TArray<int32> Uses;
				Uses.Init(0, Set->Num());
				int32 Rooms = 0;
				int32 Exact = 0;
				for (ENRGridLayoutShape Shape : { ENRGridLayoutShape::Linear, ENRGridLayoutShape::Branching, ENRGridLayoutShape::Loop, ENRGridLayoutShape::Mixed })
				{
					const FNRMapGenSettings Settings = SettingsFor(Shape);
					for (int32 Seed = 1; Seed <= SeedCount; ++Seed)
					{
						FNRGeneratedLayout Layout;
						FString Error;
						if (!FNRGridLayoutGenerator::Generate(Settings, Seed, Layout, Error))
						{
							continue;
						}
						FNRGeneratedLayout Again = Layout;
						if (!FNRGridLayoutGenerator::AssignTemplates(*Set, Layout, Error))
						{
							Failures.Add(FString::Printf(TEXT("%s, seed %d: %s"), SetName, Seed, *Error));
							continue;
						}
						for (const FString& Problem : FNRGridLayoutGenerator::ValidateTemplates(*Set, Layout))
						{
							Failures.Add(FString::Printf(TEXT("%s, seed %d: %s"), SetName, Seed, *Problem));
						}
						FNRGridLayoutGenerator::AssignTemplates(*Set, Again, Error);
						if (!FNRGridLayoutGenerator::Identical(Layout, Again)
							|| Layout.Rooms.ContainsByPredicate([&Again](const FNRGeneratedRoom& Room)
							{
								const FNRGeneratedRoom* Other = Again.FindRoom(Room.RoomId);
								return !Other || Other->TemplateIndex != Room.TemplateIndex || Other->RotationSteps != Room.RotationSteps;
							}))
						{
							Failures.Add(FString::Printf(TEXT("%s, seed %d: template choice not reproducible"), SetName, Seed));
						}
						for (const FNRGeneratedRoom& Room : Layout.Rooms)
						{
							++Uses[Room.TemplateIndex];
							++Rooms;
							Exact += NRGrid::RotateMask((*Set)[Room.TemplateIndex].PortMask, Room.RotationSteps) == Room.ConnectionMask ? 1 : 0;
						}
					}
				}
				FString UseText;
				for (int32 Index = 0; Index < Uses.Num(); ++Index)
				{
					UseText += FString::Printf(TEXT(" t%d=%d"), Index, Uses[Index]);
				}
				Summary.Add(FString::Printf(TEXT("%s: %d rooms placed, %d with no spare doorway; uses%s"), SetName, Rooms, Exact, *UseText));
			}

			// A room type no template allows must be refused, naming the room, not filled.
			FNRGeneratedLayout Layout;
			FString Error;
			FNRGridLayoutGenerator::Generate(SettingsFor(ENRGridLayoutShape::Mixed), 5, Layout, Error);
			const TArray<FNRRoomTemplateDesc> NoBoss = { Make(N | E | S | W, AllTypes & ~(1 << static_cast<int32>(ENRGridRoomType::Boss)), 1.0f) };
			if (FNRGridLayoutGenerator::AssignTemplates(NoBoss, Layout, Error))
			{
				Failures.Add(TEXT("templates without a boss room were accepted"));
			}
			else
			{
				Summary.Add(FString::Printf(TEXT("refusal 'no boss template': %s"), *Error));
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
		Summary.ParseIntoArrayLines(Lines);
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
	TEXT("NR.Grid.Test [seeds]: generate and validate that many seeds per shape (default 100) and check impossible settings."),
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
