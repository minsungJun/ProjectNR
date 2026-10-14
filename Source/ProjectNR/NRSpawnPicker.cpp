// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRSpawnPicker.h"

TArray<FNRSpawnPicker::FPick> FNRSpawnPicker::Pick(const TArray<FCandidate>& Candidates, const FParams& Params, FRandomStream& Random)
{
	TArray<FPick> Picks;
	const FNREncounterRule& Rule = Params.Rule;

	// 1. The boss, where there is one to place.
	TArray<int32> Others;
	TArray<int32> Usable;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FCandidate& Candidate = Candidates[Index];
		if (Candidate.Kind == ENRSpawnKind::Boss)
		{
			if (Params.bHasBoss && Candidate.bUsable)
			{
				Picks.Add({ Index, ENRSpawnKind::Boss });
			}
			continue;
		}
		Others.Add(Index);
		if (Candidate.bUsable)
		{
			Usable.Add(Index);
		}
	}

	// 2. How many: a share of all the room's other points, so where the player came in from
	// does not shrink the room, but never more than can be used.
	const float Ratio = Random.FRandRange(FMath::Min(Rule.MinUseRatio, Rule.MaxUseRatio), FMath::Max(Rule.MinUseRatio, Rule.MaxUseRatio));
	int32 Count = FMath::RoundToInt(Others.Num() * Ratio);
	Count = FMath::Clamp(Count, Rule.MinEnemies, FMath::Max(Rule.MinEnemies, Rule.MaxEnemies));
	Count = FMath::Min(Count, Usable.Num());

	// 3. Required points first, then by weight, apart from those already drawn while possible.
	TArray<int32> Drawn;
	for (const int32 Index : Usable)
	{
		if (Candidates[Index].bRequired && Drawn.Num() < Count)
		{
			Drawn.Add(Index);
		}
	}
	Usable.RemoveAll([&Drawn](int32 Index) { return Drawn.Contains(Index); });

	auto FarFromDrawn = [&](int32 Index)
	{
		return !Drawn.ContainsByPredicate([&](int32 Other)
		{
			return FVector::DistSquared2D(Candidates[Index].Location, Candidates[Other].Location) < FMath::Square(Params.MinSpacing);
		});
	};
	while (Drawn.Num() < Count && Usable.Num() > 0)
	{
		TArray<int32> Pool = Usable.FilterByPredicate(FarFromDrawn);
		if (Pool.Num() == 0)
		{
			Pool = Usable;
		}
		float Total = 0.0f;
		for (const int32 Index : Pool)
		{
			Total += FMath::Max(Candidates[Index].Weight, KINDA_SMALL_NUMBER);
		}
		float Roll = Random.FRandRange(0.0f, Total);
		int32 Chosen = Pool.Last();
		for (const int32 Index : Pool)
		{
			Roll -= FMath::Max(Candidates[Index].Weight, KINDA_SMALL_NUMBER);
			if (Roll <= 0.0f)
			{
				Chosen = Index;
				break;
			}
		}
		Drawn.Add(Chosen);
		Usable.Remove(Chosen);
	}

	// 4. What comes out of each: ranged up to the room's cap.
	int32 Ranged = 0;
	for (const int32 Index : Drawn)
	{
		const ENRSpawnKind Kind = Candidates[Index].Kind;
		bool bRanged = false;
		if (Ranged < Rule.MaxRanged)
		{
			bRanged = Kind == ENRSpawnKind::Ranged || (Kind == ENRSpawnKind::Any && Random.FRand() < Rule.AnyRangedChance);
		}
		Ranged += bRanged ? 1 : 0;
		Picks.Add({ Index, bRanged ? ENRSpawnKind::Ranged : ENRSpawnKind::Melee });
	}
	return Picks;
}
