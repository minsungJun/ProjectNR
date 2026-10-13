// Neural Enemy AI prototype module.

// Checks for FPositionalSlotAssigner: ring layout, spreading out, filling and overflowing the
// ring, unreachable slots, holding and switching, the brain's tilt (strafe, caution), and
// reproducibility. Automation test NeuralAI.PositionalSlots.Assigner.

#include "Combat/PositionalSlotAssigner.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PositionalSlotAssignerTests
{
	using FAssigner = FPositionalSlotAssigner;

	/** One ring of Count slots at Radius around the origin: slot i at 360 / Count * i degrees. */
	TArray<FAssigner::FSlot> MakeSlots(int32 Count = 8, float Radius = 350.0f)
	{
		TArray<FAssigner::FSlot> Slots;
		for (const FVector& Location : FAssigner::RingPositions(FVector::ZeroVector, Count, Radius))
		{
			Slots.AddDefaulted_GetRef().Location = Location;
		}
		return Slots;
	}

	FVector AtBearing(float Degrees, float Distance)
	{
		const float Radians = FMath::DegreesToRadians(Degrees);
		return FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.0f) * Distance;
	}

	FAssigner::FClaimant MakeClaimant(int32 Id, float Bearing, float Distance, int32 CurrentSlot = INDEX_NONE, float HeldFor = 5.0f)
	{
		FAssigner::FClaimant Claimant;
		Claimant.Id = Id;
		Claimant.Location = AtBearing(Bearing, Distance);
		Claimant.CurrentSlot = CurrentSlot;
		Claimant.HeldFor = HeldFor;
		return Claimant;
	}

	/** Count claimants bunched on one side, Spread degrees apart round Bearing. */
	TArray<FAssigner::FClaimant> MakeBunch(int32 Count, float Bearing = 0.0f, float Spread = 8.0f, float Distance = 1000.0f)
	{
		TArray<FAssigner::FClaimant> Claimants;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Claimants.Add(MakeClaimant(Index + 1, Bearing + (Index - (Count - 1) * 0.5f) * Spread, Distance + 10.0f * Index));
		}
		return Claimants;
	}

	TArray<int32> SlotsOf(const TArray<FAssigner::FResult>& Results)
	{
		TArray<int32> Slots;
		for (const FAssigner::FResult& Result : Results)
		{
			Slots.Add(Result.Slot);
		}
		return Slots;
	}

	bool AllDistinct(const TArray<int32>& Assigned)
	{
		TSet<int32> Seen;
		for (const int32 Slot : Assigned)
		{
			bool bAlready = false;
			if (Slot != INDEX_NONE)
			{
				Seen.Add(Slot, &bAlready);
			}
			if (bAlready)
			{
				return false;
			}
		}
		return true;
	}

	int32 CountAssigned(const TArray<int32>& Assigned)
	{
		return Assigned.FilterByPredicate([](int32 Slot) { return Slot != INDEX_NONE; }).Num();
	}

	/** Smallest angle between any two assigned slots, seen from the origin. */
	float SmallestGap(const TArray<FAssigner::FSlot>& Slots, const TArray<int32>& Assigned)
	{
		float Smallest = 360.0f;
		for (int32 A = 0; A < Assigned.Num(); ++A)
		{
			for (int32 B = A + 1; B < Assigned.Num(); ++B)
			{
				if (Assigned[A] != INDEX_NONE && Assigned[B] != INDEX_NONE)
				{
					Smallest = FMath::Min(Smallest, FAssigner::AngleBetween(
						FAssigner::BearingDegrees(FVector::ZeroVector, Slots[Assigned[A]].Location),
						FAssigner::BearingDegrees(FVector::ZeroVector, Slots[Assigned[B]].Location)));
				}
			}
		}
		return Smallest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPositionalSlotAssignerTest, "NeuralAI.PositionalSlots.Assigner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPositionalSlotAssignerTest::RunTest(const FString& Parameters)
{
	using namespace PositionalSlotAssignerTests;
	FAssigner::FParams Params;
	const TArray<FAssigner::FSlot> Slots = MakeSlots();

	// Ring layout: 8 slots, the first due +X, all at the radius.
	{
		const TArray<FVector> Ring = FAssigner::RingPositions(FVector(100.0f, 200.0f, 0.0f), 8, 350.0f);
		TestEqual(TEXT("ring has 8 slots"), Ring.Num(), 8);
		TestTrue(TEXT("first slot due +X"), Ring[0].Equals(FVector(450.0f, 200.0f, 0.0f), 0.01f));
		TestTrue(TEXT("third slot due +Y"), Ring[2].Equals(FVector(100.0f, 550.0f, 0.0f), 0.01f));
		TestEqual(TEXT("angle between 350 and 10 is 20"), FAssigner::AngleBetween(350.0f, 10.0f), 20.0f, 0.01f);
	}

	// One enemy, the target facing it: it takes the slot on its own side, not a long walk round.
	{
		Params.TargetForward = FVector::ForwardVector;
		const TArray<FAssigner::FResult> Results = FAssigner::Assign(Slots, { MakeClaimant(1, 0.0f, 1000.0f) }, Params);
		TestEqual(TEXT("one: slot on its side"), Results[0].Slot, 0);
	}

	// Slots at 0, 45 and 90 are held: a newcomer goes round the far side, 180 to 270.
	{
		TArray<FAssigner::FClaimant> Claimants;
		// Fresh holds, so the three stay where they are while the newcomer chooses.
		Claimants.Add(MakeClaimant(1, 0.0f, 350.0f, 0, 0.2f));
		Claimants.Add(MakeClaimant(2, 45.0f, 350.0f, 1, 0.2f));
		Claimants.Add(MakeClaimant(3, 90.0f, 350.0f, 2, 0.2f));
		Claimants.Add(MakeClaimant(4, 200.0f, 900.0f));
		const TArray<FAssigner::FResult> Results = FAssigner::Assign(Slots, Claimants, Params);
		const float Bearing = FAssigner::BearingDegrees(FVector::ZeroVector, Slots[Results[3].Slot].Location);
		TestTrue(FString::Printf(TEXT("newcomer goes to 180..270 (got %.0f)"), Bearing), Bearing >= 180.0f && Bearing <= 270.0f);
	}

	// Two, four and eight enemies arriving bunched on one side spread round the target.
	for (const TPair<int32, float>& Case : { TPair<int32, float>(2, 135.0f), TPair<int32, float>(4, 90.0f), TPair<int32, float>(8, 45.0f) })
	{
		const TArray<int32> Assigned = SlotsOf(FAssigner::Assign(Slots, MakeBunch(Case.Key), Params));
		const float Gap = SmallestGap(Slots, Assigned);
		TestTrue(FString::Printf(TEXT("%d bunched: all distinct"), Case.Key), AllDistinct(Assigned));
		TestEqual(FString::Printf(TEXT("%d bunched: all placed"), Case.Key), CountAssigned(Assigned), Case.Key);
		TestTrue(FString::Printf(TEXT("%d bunched: at least %.0f degrees apart (got %.0f)"), Case.Key, Case.Value, Gap), Gap >= Case.Value - 0.1f);
	}

	// Ten enemies, eight slots: eight placed, two without, no errors.
	{
		const TArray<int32> Assigned = SlotsOf(FAssigner::Assign(Slots, MakeBunch(10), Params));
		TestTrue(TEXT("ten: all distinct"), AllDistinct(Assigned));
		TestEqual(TEXT("ten: 8 placed"), CountAssigned(Assigned), 8);
	}

	// Unreachable slots are never given out.
	{
		TArray<FAssigner::FSlot> Blocked = Slots;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			Blocked[Index].bReachable = false;
		}
		const TArray<int32> Assigned = SlotsOf(FAssigner::Assign(Blocked, MakeBunch(5), Params));
		TestTrue(TEXT("unreachable: none handed out"), !Assigned.ContainsByPredicate([&](int32 Slot) { return Blocked.IsValidIndex(Slot) && !Blocked[Slot].bReachable; }));
		TestEqual(TEXT("unreachable: 3 placed"), CountAssigned(Assigned), 3);
	}

	// A held slot is never taken away, even by an enemy standing right next to it.
	{
		TArray<FAssigner::FClaimant> Claimants;
		Claimants.Add(MakeClaimant(1, 180.0f, 900.0f, 0, 0.2f)); // holds slot 0 from the far side
		Claimants.Add(MakeClaimant(2, 0.0f, 400.0f));
		const TArray<int32> Assigned = SlotsOf(FAssigner::Assign(Slots, Claimants, Params));
		TestEqual(TEXT("hold: holder keeps slot 0"), Assigned[0], 0);
		TestNotEqual(TEXT("hold: the nearer one takes another"), Assigned[1], 0);
	}

	// Past the hold time, a slot that is barely better is not worth moving for: from slot 0, with
	// the other enemy at 135 degrees, slot 7 spreads them a little further but not by enough.
	{
		TArray<FAssigner::FClaimant> Claimants;
		Claimants.Add(MakeClaimant(1, 0.0f, 350.0f, 0));
		Claimants.Add(MakeClaimant(2, 135.0f, 350.0f, 3));
		const TArray<int32> Assigned = SlotsOf(FAssigner::Assign(Slots, Claimants, Params));
		TestEqual(TEXT("switch: stays in slot 0"), Assigned[0], 0);
		TestEqual(TEXT("switch: stays in slot 3"), Assigned[1], 3);
	}

	// A slot far better than the one held is worth the move: crowded next to another enemy,
	// with the far side of the target empty.
	{
		TArray<FAssigner::FClaimant> Claimants;
		Claimants.Add(MakeClaimant(1, 0.0f, 350.0f, 0));
		Claimants.Add(MakeClaimant(2, 45.0f, 350.0f, 1));
		FAssigner::FParams NoFlank = Params;
		NoFlank.Weights.Flank = 0.0f;
		NoFlank.Weights.Separation = 4.0f;
		const TArray<int32> Assigned = SlotsOf(FAssigner::Assign(Slots, Claimants, NoFlank));
		TestTrue(TEXT("switch: one of the crowded pair moves away"), SmallestGap(Slots, Assigned) > 45.0f);
	}

	// On its way round to slot 4 behind the target, an enemy passing slot 0 in front keeps going.
	{
		TArray<FAssigner::FClaimant> Claimants;
		Claimants.Add(MakeClaimant(1, 90.0f, 350.0f, 2, 0.2f));
		Claimants.Add(MakeClaimant(2, 270.0f, 350.0f, 6, 0.2f));
		Claimants.Add(MakeClaimant(3, 0.0f, 600.0f, 4));
		const TArray<int32> Assigned = SlotsOf(FAssigner::Assign(Slots, Claimants, Params));
		TestEqual(TEXT("en route: keeps slot 4"), Assigned[2], 4);
	}

	// Strafing raises the neighbour on that side; caution raises the slots behind the target.
	{
		FAssigner::FClaimant Claimant = MakeClaimant(1, 0.0f, 350.0f, 0);
		const TArray<int32> None;
		// Facing the target from slot 0 (due +X) the enemy's right is -Y: slot 7 (315 degrees).
		const float Still = FAssigner::ScoreSlot(Slots, 7, Claimant, None, Params).Total;
		Claimant.Intent.Strafe = 1.0f;
		TestTrue(TEXT("strafe right raises the right neighbour"), FAssigner::ScoreSlot(Slots, 7, Claimant, None, Params).Total > Still);
		TestEqual(TEXT("strafe right leaves the left neighbour"), FAssigner::ScoreSlot(Slots, 1, Claimant, None, Params).Strafe, 0.0f);

		Claimant.Intent = FAssigner::FIntent();
		Params.TargetForward = FVector::ForwardVector;
		const float Calm = FAssigner::ScoreSlot(Slots, 4, Claimant, None, Params).Flank;
		Claimant.Intent.Caution = 1.0f;
		TestTrue(TEXT("caution raises the slot behind the target"), FAssigner::ScoreSlot(Slots, 4, Claimant, None, Params).Flank > Calm);
		TestEqual(TEXT("caution leaves the slot in front"), FAssigner::ScoreSlot(Slots, 0, Claimant, None, Params).Flank, 0.0f, 0.001f);
	}

	// The same input gives the same answer.
	{
		const TArray<FAssigner::FClaimant> Claimants = MakeBunch(11, 33.0f, 20.0f, 650.0f);
		TestTrue(TEXT("reproducible"), SlotsOf(FAssigner::Assign(Slots, Claimants, Params)) == SlotsOf(FAssigner::Assign(Slots, Claimants, Params)));
	}

	return true;
}

#endif
