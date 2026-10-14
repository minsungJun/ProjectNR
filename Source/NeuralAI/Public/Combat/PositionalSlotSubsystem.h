// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Combat/PositionalSlotAssigner.h"
#include "Combat/PositionalSlotTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "PositionalSlotSubsystem.generated.h"

/**
 * Positional slots around each target: where the enemies fighting it stand. Only where - which
 * of them strikes is UAttackTokenSubsystem. @see FPositionalSlotAssigner for how slots are
 * chosen, UPositionalSlotSettings for the numbers.
 *
 * An enemy that senses a target calls RequestSlot every update; one that stops (target lost,
 * dormant) is dropped a moment later, or at once through ReleaseSlot (death, unpossess). A slot
 * is an offset from the target, so GetSlot follows it live; which enemy holds which slot, and
 * which slots are unreachable (off the navmesh, behind a wall from the target), is worked out
 * every ReassignInterval, or straight away when an enemy without a slot asks.
 *
 * Reservation: being assigned a slot reserves it - no one else is given it - and arriving within
 * ArrivalRadius occupies it. An enemy holds one slot at most. It goes back when the enemy dies or
 * is unpossessed, stops asking (target lost, dormant), is moved to a better slot, or the slot
 * becomes unreachable. Enemies beyond the free slots get a hold point NoSlotHoldDistance out on
 * their own side, and ask again every round.
 *
 * Rings: each enemy asks for a slot on one ring - 0 close in (SlotRadius), 1 for shooters
 * (RangedSlotRadius) - and is only ever given slots of that ring, spread among the enemies on it.
 *
 * NeuralAI.PositionalSlots.Mode 0 turns it off, 1 assigns slots without moving anyone (to watch
 * with NeuralAI.PositionalSlots.Debug 1), 2 (default) drives movement.
 */
UCLASS()
class NEURALAI_API UPositionalSlotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Slots are being assigned (settings on, mode 1 or 2). */
	bool IsAssigning() const;

	/** Enemies move to their slots (settings on, mode 2). */
	bool IsDriving() const;

	/**
	 * Enemy is fighting Target and wants a slot. Call every update while it does. Intent is what
	 * its brain wants right now (approach, strafe, caution), which tilts the slot it is given.
	 */
	void RequestSlot(AActor* Enemy, AActor* Target, const FPositionalSlotAssigner::FIntent& Intent = FPositionalSlotAssigner::FIntent(), int32 Ring = 0);

	/** Enemy no longer fights: its slot goes back. Reason is only for the debug log. */
	void ReleaseSlot(AActor* Enemy, const TCHAR* Reason = TEXT("released"));

	/** Where Enemy should stand now. False without a slot. */
	bool GetSlot(const AActor* Enemy, FVector& OutLocation) const;

	/**
	 * Where Enemy should go: its slot, or its hold point when every slot is taken. False when it
	 * has not asked for a slot (or the target is gone). bOutHasSlot tells the two apart.
	 */
	bool GetStandPoint(const AActor* Enemy, FVector& OutLocation, bool& bOutHasSlot) const;

	/** Logs every target's slots and who holds them. */
	void LogStatus() const;

	/** For Seconds, log twice a second how well the ring holds: arrivals, spacing, slot changes. */
	void Watch(float Seconds);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FClaim
	{
		TWeakObjectPtr<AActor> Enemy;
		int32 Id = 0;
		int32 Slot = INDEX_NONE;
		/** Ring it wants a slot on. */
		int32 Ring = 0;
		float SlotSince = 0.0f;
		float LastRequest = 0.0f;
		/** From the target to where it waits while it has no slot. */
		FVector HoldOffset = FVector::ZeroVector;
		FPositionalSlotAssigner::FIntent Intent;
		/** Score of the slot held, as of the last assignment. */
		FPositionalSlotAssigner::FScore Score;
	};

	/** The slots around one target and the enemies after them. */
	struct FTargetSlots
	{
		TWeakObjectPtr<AActor> Target;
		TArray<FPositionalSlot> Slots;
		TArray<FClaim> Claims;
		float LastAssign = -100.0f;
		bool bDirty = true;
	};

	FTargetSlots* FindSlotsOf(const AActor* Enemy, int32* OutClaim = nullptr);
	const FTargetSlots* FindSlotsOf(const AActor* Enemy, int32* OutClaim = nullptr) const;

	/** Rebuilds the slots around the target: positions, navmesh projection, line of sight. */
	void RefreshSlots(FTargetSlots& Entry) const;

	void Reassign(FTargetSlots& Entry, float Now);

	/** Copies who holds what onto the slots, and whether each holder has arrived. */
	void UpdateOccupancy(FTargetSlots& Entry) const;

	/**
	 * Out on the enemy's side of the target, on the navmesh: NoSlotHoldDistance away for the close
	 * ring; for the ranged ring where it is, kept between 60% and 100% of the ring's radius.
	 */
	FVector FindHoldOffset(const AActor* Target, const AActor* Enemy, int32 Ring) const;

	/** With NeuralAI.PositionalSlots.Debug, logs a slot going back and why. */
	void LogRelease(const FTargetSlots& Entry, const FClaim& Claim, const TCHAR* Reason) const;

	void DrawDebug(const FTargetSlots& Entry) const;

	void LogWatch(const FTargetSlots& Entry) const;

	TArray<FTargetSlots> Targets;
	int32 NextClaimId = 1;

	/** Watch: until when, the last line, and slot changes (an enemy moving from one slot to another) since. */
	float WatchUntil = -1.0f;
	float LastWatchLog = -1.0f;
	mutable int32 SlotChanges = 0;
};
