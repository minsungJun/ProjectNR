// Neural Enemy AI prototype module.

// Checks for FPositionalSlotAssigner: ring layout, distinct slots, enemies beyond the slots,
// unreachable slots, holding and switching, and reproducibility. Automation test
// NeuralAI.PositionalSlots.Assigner.

#include "Combat/PositionalSlotAssigner.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PositionalSlotAssignerTests
{
	using FAssigner = FPositionalSlotAssigner;

	/** One ring of Count slots at Radius around the origin. */
	TArray<FAssigner::FSlot> MakeSlots(int32 Count = 8, float Radius = 350.0f)
	{
		TArray<FAssigner::FSlot> Slots;
		for (const FVector& Location : FAssigner::RingPositions(FVector::ZeroVector, Count, Radius))
		{
			Slots.AddDefaulted_GetRef().Location = Location;
		}
		return Slots;
	}

	/** Count claimants evenly round a circle of Radius, the first at Phase degrees. */
	TArray<FAssigner::FClaimant> MakeClaimants(int32 Count, float Radius, float Phase = 10.0f)
	{
		TArray<FAssigner::FClaimant> Claimants;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(Phase + 360.0f * Index / Count);
			FAssigner::FClaimant& Claimant = Claimants.AddDefaulted_GetRef();
			Claimant.Id = Index + 1;
			Claimant.Location = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * (Radius + 13.0f * Index);
		}
		return Claimants;
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPositionalSlotAssignerTest, "NeuralAI.PositionalSlots.Assigner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPositionalSlotAssignerTest::RunTest(const FString& Parameters)
{
	using namespace PositionalSlotAssignerTests;
	FAssigner::FParams Params;

	// Ring layout: 8 slots, the first due +X, all at the radius.
	{
		const TArray<FVector> Ring = FAssigner::RingPositions(FVector(100.0f, 200.0f, 0.0f), 8, 350.0f);
		TestEqual(TEXT("ring has 8 slots"), Ring.Num(), 8);
		TestTrue(TEXT("first slot due +X"), Ring[0].Equals(FVector(450.0f, 200.0f, 0.0f), 0.01f));
		TestTrue(TEXT("third slot due +Y"), Ring[2].Equals(FVector(100.0f, 550.0f, 0.0f), 0.01f));
	}

	// Walking round the target costs more than reaching the near side.
	{
		const FVector East(800.0f, 0.0f, 0.0f);
		TestTrue(TEXT("east slot cheaper from the east"),
			FAssigner::WalkCost(FVector::ZeroVector, East, FVector(350.0f, 0.0f, 0.0f)) < FAssigner::WalkCost(FVector::ZeroVector, East, FVector(-350.0f, 0.0f, 0.0f)));
	}

	// Five enemies around: five different slots.
	{
		const TArray<int32> Assigned = FAssigner::Assign(MakeSlots(), MakeClaimants(5, 900.0f), Params);
		TestTrue(TEXT("five: all distinct"), AllDistinct(Assigned));
		TestEqual(TEXT("five: all placed"), CountAssigned(Assigned), 5);
	}

	// Ten enemies, eight slots: eight placed, two without, no errors.
	{
		const TArray<int32> Assigned = FAssigner::Assign(MakeSlots(), MakeClaimants(10, 700.0f), Params);
		TestTrue(TEXT("ten: all distinct"), AllDistinct(Assigned));
		TestEqual(TEXT("ten: 8 placed"), CountAssigned(Assigned), 8);
	}

	// Unreachable slots are never given out.
	{
		TArray<FAssigner::FSlot> Slots = MakeSlots();
		for (int32 Index = 0; Index < 5; ++Index)
		{
			Slots[Index].bReachable = false;
		}
		const TArray<int32> Assigned = FAssigner::Assign(Slots, MakeClaimants(5, 900.0f), Params);
		TestTrue(TEXT("unreachable: none handed out"), !Assigned.ContainsByPredicate([&](int32 Slot) { return Slots.IsValidIndex(Slot) && !Slots[Slot].bReachable; }));
		TestEqual(TEXT("unreachable: 3 placed"), CountAssigned(Assigned), 3);
	}

	// A fresh hold is kept even against someone nearer who would like it.
	{
		TArray<FAssigner::FClaimant> Claimants;
		FAssigner::FClaimant& Holder = Claimants.AddDefaulted_GetRef();
		Holder.Id = 1;
		Holder.Location = FVector(-900.0f, 0.0f, 0.0f);
		Holder.CurrentSlot = 0; // due +X, the far side for it
		Holder.HeldFor = 0.2f;
		FAssigner::FClaimant& Near = Claimants.AddDefaulted_GetRef();
		Near.Id = 2;
		Near.Location = FVector(500.0f, 0.0f, 0.0f);
		const TArray<int32> Assigned = FAssigner::Assign(MakeSlots(), Claimants, Params);
		TestEqual(TEXT("hold: holder keeps slot 0"), Assigned[0], 0);
		TestNotEqual(TEXT("hold: the nearer one takes another"), Assigned[1], 0);
	}

	// Past the hold time, a slightly better slot is not worth moving for; a much better one is.
	{
		FAssigner::FClaimant Claimant;
		Claimant.Id = 1;
		Claimant.HeldFor = 5.0f;
		Claimant.CurrentSlot = 1; // 45 degrees
		Claimant.Location = FVector(FMath::Cos(FMath::DegreesToRadians(30.0f)), FMath::Sin(FMath::DegreesToRadians(30.0f)), 0.0f) * 800.0f;
		TestEqual(TEXT("switch: small gain, stays"), FAssigner::Assign(MakeSlots(), { Claimant }, Params)[0], 1);
		Claimant.Location = FVector(0.0f, -800.0f, 0.0f); // now due -Y, slot 6 is right there
		TestEqual(TEXT("switch: large gain, moves"), FAssigner::Assign(MakeSlots(), { Claimant }, Params)[0], 6);
	}

	// The same input gives the same answer.
	{
		const TArray<FAssigner::FClaimant> Claimants = MakeClaimants(11, 650.0f, 33.0f);
		TestTrue(TEXT("reproducible"), FAssigner::Assign(MakeSlots(), Claimants, Params) == FAssigner::Assign(MakeSlots(), Claimants, Params));
	}

	return true;
}

#endif
