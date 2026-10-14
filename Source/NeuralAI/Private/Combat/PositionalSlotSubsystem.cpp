// Neural Enemy AI prototype module.

#include "Combat/PositionalSlotSubsystem.h"

#include "Combat/PositionalSlotSettings.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "NeuralAI.h"

static TAutoConsoleVariable<int32> CVarPositionalSlotMode(TEXT("NeuralAI.PositionalSlots.Mode"), 2,
	TEXT("0: no positional slots, enemies walk at the player. 1: assign slots but move as before (to watch). 2: enemies go to their slots."));

static TAutoConsoleVariable<int32> CVarPositionalSlotDebug(TEXT("NeuralAI.PositionalSlots.Debug"), 0,
	TEXT("1: draw the slots (green free, yellow reserved, red occupied, grey unreachable), who holds them and their scores."));

namespace
{
	/** Seconds without a RequestSlot before a claim lapses. */
	constexpr float PositionalSlotClaimLapse = 0.3f;
	bool ShouldDrawDebug()
	{
		return CVarPositionalSlotDebug.GetValueOnGameThread() != 0 || GetDefault<UPositionalSlotSettings>()->bDrawDebug;
	}
}

bool UPositionalSlotSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPositionalSlotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPositionalSlotSubsystem, STATGROUP_Tickables);
}

bool UPositionalSlotSubsystem::IsAssigning() const
{
	return GetDefault<UPositionalSlotSettings>()->bEnabled && CVarPositionalSlotMode.GetValueOnGameThread() >= 1;
}

bool UPositionalSlotSubsystem::IsDriving() const
{
	return GetDefault<UPositionalSlotSettings>()->bEnabled && CVarPositionalSlotMode.GetValueOnGameThread() >= 2;
}

UPositionalSlotSubsystem::FTargetSlots* UPositionalSlotSubsystem::FindSlotsOf(const AActor* Enemy, int32* OutClaim)
{
	return const_cast<FTargetSlots*>(static_cast<const UPositionalSlotSubsystem*>(this)->FindSlotsOf(Enemy, OutClaim));
}

const UPositionalSlotSubsystem::FTargetSlots* UPositionalSlotSubsystem::FindSlotsOf(const AActor* Enemy, int32* OutClaim) const
{
	for (const FTargetSlots& Entry : Targets)
	{
		const int32 Claim = Entry.Claims.IndexOfByPredicate([Enemy](const FClaim& Candidate) { return Candidate.Enemy.Get() == Enemy; });
		if (Claim != INDEX_NONE)
		{
			if (OutClaim)
			{
				*OutClaim = Claim;
			}
			return &Entry;
		}
	}
	return nullptr;
}

void UPositionalSlotSubsystem::RequestSlot(AActor* Enemy, AActor* Target, const FPositionalSlotAssigner::FIntent& Intent, int32 Ring)
{
	Ring = FMath::Clamp(Ring, 0, UPositionalSlotSettings::RingCount - 1);
	const UWorld* World = GetWorld();
	if (!World || !Enemy || !Target)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	int32 ClaimIndex = INDEX_NONE;
	if (FTargetSlots* Current = FindSlotsOf(Enemy, &ClaimIndex))
	{
		if (Current->Target.Get() == Target)
		{
			FClaim& Claim = Current->Claims[ClaimIndex];
			Claim.LastRequest = Now;
			Claim.Intent = Intent;
			if (Claim.Ring != Ring)
			{
				Claim.Ring = Ring;
				Claim.Slot = INDEX_NONE;
				Current->bDirty = true;
			}
			return;
		}
		// Switched targets: leave the old ring.
		ReleaseSlot(Enemy, TEXT("switched targets"));
	}

	FTargetSlots* Entry = Targets.FindByPredicate([Target](const FTargetSlots& Candidate) { return Candidate.Target.Get() == Target; });
	if (!Entry)
	{
		Entry = &Targets.AddDefaulted_GetRef();
		Entry->Target = Target;
	}
	FClaim& Claim = Entry->Claims.AddDefaulted_GetRef();
	Claim.Enemy = Enemy;
	Claim.Id = NextClaimId++;
	Claim.LastRequest = Now;
	Claim.Intent = Intent;
	Claim.Ring = Ring;
	// A newcomer should not wait for the next round to know where to go.
	Entry->bDirty = true;
}

void UPositionalSlotSubsystem::ReleaseSlot(AActor* Enemy, const TCHAR* Reason)
{
	for (FTargetSlots& Entry : Targets)
	{
		const int32 ClaimIndex = Entry.Claims.IndexOfByPredicate([Enemy](const FClaim& Claim) { return Claim.Enemy.Get() == Enemy; });
		if (ClaimIndex != INDEX_NONE)
		{
			LogRelease(Entry, Entry.Claims[ClaimIndex], Reason);
			Entry.Claims.RemoveAt(ClaimIndex);
			UpdateOccupancy(Entry);
		}
	}
}

void UPositionalSlotSubsystem::LogRelease(const FTargetSlots& Entry, const FClaim& Claim, const TCHAR* Reason) const
{
	if (Claim.Slot != INDEX_NONE && CVarPositionalSlotDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogNeuralAI, Log, TEXT("%s gives back slot %d of %s: %s"), *GetNameSafe(Claim.Enemy.Get()), Claim.Slot,
			*GetNameSafe(Entry.Target.Get()), Reason);
	}
}

bool UPositionalSlotSubsystem::GetSlot(const AActor* Enemy, FVector& OutLocation) const
{
	int32 ClaimIndex = INDEX_NONE;
	const FTargetSlots* Entry = FindSlotsOf(Enemy, &ClaimIndex);
	const AActor* Target = Entry ? Entry->Target.Get() : nullptr;
	if (!Target)
	{
		return false;
	}
	const int32 Slot = Entry->Claims[ClaimIndex].Slot;
	if (!Entry->Slots.IsValidIndex(Slot))
	{
		return false;
	}
	OutLocation = Target->GetActorLocation() + Entry->Slots[Slot].Offset;
	return true;
}

bool UPositionalSlotSubsystem::GetStandPoint(const AActor* Enemy, FVector& OutLocation, bool& bOutHasSlot) const
{
	bOutHasSlot = GetSlot(Enemy, OutLocation);
	if (bOutHasSlot)
	{
		return true;
	}
	int32 ClaimIndex = INDEX_NONE;
	const FTargetSlots* Entry = FindSlotsOf(Enemy, &ClaimIndex);
	const AActor* Target = Entry ? Entry->Target.Get() : nullptr;
	if (!Target || Entry->Claims[ClaimIndex].HoldOffset.IsNearlyZero())
	{
		return false;
	}
	OutLocation = Target->GetActorLocation() + Entry->Claims[ClaimIndex].HoldOffset;
	return true;
}

FVector UPositionalSlotSubsystem::FindHoldOffset(const AActor* Target, const AActor* Enemy, int32 Ring) const
{
	const UPositionalSlotSettings* Settings = GetDefault<UPositionalSlotSettings>();
	const FVector Center = Target->GetActorLocation();
	FVector Away = (Enemy->GetActorLocation() - Center).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = FVector::ForwardVector;
	}
	// A shooter left without a slot stays about where it is, still in range; a close-in enemy
	// waits clear of the ring.
	const float Distance = Ring == 1
		? FMath::Clamp(FVector::Dist2D(Enemy->GetActorLocation(), Center), Settings->RangedSlotRadius * 0.6f, Settings->RangedSlotRadius)
		: Settings->NoSlotHoldDistance;
	FVector Point = Center + Away * Distance;
	const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (Navigation && Navigation->ProjectPointToNavigation(Point, Projected, FVector(150.0f, 150.0f, Settings->NavProjectionHeight)))
	{
		Point = Projected.Location;
	}
	// Never on top of the target, even when the navmesh only has room close by.
	const FVector Offset = Point - Center;
	return Offset.Size2D() < 100.0f ? Away * Distance : Offset;
}

void UPositionalSlotSubsystem::RefreshSlots(FTargetSlots& Entry) const
{
	const UPositionalSlotSettings* Settings = GetDefault<UPositionalSlotSettings>();
	const AActor* Target = Entry.Target.Get();
	UWorld* World = GetWorld();
	if (!Target || !World)
	{
		return;
	}
	const FVector Center = Target->GetActorLocation();

	// Every ring in turn: the close ring first, then the ranged ring turned half a slot so its
	// shooters stand between, not behind, the enemies in close.
	TArray<FVector> Positions;
	TArray<int32> Rings;
	TArray<float> Angles;
	for (int32 Ring = 0; Ring < UPositionalSlotSettings::RingCount; ++Ring)
	{
		const int32 Count = Settings->GetRingSlotCount(Ring);
		const float Turn = Ring == 0 ? 0.0f : 180.0f / Count;
		for (const FVector& Position : FPositionalSlotAssigner::RingPositions(FVector::ZeroVector, Count, Settings->GetRingRadius(Ring)))
		{
			Positions.Add(Center + Position.RotateAngleAxis(Turn, FVector::UpVector));
			Rings.Add(Ring);
			Angles.Add(FMath::Fmod(FPositionalSlotAssigner::BearingDegrees(FVector::ZeroVector, Position) + Turn, 360.0f));
		}
	}

	// The slot count may have been changed in the editor: keep holders only while it is the same.
	if (Entry.Slots.Num() != Positions.Num())
	{
		Entry.Slots.SetNum(Positions.Num());
		for (FClaim& Claim : Entry.Claims)
		{
			Claim.Slot = INDEX_NONE;
		}
	}

	// A slot must land on the navmesh and be in plain sight of the target: through a wall it
	// could be reached only the long way round, if at all.
	const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const FVector Extent(50.0f, 50.0f, Settings->NavProjectionHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PositionalSlot), false, Target);

	// The floor the target stands on, to keep slots on it.
	FNavLocation TargetFloor;
	const bool bTargetOnNav = Navigation && Navigation->ProjectPointToNavigation(Center, TargetFloor, Extent);
	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		FPositionalSlot& Slot = Entry.Slots[Index];
		Slot.Index = Index;
		Slot.Ring = Rings[Index];
		Slot.AngleDegrees = Angles[Index];
		Slot.Offset = Positions[Index] - Center;
		Slot.WorldLocation = Positions[Index];

		FNavLocation Projected;
		bool bOnNav = !Navigation || Navigation->ProjectPointToNavigation(Positions[Index], Projected, Extent);
		if (Navigation && bOnNav)
		{
			Slot.WorldLocation = Projected.Location;
			bOnNav = !bTargetOnNav || FMath::Abs(Projected.Location.Z - TargetFloor.Location.Z) <= Settings->MaxSlotHeightGap;
		}
		FHitResult Hit;
		const bool bBlocked = Settings->bRequireLineOfSight
			&& World->LineTraceSingleByObjectType(Hit, Center, Positions[Index], FCollisionObjectQueryParams(ECC_WorldStatic), Params);
		Slot.bReachable = bOnNav && !bBlocked;
	}
}

void UPositionalSlotSubsystem::Reassign(FTargetSlots& Entry, float Now)
{
	const UPositionalSlotSettings* Settings = GetDefault<UPositionalSlotSettings>();
	RefreshSlots(Entry);

	FPositionalSlotAssigner::FParams Params;
	Params.Target = Entry.Target->GetActorLocation();
	Params.TargetForward = Entry.Target->GetActorForwardVector();
	Params.HoldTime = Settings->HoldTime;
	Params.Weights.Separation = Settings->SeparationWeight;
	Params.Weights.Flank = Settings->FlankWeight;
	Params.Weights.Distance = Settings->DistanceWeight;
	Params.Weights.DistanceScale = Settings->DistanceScale;
	Params.Weights.CurrentSlotBonus = Settings->CurrentSlotBonus;
	Params.Weights.SwitchThreshold = Settings->SwitchThreshold;
	Params.Weights.Strafe = Settings->StrafeWeight;
	Params.Weights.ShorterWalkReward = Settings->ShorterWalkReward;

	// Each ring is assigned on its own, among the enemies that want it: the assigner sees that
	// ring's slots only, by their index within the ring.
	TArray<int32> Assigned;
	Assigned.Init(INDEX_NONE, Entry.Claims.Num());
	for (int32 Ring = 0; Ring < UPositionalSlotSettings::RingCount; ++Ring)
	{
		TArray<int32> SlotOf; // ring-local index -> slot index
		TArray<FPositionalSlotAssigner::FSlot> Slots;
		for (const FPositionalSlot& Slot : Entry.Slots)
		{
			if (Slot.Ring == Ring)
			{
				SlotOf.Add(Slot.Index);
				FPositionalSlotAssigner::FSlot& Input = Slots.AddDefaulted_GetRef();
				Input.Location = Slot.WorldLocation;
				Input.bReachable = Slot.bReachable;
			}
		}

		TArray<int32> ClaimOf; // claimant index -> claim index
		TArray<FPositionalSlotAssigner::FClaimant> Claimants;
		for (int32 Index = 0; Index < Entry.Claims.Num(); ++Index)
		{
			const FClaim& Claim = Entry.Claims[Index];
			if (Claim.Ring != Ring)
			{
				continue;
			}
			ClaimOf.Add(Index);
			FPositionalSlotAssigner::FClaimant& Claimant = Claimants.AddDefaulted_GetRef();
			Claimant.Id = Claim.Id;
			Claimant.Location = Claim.Enemy->GetActorLocation();
			Claimant.CurrentSlot = SlotOf.Find(Claim.Slot);
			Claimant.HeldFor = Now - Claim.SlotSince;
			Claimant.Intent = Claim.Intent;
		}
		if (Claimants.Num() == 0)
		{
			continue;
		}

		const TArray<FPositionalSlotAssigner::FResult> Results = FPositionalSlotAssigner::Assign(Slots, Claimants, Params);
		for (int32 Index = 0; Index < Results.Num(); ++Index)
		{
			Assigned[ClaimOf[Index]] = SlotOf.IsValidIndex(Results[Index].Slot) ? SlotOf[Results[Index].Slot] : INDEX_NONE;
			Entry.Claims[ClaimOf[Index]].Score = Results[Index].Score;
		}
	}

	for (int32 Index = 0; Index < Entry.Claims.Num(); ++Index)
	{
		FClaim& Claim = Entry.Claims[Index];
		if (Assigned[Index] != Claim.Slot)
		{
			const bool bWasReachable = Entry.Slots.IsValidIndex(Claim.Slot) && Entry.Slots[Claim.Slot].bReachable;
			LogRelease(Entry, Claim, !bWasReachable ? TEXT("slot unreachable")
				: Assigned[Index] == INDEX_NONE ? TEXT("taken by a nearer enemy") : TEXT("moved to a better slot"));
			SlotChanges += Claim.Slot != INDEX_NONE && Assigned[Index] != INDEX_NONE ? 1 : 0;
			Claim.Slot = Assigned[Index];
			Claim.SlotSince = Now;
		}
		// Without a slot it waits out on its own side, clear of the ring, and asks again next round.
		Claim.HoldOffset = Claim.Slot == INDEX_NONE ? FindHoldOffset(Entry.Target.Get(), Claim.Enemy.Get(), Claim.Ring) : FVector::ZeroVector;
	}
	Entry.LastAssign = Now;
	Entry.bDirty = false;
	UpdateOccupancy(Entry);
}

void UPositionalSlotSubsystem::UpdateOccupancy(FTargetSlots& Entry) const
{
	const float Arrival = GetDefault<UPositionalSlotSettings>()->ArrivalRadius;
	const AActor* Target = Entry.Target.Get();
	for (FPositionalSlot& Slot : Entry.Slots)
	{
		const FClaim* Holder = Entry.Claims.FindByPredicate([&Slot](const FClaim& Claim) { return Claim.Slot == Slot.Index; });
		const AActor* Enemy = Holder ? Holder->Enemy.Get() : nullptr;
		Slot.ReservedEnemy = Holder ? Holder->Enemy : nullptr;
		Slot.ReservationTime = Holder ? Holder->SlotSince : 0.0f;
		Slot.Score = Holder ? Holder->Score.Total : 0.0f;
		Slot.bOccupied = Enemy && Target
			&& FVector::Dist2D(Enemy->GetActorLocation(), Target->GetActorLocation() + Slot.Offset) <= Arrival;
	}
}

void UPositionalSlotSubsystem::Tick(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	const float Interval = GetDefault<UPositionalSlotSettings>()->ReassignInterval;

	for (FTargetSlots& Entry : Targets)
	{
		// Enemies that stopped asking (lost the target, fell asleep) give their slot back.
		const int32 Before = Entry.Claims.Num();
		for (const FClaim& Claim : Entry.Claims)
		{
			if (Claim.Enemy.IsValid() && Now - Claim.LastRequest > PositionalSlotClaimLapse)
			{
				LogRelease(Entry, Claim, TEXT("stopped asking (target lost or dormant)"));
			}
		}
		Entry.Claims.RemoveAll([Now](const FClaim& Claim) { return !Claim.Enemy.IsValid() || Now - Claim.LastRequest > PositionalSlotClaimLapse; });
		Entry.bDirty |= Entry.Claims.Num() != Before;
	}
	Targets.RemoveAll([](const FTargetSlots& Entry) { return !Entry.Target.IsValid() || Entry.Claims.Num() == 0; });

	for (FTargetSlots& Entry : Targets)
	{
		if (Entry.bDirty || Now - Entry.LastAssign >= Interval)
		{
			Reassign(Entry, Now);
		}
		else
		{
			UpdateOccupancy(Entry);
		}
		if (ShouldDrawDebug())
		{
			DrawDebug(Entry);
		}
	}

	if (Now < WatchUntil && Now - LastWatchLog >= 0.5f)
	{
		LastWatchLog = Now;
		for (const FTargetSlots& Entry : Targets)
		{
			LogWatch(Entry);
		}
		SlotChanges = 0;
	}
}

void UPositionalSlotSubsystem::Watch(float Seconds)
{
	const UWorld* World = GetWorld();
	WatchUntil = World ? World->GetTimeSeconds() + Seconds : -1.0f;
	LastWatchLog = -1.0f;
	SlotChanges = 0;
}

void UPositionalSlotSubsystem::LogWatch(const FTargetSlots& Entry) const
{
	const AActor* Target = Entry.Target.Get();
	if (!Target)
	{
		return;
	}
	const FVector Center = Target->GetActorLocation();
	int32 WithSlot = 0;
	int32 Arrived = 0;
	float SumOff = 0.0f;
	float MaxOff = 0.0f;
	float NearestTarget = TNumericLimits<float>::Max();
	float NearestPair = TNumericLimits<float>::Max();
	const float Arrival = GetDefault<UPositionalSlotSettings>()->ArrivalRadius;
	for (int32 Index = 0; Index < Entry.Claims.Num(); ++Index)
	{
		const AActor* Enemy = Entry.Claims[Index].Enemy.Get();
		if (!Enemy)
		{
			continue;
		}
		const FVector Location = Enemy->GetActorLocation();
		FVector Stand;
		bool bHasSlot = false;
		if (GetStandPoint(Enemy, Stand, bHasSlot))
		{
			const float Off = FVector::Dist2D(Location, Stand);
			SumOff += Off;
			MaxOff = FMath::Max(MaxOff, Off);
			Arrived += Off <= Arrival ? 1 : 0;
		}
		WithSlot += bHasSlot ? 1 : 0;
		NearestTarget = FMath::Min(NearestTarget, FVector::Dist2D(Location, Center));
		for (int32 Other = Index + 1; Other < Entry.Claims.Num(); ++Other)
		{
			if (const AActor* OtherEnemy = Entry.Claims[Other].Enemy.Get())
			{
				NearestPair = FMath::Min(NearestPair, FVector::Dist2D(Location, OtherEnemy->GetActorLocation()));
			}
		}
	}
	const int32 Count = FMath::Max(Entry.Claims.Num(), 1);
	UE_LOG(LogNeuralAI, Log, TEXT("Watch %s: %d enemy(s), %d with a slot, %d at their place | off by %.0f avg, %.0f max | nearest to target %.0f, nearest pair %.0f | slot changes %d"),
		*GetNameSafe(Target), Entry.Claims.Num(), WithSlot, Arrived, SumOff / Count, MaxOff,
		NearestTarget == TNumericLimits<float>::Max() ? -1.0f : NearestTarget,
		NearestPair == TNumericLimits<float>::Max() ? -1.0f : NearestPair, SlotChanges);
}

void UPositionalSlotSubsystem::DrawDebug(const FTargetSlots& Entry) const
{
	UWorld* World = GetWorld();
	const AActor* Target = Entry.Target.Get();
	if (!World || !Target)
	{
		return;
	}
	const FVector Center = Target->GetActorLocation();
	// The ranged ring only clutters the view until a shooter is after this target.
	const bool bRangedInUse = Entry.Claims.ContainsByPredicate([](const FClaim& Claim) { return Claim.Ring == 1; });
	for (const FPositionalSlot& Slot : Entry.Slots)
	{
		if (Slot.Ring == 1 && !bRangedInUse)
		{
			continue;
		}
		const FVector Location = Center + Slot.Offset;
		const AActor* Holder = Slot.ReservedEnemy.Get();
		const FColor Color = !Slot.bReachable ? FColor(128, 128, 128)
			: Slot.bOccupied ? FColor::Red
			: Holder ? FColor::Yellow
			: FColor::Green;
		DrawDebugSphere(World, Location, 25.0f, 8, Color, false, -1.0f, 0, 2.0f);
		if (Holder)
		{
			DrawDebugLine(World, Holder->GetActorLocation(), Location, Color, false, -1.0f, 0, 1.5f);
		}
		// Number, score and holder above the slot. A zero duration would be dropped before the HUD
		// draws it; a few frames of overlap do no harm.
		const FString Label = Holder
			? FString::Printf(TEXT("#%d %.2f %s"), Slot.Index, Slot.Score, *Holder->GetName())
			: FString::Printf(TEXT("#%d"), Slot.Index);
		DrawDebugString(World, Location + FVector(0.0f, 0.0f, 45.0f), Label, nullptr, Color, 0.05f, true, 1.2f);
	}
	// Enemies without a slot, to where they wait.
	for (const FClaim& Claim : Entry.Claims)
	{
		if (Claim.Slot == INDEX_NONE && Claim.Enemy.IsValid() && !Claim.HoldOffset.IsNearlyZero())
		{
			const FVector Hold = Center + Claim.HoldOffset;
			DrawDebugBox(World, Hold, FVector(15.0f), FColor(160, 160, 160), false, -1.0f, 0, 2.0f);
			DrawDebugLine(World, Claim.Enemy->GetActorLocation(), Hold, FColor(160, 160, 160), false, -1.0f, 0, 1.0f);
		}
	}
}

void UPositionalSlotSubsystem::LogStatus() const
{
	UE_LOG(LogNeuralAI, Log, TEXT("Positional slots: %s, %d target(s)"),
		IsDriving() ? TEXT("driving") : IsAssigning() ? TEXT("assigning only") : TEXT("off"), Targets.Num());
	for (const FTargetSlots& Entry : Targets)
	{
		int32 Unreachable = 0;
		int32 Occupied = 0;
		for (const FPositionalSlot& Slot : Entry.Slots)
		{
			Unreachable += Slot.bReachable ? 0 : 1;
			Occupied += Slot.bOccupied ? 1 : 0;
		}
		const int32 WithSlot = Entry.Claims.FilterByPredicate([](const FClaim& Claim) { return Claim.Slot != INDEX_NONE; }).Num();
		UE_LOG(LogNeuralAI, Log, TEXT("  %s: %d enemy(s), %d with a slot (%d arrived), %d without, %d of %d slot(s) unreachable"),
			*GetNameSafe(Entry.Target.Get()), Entry.Claims.Num(), WithSlot, Occupied, Entry.Claims.Num() - WithSlot,
			Unreachable, Entry.Slots.Num());
		for (const FClaim& Claim : Entry.Claims)
		{
			const bool bHasSlot = Entry.Slots.IsValidIndex(Claim.Slot);
			const float Off = bHasSlot && Entry.Target.IsValid() && Claim.Enemy.IsValid()
				? FVector::Dist2D(Claim.Enemy->GetActorLocation(), Entry.Target->GetActorLocation() + Entry.Slots[Claim.Slot].Offset) : -1.0f;
			const float FromHold = !bHasSlot && Entry.Target.IsValid() && Claim.Enemy.IsValid()
				? FVector::Dist2D(Claim.Enemy->GetActorLocation(), Entry.Target->GetActorLocation() + Claim.HoldOffset) : -1.0f;
			const float FromTarget = Entry.Target.IsValid() && Claim.Enemy.IsValid()
				? FVector::Dist2D(Claim.Enemy->GetActorLocation(), Entry.Target->GetActorLocation()) : -1.0f;
			const FString Where = bHasSlot
				? FString::Printf(TEXT("%sslot %d (%.0f deg), %.0f cm from it, score %.2f = sep %.2f + flank %.2f + dist %.2f + strafe %.2f"),
					Claim.Ring == 1 ? TEXT("ranged ") : TEXT(""), Claim.Slot, Entry.Slots[Claim.Slot].AngleDegrees, Off,
					Claim.Score.Total, Claim.Score.Separation, Claim.Score.Flank, Claim.Score.Distance, Claim.Score.Strafe)
				: FString::Printf(TEXT("no slot, %.0f cm from its hold point"), FromHold);
			UE_LOG(LogNeuralAI, Log, TEXT("    %s: %s, %.0f cm from the target"), *GetNameSafe(Claim.Enemy.Get()), *Where, FromTarget);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNeuralAIPositionalSlotsWatchCommand(TEXT("NeuralAI.PositionalSlots.Watch"),
	TEXT("NeuralAI.PositionalSlots.Watch [seconds]: log twice a second how well the ring holds (default 10 s)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UPositionalSlotSubsystem* Slots = World ? World->GetSubsystem<UPositionalSlotSubsystem>() : nullptr)
		{
			Slots->Watch(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 10.0f);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GNeuralAIPositionalSlotsStatusCommand(TEXT("NeuralAI.PositionalSlots.Status"),
	TEXT("Log every target's positional slots and who holds them."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		if (const UPositionalSlotSubsystem* Slots = World ? World->GetSubsystem<UPositionalSlotSubsystem>() : nullptr)
		{
			Slots->LogStatus();
		}
	}));
