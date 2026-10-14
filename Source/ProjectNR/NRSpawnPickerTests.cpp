// Copyright Epic Games, Inc. All Rights Reserved.

// Checks for FNRSpawnPicker: the share of points used, its bounds, required and boss points,
// weights, spacing, the ranged cap, unusable points and reproducibility. Automation test
// ProjectNR.Spawn.Picker.

#include "Misc/AutomationTest.h"
#include "NRSpawnPicker.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace NRSpawnPickerTests
{
	using FPicker = FNRSpawnPicker;

	/** Count points in a row 400 cm apart, all of Kind. */
	TArray<FPicker::FCandidate> MakePoints(int32 Count, ENRSpawnKind Kind = ENRSpawnKind::Melee)
	{
		TArray<FPicker::FCandidate> Points;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FPicker::FCandidate& Point = Points.AddDefaulted_GetRef();
			Point.Location = FVector(400.0f * Index, 0.0f, 0.0f);
			Point.Kind = Kind;
		}
		return Points;
	}

	FPicker::FParams MakeParams(float MinRatio = 0.6f, float MaxRatio = 0.8f)
	{
		FPicker::FParams Params;
		Params.Rule.MinUseRatio = MinRatio;
		Params.Rule.MaxUseRatio = MaxRatio;
		return Params;
	}

	int32 CountKind(const TArray<FPicker::FPick>& Picks, ENRSpawnKind Kind)
	{
		return Picks.FilterByPredicate([Kind](const FPicker::FPick& Pick) { return Pick.Kind == Kind; }).Num();
	}

	bool Contains(const TArray<FPicker::FPick>& Picks, int32 Candidate)
	{
		return Picks.ContainsByPredicate([Candidate](const FPicker::FPick& Pick) { return Pick.Candidate == Candidate; });
	}

	bool AllDistinct(const TArray<FPicker::FPick>& Picks)
	{
		TSet<int32> Seen;
		for (const FPicker::FPick& Pick : Picks)
		{
			bool bAlready = false;
			Seen.Add(Pick.Candidate, &bAlready);
			if (bAlready)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNRSpawnPickerTest, "ProjectNR.Spawn.Picker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNRSpawnPickerTest::RunTest(const FString& Parameters)
{
	using namespace NRSpawnPickerTests;

	// 10 points at 60-80%: always 6 to 8, every value turning up, all different points.
	{
		TSet<int32> Seen;
		bool bInRange = true;
		bool bDistinct = true;
		for (int32 Seed = 1; Seed <= 200; ++Seed)
		{
			FRandomStream Random(Seed);
			const TArray<FPicker::FPick> Picks = FPicker::Pick(MakePoints(10), MakeParams(), Random);
			bInRange &= Picks.Num() >= 6 && Picks.Num() <= 8;
			bDistinct &= AllDistinct(Picks);
			Seen.Add(Picks.Num());
		}
		TestTrue(TEXT("share: 6 to 8 of 10"), bInRange);
		TestTrue(TEXT("share: no point twice"), bDistinct);
		TestEqual(TEXT("share: 6, 7 and 8 all turn up"), Seen.Num(), 3);
	}

	// Bounds: the minimum lifts a small room, the maximum caps a big one.
	{
		FRandomStream Random(7);
		FPicker::FParams Params = MakeParams();
		Params.Rule.MinEnemies = 3;
		TestEqual(TEXT("min: 2 points at 60-80% still give 2 (no more than exist)"), FPicker::Pick(MakePoints(2), Params, Random).Num(), 2);
		TestTrue(TEXT("min: 4 points at 60-80% give at least 3"), FPicker::Pick(MakePoints(4), Params, Random).Num() >= 3);
		Params.Rule.MinEnemies = 1;
		Params.Rule.MaxEnemies = 5;
		TestEqual(TEXT("max: 20 points capped at 5"), FPicker::Pick(MakePoints(20), Params, Random).Num(), 5);
	}

	// Required points are always in.
	{
		bool bAlways = true;
		for (int32 Seed = 1; Seed <= 50; ++Seed)
		{
			TArray<FPicker::FCandidate> Points = MakePoints(10);
			Points[9].bRequired = true;
			FRandomStream Random(Seed);
			bAlways &= Contains(FPicker::Pick(Points, MakeParams(), Random), 9);
		}
		TestTrue(TEXT("required: always picked"), bAlways);
	}

	// Boss points: used, outside the share, only while there is a boss.
	{
		TArray<FPicker::FCandidate> Points = MakePoints(10);
		Points.AddDefaulted_GetRef().Kind = ENRSpawnKind::Boss;
		FPicker::FParams Params = MakeParams(1.0f, 1.0f);
		FRandomStream Random(3);
		Params.bHasBoss = false;
		const TArray<FPicker::FPick> Without = FPicker::Pick(Points, Params, Random);
		TestEqual(TEXT("boss: none without a boss class"), CountKind(Without, ENRSpawnKind::Boss), 0);
		TestEqual(TEXT("boss: minions still 10"), Without.Num(), 10);
		Params.bHasBoss = true;
		const TArray<FPicker::FPick> With = FPicker::Pick(Points, Params, Random);
		TestEqual(TEXT("boss: one with a boss class"), CountKind(With, ENRSpawnKind::Boss), 1);
		TestEqual(TEXT("boss: on top of the 10 minions"), With.Num(), 11);
	}

	// Weights: a heavy point is drawn far more often than a light one.
	{
		int32 Heavy = 0;
		int32 Light = 0;
		for (int32 Seed = 1; Seed <= 300; ++Seed)
		{
			TArray<FPicker::FCandidate> Points = MakePoints(10);
			Points[0].Weight = 10.0f;
			FPicker::FParams Params = MakeParams(0.1f, 0.1f);
			FRandomStream Random(Seed);
			const TArray<FPicker::FPick> Picks = FPicker::Pick(Points, Params, Random);
			Heavy += Contains(Picks, 0) ? 1 : 0;
			Light += Contains(Picks, 5) ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("weight: heavy point %d times, light %d"), Heavy, Light), Heavy > Light * 3);
	}

	// Spacing: two points 100 cm apart are not both taken while farther points remain.
	{
		bool bApart = true;
		for (int32 Seed = 1; Seed <= 100; ++Seed)
		{
			TArray<FPicker::FCandidate> Points = MakePoints(10);
			Points[1].Location = Points[0].Location + FVector(100.0f, 0.0f, 0.0f);
			FRandomStream Random(Seed);
			const TArray<FPicker::FPick> Picks = FPicker::Pick(Points, MakeParams(0.5f, 0.5f), Random);
			bApart &= !(Contains(Picks, 0) && Contains(Picks, 1));
		}
		TestTrue(TEXT("spacing: close pair never both picked"), bApart);
	}

	// Ranged cap: ranged and any points beyond it become close-in enemies.
	{
		TArray<FPicker::FCandidate> Points = MakePoints(10, ENRSpawnKind::Ranged);
		FPicker::FParams Params = MakeParams(1.0f, 1.0f);
		Params.Rule.MaxRanged = 3;
		FRandomStream Random(11);
		const TArray<FPicker::FPick> Picks = FPicker::Pick(Points, Params, Random);
		TestEqual(TEXT("cap: 3 ranged"), CountKind(Picks, ENRSpawnKind::Ranged), 3);
		TestEqual(TEXT("cap: 7 close-in"), CountKind(Picks, ENRSpawnKind::Melee), 7);
		TestEqual(TEXT("melee points stay melee"), CountKind(FPicker::Pick(MakePoints(10), Params, Random), ENRSpawnKind::Ranged), 0);
	}

	// Unusable points are never drawn but still count towards the share.
	{
		TArray<FPicker::FCandidate> Points = MakePoints(10);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Points[Index].bUsable = false;
		}
		FRandomStream Random(5);
		const TArray<FPicker::FPick> Picks = FPicker::Pick(Points, MakeParams(0.7f, 0.7f), Random);
		TestEqual(TEXT("unusable: still 7 of 10"), Picks.Num(), 7);
		TestTrue(TEXT("unusable: none of them drawn"), !Contains(Picks, 0) && !Contains(Picks, 1) && !Contains(Picks, 2));
	}

	// The same stream gives the same picks.
	{
		TArray<FPicker::FCandidate> Points = MakePoints(12, ENRSpawnKind::Any);
		FRandomStream A(1234);
		FRandomStream B(1234);
		const TArray<FPicker::FPick> First = FPicker::Pick(Points, MakeParams(), A);
		const TArray<FPicker::FPick> Second = FPicker::Pick(Points, MakeParams(), B);
		bool bSame = First.Num() == Second.Num();
		for (int32 Index = 0; bSame && Index < First.Num(); ++Index)
		{
			bSame = First[Index].Candidate == Second[Index].Candidate && First[Index].Kind == Second[Index].Kind;
		}
		TestTrue(TEXT("reproducible"), bSame);
	}

	return true;
}

#endif
