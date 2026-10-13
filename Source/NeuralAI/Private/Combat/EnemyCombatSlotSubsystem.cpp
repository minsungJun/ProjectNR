// Neural Enemy AI prototype module.

#include "Combat/EnemyCombatSlotSubsystem.h"

#include "Combat/EnemyCombatSlotSettings.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "NeuralAI.h"

static TAutoConsoleVariable<int32> CVarCombatSlotMode(TEXT("NeuralAI.Slots.Mode"), 2,
	TEXT("0: no combat slots. 1: assign slots but move and attack as before (to watch). 2: enemies go to their slots and need a token to strike."));

static TAutoConsoleVariable<int32> CVarCombatSlotDebug(TEXT("NeuralAI.Slots.Debug"), 0,
	TEXT("1: draw the slots (green free, orange held, red blocked, blue wait ring) and log attack tokens."));

namespace
{
	/** Seconds without a RequestSlot before a claim lapses. */
	constexpr float CombatSlotClaimLapse = 0.3f;

	/** Seconds without asking again before an enemy stops counting as waiting for a token. */
	constexpr float CombatSlotWaitLapse = 0.5f;
}

bool UEnemyCombatSlotSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UEnemyCombatSlotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEnemyCombatSlotSubsystem, STATGROUP_Tickables);
}

bool UEnemyCombatSlotSubsystem::IsAssigning() const
{
	return GetDefault<UEnemyCombatSlotSettings>()->bEnabled && CVarCombatSlotMode.GetValueOnGameThread() >= 1;
}

bool UEnemyCombatSlotSubsystem::IsDriving() const
{
	return GetDefault<UEnemyCombatSlotSettings>()->bEnabled && CVarCombatSlotMode.GetValueOnGameThread() >= 2;
}

UEnemyCombatSlotSubsystem::FRing* UEnemyCombatSlotSubsystem::FindRingOf(const AActor* Enemy, int32* OutClaim)
{
	return const_cast<FRing*>(static_cast<const UEnemyCombatSlotSubsystem*>(this)->FindRingOf(Enemy, OutClaim));
}

const UEnemyCombatSlotSubsystem::FRing* UEnemyCombatSlotSubsystem::FindRingOf(const AActor* Enemy, int32* OutClaim) const
{
	for (const FRing& Ring : Rings)
	{
		const int32 Claim = Ring.Claims.IndexOfByPredicate([Enemy](const FClaim& Entry) { return Entry.Enemy.Get() == Enemy; });
		if (Claim != INDEX_NONE)
		{
			if (OutClaim)
			{
				*OutClaim = Claim;
			}
			return &Ring;
		}
	}
	return nullptr;
}

void UEnemyCombatSlotSubsystem::RequestSlot(AActor* Enemy, AActor* Target)
{
	const UWorld* World = GetWorld();
	if (!World || !Enemy || !Target)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	int32 ClaimIndex = INDEX_NONE;
	if (FRing* Current = FindRingOf(Enemy, &ClaimIndex))
	{
		if (Current->Target.Get() == Target)
		{
			Current->Claims[ClaimIndex].LastRequest = Now;
			return;
		}
		// Switched targets: leave the old ring.
		ReleaseSlot(Enemy);
	}

	FRing* Ring = Rings.FindByPredicate([Target](const FRing& Entry) { return Entry.Target.Get() == Target; });
	if (!Ring)
	{
		Ring = &Rings.AddDefaulted_GetRef();
		Ring->Target = Target;
	}
	FClaim& Claim = Ring->Claims.AddDefaulted_GetRef();
	Claim.Enemy = Enemy;
	Claim.Id = NextClaimId++;
	Claim.LastRequest = Now;
	// A newcomer should not wait for the next round to know where to go.
	Ring->bDirty = true;
}

void UEnemyCombatSlotSubsystem::ReleaseSlot(AActor* Enemy)
{
	for (FRing& Ring : Rings)
	{
		Ring.Claims.RemoveAll([Enemy](const FClaim& Claim) { return Claim.Enemy.Get() == Enemy; });
		Ring.Tokens.RemoveAll([Enemy](const FToken& Token) { return Token.Holder.Get() == Enemy; });
		Ring.Waiting.RemoveAll([Enemy](const FWaiting& Entry) { return Entry.Enemy.Get() == Enemy; });
	}
}

bool UEnemyCombatSlotSubsystem::GetSlot(const AActor* Enemy, FVector& OutLocation, bool& bOutAttackRing) const
{
	int32 ClaimIndex = INDEX_NONE;
	const FRing* Ring = FindRingOf(Enemy, &ClaimIndex);
	const AActor* Target = Ring ? Ring->Target.Get() : nullptr;
	if (!Target)
	{
		return false;
	}
	const int32 Slot = Ring->Claims[ClaimIndex].Slot;
	if (!Ring->Offsets.IsValidIndex(Slot))
	{
		return false;
	}
	OutLocation = Target->GetActorLocation() + Ring->Offsets[Slot];
	bOutAttackRing = Ring->Slots[Slot].Ring == 0;
	return true;
}

bool UEnemyCombatSlotSubsystem::TryTakeAttackToken(AActor* Enemy)
{
	const UWorld* World = GetWorld();
	FRing* Ring = World ? FindRingOf(Enemy) : nullptr;
	if (!Ring)
	{
		return false;
	}
	const float Now = World->GetTimeSeconds();
	const UEnemyCombatSlotSettings* Settings = GetDefault<UEnemyCombatSlotSettings>();

	Ring->Tokens.RemoveAll([Now](const FToken& Token) { return !Token.Holder.IsValid() || Token.Until <= Now; });
	Ring->Waiting.RemoveAll([Now](const FWaiting& Entry) { return !Entry.Enemy.IsValid() || Now - Entry.LastAsked > CombatSlotWaitLapse; });

	FWaiting* Mine = Ring->Waiting.FindByPredicate([Enemy](const FWaiting& Entry) { return Entry.Enemy.Get() == Enemy; });
	if (!Mine)
	{
		Mine = &Ring->Waiting.AddDefaulted_GetRef();
		Mine->Enemy = Enemy;
		Mine->Since = Now;
	}
	Mine->LastAsked = Now;
	const float MySince = Mine->Since;

	// Full, or someone has been kept waiting longer: not yet.
	const bool bFull = Ring->Tokens.Num() >= Settings->MaxAttackers;
	const bool bQueued = Ring->Waiting.ContainsByPredicate([Enemy, MySince](const FWaiting& Entry)
	{
		return Entry.Enemy.Get() != Enemy && Entry.Since < MySince;
	});
	if (bFull || bQueued)
	{
		return false;
	}

	Ring->Waiting.RemoveAll([Enemy](const FWaiting& Entry) { return Entry.Enemy.Get() == Enemy; });
	FToken& Token = Ring->Tokens.AddDefaulted_GetRef();
	Token.Holder = Enemy;
	Token.Until = Now + Settings->AttackTokenHold;
	if (CVarCombatSlotDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogNeuralAI, Log, TEXT("%s takes an attack token (%d/%d out, %d waiting)"), *GetNameSafe(Enemy),
			Ring->Tokens.Num(), Settings->MaxAttackers, Ring->Waiting.Num());
	}
	return true;
}

void UEnemyCombatSlotSubsystem::RefreshSlots(FRing& Ring) const
{
	const UEnemyCombatSlotSettings* Settings = GetDefault<UEnemyCombatSlotSettings>();
	const AActor* Target = Ring.Target.Get();
	UWorld* World = GetWorld();
	if (!Target || !World)
	{
		return;
	}
	const FVector Center = Target->GetActorLocation();

	Ring.Slots.Reset();
	Ring.Offsets.Reset();
	for (const int32 RingIndex : { 0, 1 })
	{
		const int32 Count = RingIndex == 0 ? Settings->AttackSlotCount : Settings->WaitSlotCount;
		const float Radius = RingIndex == 0 ? Settings->AttackRingRadius : Settings->WaitRingRadius;
		for (const FVector& Position : FEnemySlotAssigner::RingPositions(Center, Count, Radius))
		{
			FEnemySlotAssigner::FSlot& Slot = Ring.Slots.AddDefaulted_GetRef();
			Slot.Location = Position;
			Slot.Ring = RingIndex;
			Ring.Offsets.Add(Position - Center);
		}
	}

	// A slot must stand on the navmesh and be in plain sight of the target: through a wall it
	// could be reached only the long way round, if at all.
	const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyCombatSlot), false, Target);
	for (FEnemySlotAssigner::FSlot& Slot : Ring.Slots)
	{
		FNavLocation Projected;
		const bool bOnNav = !Navigation || Navigation->ProjectPointToNavigation(Slot.Location, Projected, FVector(50.0f, 50.0f, 250.0f));
		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByObjectType(Hit, Center, Slot.Location, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
		Slot.bUsable = bOnNav && !bBlocked;
	}
}

void UEnemyCombatSlotSubsystem::Reassign(FRing& Ring, float Now)
{
	const UEnemyCombatSlotSettings* Settings = GetDefault<UEnemyCombatSlotSettings>();
	RefreshSlots(Ring);

	TArray<FEnemySlotAssigner::FClaimant> Claimants;
	for (const FClaim& Claim : Ring.Claims)
	{
		FEnemySlotAssigner::FClaimant& Claimant = Claimants.AddDefaulted_GetRef();
		Claimant.Id = Claim.Id;
		Claimant.Location = Claim.Enemy->GetActorLocation();
		Claimant.CurrentSlot = Claim.Slot;
		Claimant.HeldFor = Now - Claim.SlotSince;
	}

	FEnemySlotAssigner::FParams Params;
	Params.Target = Ring.Target->GetActorLocation();
	Params.HoldTime = Settings->HoldTime;
	Params.SwitchGain = Settings->SwitchGain;
	const TArray<int32> Assigned = FEnemySlotAssigner::Assign(Ring.Slots, Claimants, Params);

	for (int32 Index = 0; Index < Ring.Claims.Num(); ++Index)
	{
		FClaim& Claim = Ring.Claims[Index];
		if (Assigned[Index] != Claim.Slot)
		{
			Claim.Slot = Assigned[Index];
			Claim.SlotSince = Now;
		}
	}
	Ring.LastAssign = Now;
	Ring.bDirty = false;
}

void UEnemyCombatSlotSubsystem::Tick(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	const float Interval = GetDefault<UEnemyCombatSlotSettings>()->ReassignInterval;

	for (FRing& Ring : Rings)
	{
		// Enemies that stopped asking (lost the target, fell asleep, died) give their slot back.
		const int32 Before = Ring.Claims.Num();
		Ring.Claims.RemoveAll([Now](const FClaim& Claim) { return !Claim.Enemy.IsValid() || Now - Claim.LastRequest > CombatSlotClaimLapse; });
		Ring.bDirty |= Ring.Claims.Num() != Before;
	}
	Rings.RemoveAll([](const FRing& Ring) { return !Ring.Target.IsValid() || Ring.Claims.Num() == 0; });

	for (FRing& Ring : Rings)
	{
		if (Ring.bDirty || Now - Ring.LastAssign >= Interval)
		{
			Reassign(Ring, Now);
		}
		if (CVarCombatSlotDebug.GetValueOnGameThread() != 0)
		{
			DrawDebug(Ring);
		}
	}
}

void UEnemyCombatSlotSubsystem::DrawDebug(const FRing& Ring) const
{
	UWorld* World = GetWorld();
	const AActor* Target = Ring.Target.Get();
	if (!World || !Target)
	{
		return;
	}
	const FVector Center = Target->GetActorLocation();
	for (int32 Index = 0; Index < Ring.Slots.Num(); ++Index)
	{
		const FClaim* Owner = Ring.Claims.FindByPredicate([Index](const FClaim& Claim) { return Claim.Slot == Index; });
		const FVector Location = Center + Ring.Offsets[Index];
		const FColor Color = !Ring.Slots[Index].bUsable ? FColor::Red
			: Owner ? FColor::Orange
			: Ring.Slots[Index].Ring == 0 ? FColor::Green : FColor(80, 120, 255);
		DrawDebugSphere(World, Location, 25.0f, 8, Color, false, -1.0f, 0, 2.0f);
		if (Owner && Owner->Enemy.IsValid())
		{
			const bool bAttacking = Ring.Tokens.ContainsByPredicate([Owner](const FToken& Token) { return Token.Holder == Owner->Enemy; });
			DrawDebugLine(World, Owner->Enemy->GetActorLocation(), Location, bAttacking ? FColor::Red : FColor::Orange, false, -1.0f, 0, 1.5f);
		}
	}
}

void UEnemyCombatSlotSubsystem::LogStatus() const
{
	const UEnemyCombatSlotSettings* Settings = GetDefault<UEnemyCombatSlotSettings>();
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	UE_LOG(LogNeuralAI, Log, TEXT("Combat slots: %s, %d target(s)"),
		IsDriving() ? TEXT("driving") : IsAssigning() ? TEXT("assigning only") : TEXT("off"), Rings.Num());
	for (const FRing& Ring : Rings)
	{
		int32 Blocked = 0;
		int32 InAttack = 0;
		int32 InWait = 0;
		for (const FEnemySlotAssigner::FSlot& Slot : Ring.Slots)
		{
			Blocked += Slot.bUsable ? 0 : 1;
		}
		TArray<FString> Lines;
		for (const FClaim& Claim : Ring.Claims)
		{
			const bool bHasSlot = Ring.Slots.IsValidIndex(Claim.Slot);
			const bool bAttack = bHasSlot && Ring.Slots[Claim.Slot].Ring == 0;
			InAttack += bAttack ? 1 : 0;
			InWait += bHasSlot && !bAttack ? 1 : 0;
			const float Off = bHasSlot && Ring.Target.IsValid() && Claim.Enemy.IsValid()
				? FVector::Dist2D(Claim.Enemy->GetActorLocation(), Ring.Target->GetActorLocation() + Ring.Offsets[Claim.Slot]) : -1.0f;
			Lines.Add(FString::Printf(TEXT("%s: %s%d, %.0f cm from it"), *GetNameSafe(Claim.Enemy.Get()),
				!bHasSlot ? TEXT("no slot ") : bAttack ? TEXT("attack ") : TEXT("wait "), Claim.Slot, Off));
		}
		const int32 TokensOut = Ring.Tokens.FilterByPredicate([Now](const FToken& Token) { return Token.Until > Now; }).Num();
		UE_LOG(LogNeuralAI, Log, TEXT("  %s: %d enemy(s), %d on the attack ring, %d waiting, %d slot(s) blocked, tokens %d/%d"),
			*GetNameSafe(Ring.Target.Get()), Ring.Claims.Num(), InAttack, InWait, Blocked, TokensOut, Settings->MaxAttackers);
		for (const FString& Line : Lines)
		{
			UE_LOG(LogNeuralAI, Log, TEXT("    %s"), *Line);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNeuralAISlotsStatusCommand(TEXT("NeuralAI.Slots.Status"),
	TEXT("Log every target's combat slots, who holds them and the attack tokens out."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		if (const UEnemyCombatSlotSubsystem* Slots = World ? World->GetSubsystem<UEnemyCombatSlotSubsystem>() : nullptr)
		{
			Slots->LogStatus();
		}
	}));
