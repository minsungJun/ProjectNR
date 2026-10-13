// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
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

	/** Enemy is fighting Target and wants a slot. Call every update while it does. */
	void RequestSlot(AActor* Enemy, AActor* Target);

	/** Enemy no longer fights: its slot goes back. */
	void ReleaseSlot(AActor* Enemy);

	/** Where Enemy should stand now. False without a slot. */
	bool GetSlot(const AActor* Enemy, FVector& OutLocation) const;

	/** Logs every target's slots and who holds them. */
	void LogStatus() const;

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
		float SlotSince = 0.0f;
		float LastRequest = 0.0f;
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

	void DrawDebug(const FTargetSlots& Entry) const;

	TArray<FTargetSlots> Targets;
	int32 NextClaimId = 1;
};
