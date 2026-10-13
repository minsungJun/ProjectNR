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

float FPositionalSlotAssigner::BearingDegrees(const FVector& Target, const FVector& Location)
{
	const float Degrees = FMath::RadiansToDegrees(FMath::Atan2(Location.Y - Target.Y, Location.X - Target.X));
	return Degrees < 0.0f ? Degrees + 360.0f : Degrees;
}

float FPositionalSlotAssigner::AngleBetween(float A, float B)
{
	const float Difference = FMath::Abs(FMath::Fmod(A - B, 360.0f));
	return Difference > 180.0f ? 360.0f - Difference : Difference;
}

FPositionalSlotAssigner::FScore FPositionalSlotAssigner::ScoreSlot(const TArray<FSlot>& Slots, int32 SlotIndex,
	const FClaimant& Claimant, const TArray<int32>& OtherSlots, const FParams& Params)
{
	const FWeights& Weights = Params.Weights;
	const FVector& SlotLocation = Slots[SlotIndex].Location;
	const float Bearing = BearingDegrees(Params.Target, SlotLocation);
	FScore Score;

	// Separation: as far round from everyone else as possible.
	float Nearest = 180.0f;
	for (const int32 Other : OtherSlots)
	{
		if (Slots.IsValidIndex(Other) && Other != SlotIndex)
		{
			Nearest = FMath::Min(Nearest, AngleBetween(Bearing, BearingDegrees(Params.Target, Slots[Other].Location)));
		}
	}
	Score.Separation = Weights.Separation * Nearest / 180.0f;

	// Flank: the target's sides and back; a cautious enemy cares more.
	const FVector Facing = Params.TargetForward.GetSafeNormal2D();
	const FVector ToSlot = FVector(SlotLocation.X - Params.Target.X, SlotLocation.Y - Params.Target.Y, 0.0f).GetSafeNormal();
	if (!Facing.IsNearlyZero() && !ToSlot.IsNearlyZero())
	{
		const float Flank = (1.0f - FVector::DotProduct(Facing, ToSlot)) * 0.5f;
		Score.Flank = Weights.Flank * (1.0f + FMath::Clamp(Claimant.Intent.Caution, 0.0f, 1.0f)) * Flank;
	}

	// Distance: the walk there; an enemy that wants to close in cares more.
	const float Walk = WalkCost(Params.Target, Claimant.Location, SlotLocation);
	Score.Distance = -Weights.Distance * (1.0f + FMath::Clamp(Claimant.Intent.Approach, 0.0f, 1.0f)) * Walk / FMath::Max(Weights.DistanceScale, 1.0f);

	// Strafe: the neighbouring slot on the side the enemy is stepping to.
	const int32 Current = Claimant.CurrentSlot;
	if (Slots.IsValidIndex(Current) && Current != SlotIndex && Slots.Num() > 2 && !FMath::IsNearlyZero(Claimant.Intent.Strafe))
	{
		const int32 Count = Slots.Num();
		const bool bNeighbour = SlotIndex == (Current + 1) % Count || SlotIndex == (Current + Count - 1) % Count;
		if (bNeighbour)
		{
			// Standing in its slot facing the target, the enemy's right is up x forward.
			const FVector Forward = FVector(Params.Target.X - Slots[Current].Location.X, Params.Target.Y - Slots[Current].Location.Y, 0.0f).GetSafeNormal();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
			const float Side = FMath::Sign(FVector::DotProduct(SlotLocation - Slots[Current].Location, Right));
			Score.Strafe = Weights.Strafe * FMath::Max(0.0f, Side * FMath::Clamp(Claimant.Intent.Strafe, -1.0f, 1.0f));
		}
	}

	Score.Total = Score.Separation + Score.Flank + Score.Distance + Score.Strafe;
	return Score;
}

TArray<FPositionalSlotAssigner::FResult> FPositionalSlotAssigner::Assign(const TArray<FSlot>& Slots,
	const TArray<FClaimant>& Claimants, const FParams& Params)
{
	TArray<FResult> Result;
	Result.SetNum(Claimants.Num());
	TArray<bool> Decided;
	Decided.Init(false, Claimants.Num());

	auto Holdable = [&Slots](int32 Slot) { return Slots.IsValidIndex(Slot) && Slots[Slot].bReachable; };

	// Holders first, so nobody is chased out of a slot; then nearest the target.
	TArray<int32> Order;
	for (int32 Index = 0; Index < Claimants.Num(); ++Index)
	{
		Order.Add(Index);
	}
	Algo::Sort(Order, [&](int32 A, int32 B)
	{
		const bool bHoldsA = Holdable(Claimants[A].CurrentSlot);
		const bool bHoldsB = Holdable(Claimants[B].CurrentSlot);
		if (bHoldsA != bHoldsB)
		{
			return bHoldsA;
		}
		const float DistA = FVector::DistSquared2D(Claimants[A].Location, Params.Target);
		const float DistB = FVector::DistSquared2D(Claimants[B].Location, Params.Target);
		return DistA != DistB ? DistA < DistB : Claimants[A].Id < Claimants[B].Id;
	});

	// Where everyone else is, for one claimant: what is decided so far, and the slots the
	// undecided still hold - they count as staying until they choose otherwise.
	auto OthersOf = [&](int32 Self)
	{
		TArray<int32> Others;
		for (int32 Index = 0; Index < Claimants.Num(); ++Index)
		{
			if (Index == Self)
			{
				continue;
			}
			const int32 Slot = Decided[Index] ? Result[Index].Slot : (Holdable(Claimants[Index].CurrentSlot) ? Claimants[Index].CurrentSlot : INDEX_NONE);
			if (Slot != INDEX_NONE)
			{
				Others.Add(Slot);
			}
		}
		return Others;
	};

	// 1. Fresh holds are kept before anyone chooses.
	for (const int32 Index : Order)
	{
		const FClaimant& Claimant = Claimants[Index];
		if (Holdable(Claimant.CurrentSlot) && Claimant.HeldFor < Params.HoldTime)
		{
			Result[Index].Slot = Claimant.CurrentSlot;
			Decided[Index] = true;
		}
	}
	for (const int32 Index : Order)
	{
		if (Decided[Index])
		{
			Result[Index].Score = ScoreSlot(Slots, Result[Index].Slot, Claimants[Index], OthersOf(Index), Params);
		}
	}

	// 2. Everyone else chooses in order.
	for (const int32 Index : Order)
	{
		if (Decided[Index])
		{
			continue;
		}
		const FClaimant& Claimant = Claimants[Index];
		const TArray<int32> Others = OthersOf(Index);

		int32 Best = INDEX_NONE;
		FScore BestScore;
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			if (!Slots[SlotIndex].bReachable || SlotIndex == Claimant.CurrentSlot || Others.Contains(SlotIndex))
			{
				continue;
			}
			const FScore Score = ScoreSlot(Slots, SlotIndex, Claimant, Others, Params);
			if (Best == INDEX_NONE || Score.Total > BestScore.Total)
			{
				Best = SlotIndex;
				BestScore = Score;
			}
		}

		// Holding a slot already: weigh the others by the extra walk, not the whole walk, and stay
		// unless one is clearly better than the slot held, bonus included.
		const int32 Current = Claimant.CurrentSlot;
		if (Holdable(Current) && !Others.Contains(Current))
		{
			const FScore CurrentScore = ScoreSlot(Slots, Current, Claimant, Others, Params);
			auto Switching = [&](const FScore& Score)
			{
				const float Extra = Score.Distance - CurrentScore.Distance;
				return Score.Total - Score.Distance + (Extra < 0.0f ? Extra : Extra * Params.Weights.ShorterWalkReward);
			};
			const float Stay = CurrentScore.Total - CurrentScore.Distance + Params.Weights.CurrentSlotBonus;

			Best = Current;
			BestScore = CurrentScore;
			float BestSwitching = Stay + Params.Weights.SwitchThreshold;
			for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
			{
				if (!Slots[SlotIndex].bReachable || SlotIndex == Current || Others.Contains(SlotIndex))
				{
					continue;
				}
				const FScore Score = ScoreSlot(Slots, SlotIndex, Claimant, Others, Params);
				if (Switching(Score) > BestSwitching)
				{
					Best = SlotIndex;
					BestScore = Score;
					BestSwitching = Switching(Score);
				}
			}
		}

		Result[Index].Slot = Best;
		Result[Index].Score = BestScore;
		Decided[Index] = true;
	}
	return Result;
}
