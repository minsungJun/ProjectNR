// Neural Enemy AI prototype module.

#include "Combat/PositionalSlotAssigner.h"

#include "Algo/Sort.h"

TArray<FVector> FPositionalSlotAssigner::RingPositions(const FVector& Center, int32 Count, float Radius)
{
	TArray<FVector> Positions;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = 2.0f * PI * Index / FMath::Max(Count, 1);
		Positions.Add(Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius);
	}
	return Positions;
}

float FPositionalSlotAssigner::WalkCost(const FVector& Target, const FVector& From, const FVector& Slot)
{
	const FVector FromOffset = FVector(From.X - Target.X, From.Y - Target.Y, 0.0f);
	const FVector SlotOffset = FVector(Slot.X - Target.X, Slot.Y - Target.Y, 0.0f);
	const float Straight = FVector::Dist2D(From, Slot);
	if (FromOffset.IsNearlyZero() || SlotOffset.IsNearlyZero())
	{
		return Straight;
	}
	// The angle between where we stand and where the slot is, seen from the target: walking
	// round to the far side costs that arc on the slot's circle, on top of the straight line.
	const float CosAngle = FVector::DotProduct(FromOffset.GetSafeNormal(), SlotOffset.GetSafeNormal());
	const float Angle = FMath::Acos(FMath::Clamp(CosAngle, -1.0f, 1.0f));
	return Straight + Angle * SlotOffset.Size();
}

TArray<int32> FPositionalSlotAssigner::Assign(const TArray<FSlot>& Slots, const TArray<FClaimant>& Claimants, const FParams& Params)
{
	TArray<int32> Result;
	Result.Init(INDEX_NONE, Claimants.Num());
	TArray<bool> Taken;
	Taken.Init(false, Slots.Num());

	auto Holdable = [&Slots](int32 Slot) { return Slots.IsValidIndex(Slot) && Slots[Slot].bReachable; };

	// Nearest the target first: whoever is closest has first pick.
	TArray<int32> Order;
	for (int32 Index = 0; Index < Claimants.Num(); ++Index)
	{
		Order.Add(Index);
	}
	Algo::Sort(Order, [&](int32 A, int32 B)
	{
		const float DistA = FVector::DistSquared2D(Claimants[A].Location, Params.Target);
		const float DistB = FVector::DistSquared2D(Claimants[B].Location, Params.Target);
		return DistA != DistB ? DistA < DistB : Claimants[A].Id < Claimants[B].Id;
	});

	// 1. Fresh holds are kept before anyone chooses.
	for (const int32 Index : Order)
	{
		const FClaimant& Claimant = Claimants[Index];
		if (Holdable(Claimant.CurrentSlot) && !Taken[Claimant.CurrentSlot] && Claimant.HeldFor < Params.HoldTime)
		{
			Result[Index] = Claimant.CurrentSlot;
			Taken[Claimant.CurrentSlot] = true;
		}
	}

	// 2. Everyone else picks in order.
	for (const int32 Index : Order)
	{
		if (Result[Index] != INDEX_NONE)
		{
			continue;
		}
		const FClaimant& Claimant = Claimants[Index];
		int32 Best = INDEX_NONE;
		float BestCost = TNumericLimits<float>::Max();
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			if (!Slots[SlotIndex].bReachable || Taken[SlotIndex])
			{
				continue;
			}
			const float SlotCost = WalkCost(Params.Target, Claimant.Location, Slots[SlotIndex].Location);
			if (SlotCost < BestCost)
			{
				Best = SlotIndex;
				BestCost = SlotCost;
			}
		}

		// Stay put unless the move is clearly better.
		const int32 Current = Claimant.CurrentSlot;
		if (Holdable(Current) && !Taken[Current])
		{
			const float CurrentCost = WalkCost(Params.Target, Claimant.Location, Slots[Current].Location);
			if (Best == INDEX_NONE || BestCost >= CurrentCost * (1.0f - Params.SwitchGain))
			{
				Best = Current;
			}
		}

		if (Best != INDEX_NONE)
		{
			Result[Index] = Best;
			Taken[Best] = true;
		}
	}
	return Result;
}
