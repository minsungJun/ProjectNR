// Neural Enemy AI prototype module.

// Checks for FEnemySlotAssigner: distinct slots, the wait ring when the attack ring is full,
// blocked slots, holding and switching, promotion, and reproducibility. Automation test
// NeuralAI.CombatSlots.Assigner.

#include "Combat/EnemySlotAssigner.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EnemySlotAssignerTests
{
	using FAssigner = FEnemySlotAssigner;

	/** Attack ring of 8 at 180 cm and wait ring of 8 at 600 cm around the origin. */
	TArray<FAssigner::FSlot> MakeSlots(int32 AttackCount = 8, int32 WaitCount = 8)
	{
		TArray<FAssigner::FSlot> Slots;
		for (const FVector& Location : FAssigner::RingPositions(FVector::ZeroVector, AttackCount, 180.0f))
		{
			FAssigner::FSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Location = Location;
			Slot.Ring = 0;
		}
		for (const FVector& Location : FAssigner::RingPositions(FVector::ZeroVector, WaitCount, 600.0f))
		{
			FAssigner::FSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Location = Location;
			Slot.Ring = 1;
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

	int32 CountRing(const TArray<FAssigner::FSlot>& Slots, const TArray<int32>& Assigned, int32 Ring)
	{
		return Assigned.FilterByPredicate([&](int32 Slot) { return Slots.IsValidIndex(Slot) && Slots[Slot].Ring == Ring; }).Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemySlotAssignerTest, "NeuralAI.CombatSlots.Assigner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEnemySlotAssignerTest::RunTest(const FString& Parameters)
{
	using namespace EnemySlotAssignerTests;
	FAssigner::FParams Params;

	// Ring layout: 8 slots, the first due +X, all at the radius.
	{
		const TArray<FVector> Ring = FAssigner::RingPositions(FVector(100.0f, 200.0f, 0.0f), 8, 180.0f);
		TestEqual(TEXT("ring has 8 slots"), Ring.Num(), 8);
		TestTrue(TEXT("first slot due +X"), Ring[0].Equals(FVector(280.0f, 200.0f, 0.0f), 0.01f));
		TestTrue(TEXT("third slot due +Y"), Ring[2].Equals(FVector(100.0f, 380.0f, 0.0f), 0.01f));
	}

	// Walking round the target costs more than reaching the near side.
	{
		const FVector East(800.0f, 0.0f, 0.0f);
		TestTrue(TEXT("east slot cheaper from the east"),
			FAssigner::Cost(FVector::ZeroVector, East, FVector(180.0f, 0.0f, 0.0f)) < FAssigner::Cost(FVector::ZeroVector, East, FVector(-180.0f, 0.0f, 0.0f)));
	}

	// Five enemies around: five different attack slots, each on its own side.
	{
		const TArray<FAssigner::FSlot> Slots = MakeSlots();
		const TArray<FAssigner::FClaimant> Claimants = MakeClaimants(5, 900.0f);
		const TArray<int32> Assigned = FAssigner::Assign(Slots, Claimants, Params);
		TestTrue(TEXT("five: all distinct"), AllDistinct(Assigned));
		TestEqual(TEXT("five: all on the attack ring"), CountRing(Slots, Assigned, 0), 5);
		for (int32 Index = 0; Index < Claimants.Num(); ++Index)
		{
			const FVector ToClaimant = Claimants[Index].Location.GetSafeNormal2D();
			const FVector ToSlot = Slots[Assigned[Index]].Location.GetSafeNormal2D();
			TestTrue(FString::Printf(TEXT("five: claimant %d gets a slot on its side"), Index), FVector::DotProduct(ToClaimant, ToSlot) > 0.5f);
		}
	}

	// Twelve enemies: the attack ring fills, the rest wait.
	{
		const TArray<FAssigner::FSlot> Slots = MakeSlots();
		const TArray<int32> Assigned = FAssigner::Assign(Slots, MakeClaimants(12, 700.0f), Params);
		TestTrue(TEXT("twelve: all distinct"), AllDistinct(Assigned));
		TestEqual(TEXT("twelve: 8 on the attack ring"), CountRing(Slots, Assigned, 0), 8);
		TestEqual(TEXT("twelve: 4 waiting"), CountRing(Slots, Assigned, 1), 4);
	}

	// Blocked slots are never given out.
	{
		TArray<FAssigner::FSlot> Slots = MakeSlots();
		for (int32 Index = 0; Index < 5; ++Index)
		{
			Slots[Index].bUsable = false;
		}
		const TArray<int32> Assigned = FAssigner::Assign(Slots, MakeClaimants(5, 900.0f), Params);
		TestTrue(TEXT("blocked: none handed out"), !Assigned.ContainsByPredicate([&](int32 Slot) { return Slots.IsValidIndex(Slot) && !Slots[Slot].bUsable; }));
		TestEqual(TEXT("blocked: 3 left on the attack ring"), CountRing(Slots, Assigned, 0), 3);
		TestEqual(TEXT("blocked: 2 waiting"), CountRing(Slots, Assigned, 1), 2);
	}

	// A fresh hold is kept even against someone nearer who would like it.
	{
		const TArray<FAssigner::FSlot> Slots = MakeSlots();
		TArray<FAssigner::FClaimant> Claimants;
		FAssigner::FClaimant& Holder = Claimants.AddDefaulted_GetRef();
		Holder.Id = 1;
		Holder.Location = FVector(-900.0f, 0.0f, 0.0f);
		Holder.CurrentSlot = 0; // due +X, the far side for it
		Holder.HeldFor = 0.2f;
		FAssigner::FClaimant& Near = Claimants.AddDefaulted_GetRef();
		Near.Id = 2;
		Near.Location = FVector(400.0f, 0.0f, 0.0f);
		const TArray<int32> Assigned = FAssigner::Assign(Slots, Claimants, Params);
		TestEqual(TEXT("hold: holder keeps slot 0"), Assigned[0], 0);
		TestNotEqual(TEXT("hold: the nearer one takes another"), Assigned[1], 0);
	}

	// Past the hold time, a slightly better slot is not worth moving for; a much better one is.
	{
		const TArray<FAssigner::FSlot> Slots = MakeSlots();
		FAssigner::FClaimant Claimant;
		Claimant.Id = 1;
		Claimant.HeldFor = 5.0f;
		Claimant.CurrentSlot = 1; // 45 degrees
		Claimant.Location = FVector(FMath::Cos(FMath::DegreesToRadians(30.0f)), FMath::Sin(FMath::DegreesToRadians(30.0f)), 0.0f) * 800.0f;
		TestEqual(TEXT("switch: small gain, stays"), FAssigner::Assign(Slots, { Claimant }, Params)[0], 1);
		Claimant.Location = FVector(0.0f, -800.0f, 0.0f); // now due -Y, slot 6 is right there
		TestEqual(TEXT("switch: large gain, moves"), FAssigner::Assign(Slots, { Claimant }, Params)[0], 6);
	}

	// Waiting in the outer ring, an attack slot frees up: step in, whatever the cost.
	{
		const TArray<FAssigner::FSlot> Slots = MakeSlots();
		FAssigner::FClaimant Claimant;
		Claimant.Id = 1;
		Claimant.HeldFor = 5.0f;
		Claimant.CurrentSlot = 8; // wait ring, due +X
		Claimant.Location = Slots[8].Location;
		const TArray<int32> Assigned = FAssigner::Assign(Slots, { Claimant }, Params);
		TestTrue(TEXT("promotion: moves to the attack ring"), Slots.IsValidIndex(Assigned[0]) && Slots[Assigned[0]].Ring == 0);
	}

	// The same input gives the same answer.
	{
		const TArray<FAssigner::FSlot> Slots = MakeSlots();
		const TArray<FAssigner::FClaimant> Claimants = MakeClaimants(11, 650.0f, 33.0f);
		TestTrue(TEXT("reproducible"), FAssigner::Assign(Slots, Claimants, Params) == FAssigner::Assign(Slots, Claimants, Params));
	}

	return true;
}

#endif
