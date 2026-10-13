// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Combat/EnemySlotAssigner.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyCombatSlotSubsystem.generated.h"

/**
 * Combat slots around each target: where the enemies fighting it stand, and which of them may
 * strike. @see FEnemySlotAssigner for how slots are chosen, UEnemyCombatSlotSettings for the
 * numbers.
 *
 * An enemy that senses a target calls RequestSlot every update; one that stops (target lost,
 * dormant, dead) is dropped a moment later, or at once through ReleaseSlot. Slots are offsets
 * from the target, so GetSlot follows it live; which enemy holds which slot, and which slots are
 * blocked (off the navmesh, behind a wall from the target), is worked out every
 * ReassignInterval, or straight away when an enemy without a slot asks.
 *
 * Attack tokens: an enemy standing in an attack slot that wants to strike asks TryTakeAttackToken.
 * At most MaxAttackers hold one at a time, each for AttackTokenHold seconds; among those kept
 * waiting, the one waiting longest goes next.
 *
 * NeuralAI.Slots.Mode 0 turns it off, 1 assigns slots without moving anyone (to watch with
 * NeuralAI.Slots.Debug 1), 2 (default) drives movement and attacks.
 */
UCLASS()
class NEURALAI_API UEnemyCombatSlotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Slots are being assigned (settings on, mode 1 or 2). */
	bool IsAssigning() const;

	/** Enemies move to their slots and need tokens to strike (settings on, mode 2). */
	bool IsDriving() const;

	/** Enemy is fighting Target and wants a slot. Call every update while it does. */
	void RequestSlot(AActor* Enemy, AActor* Target);

	/** Enemy no longer fights: its slot and any token go back. */
	void ReleaseSlot(AActor* Enemy);

	/** Where Enemy should stand now, and whether that is on the attack ring. False without a slot. */
	bool GetSlot(const AActor* Enemy, FVector& OutLocation, bool& bOutAttackRing) const;

	/**
	 * Enemy, in its attack slot, wants to strike. True - and a token held for AttackTokenHold -
	 * when fewer than MaxAttackers hold one and no enemy has been kept waiting longer.
	 */
	bool TryTakeAttackToken(AActor* Enemy);

	/** Logs every target's slots, who holds them and the tokens out. */
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

	struct FToken
	{
		TWeakObjectPtr<AActor> Holder;
		float Until = 0.0f;
	};

	struct FWaiting
	{
		TWeakObjectPtr<AActor> Enemy;
		float Since = 0.0f;
		float LastAsked = 0.0f;
	};

	struct FRing
	{
		TWeakObjectPtr<AActor> Target;
		/** Offset of each slot from the target, and whether it is usable, as of the last assignment. */
		TArray<FEnemySlotAssigner::FSlot> Slots;
		TArray<FVector> Offsets;
		TArray<FClaim> Claims;
		TArray<FToken> Tokens;
		TArray<FWaiting> Waiting;
		float LastAssign = -100.0f;
		bool bDirty = true;
	};

	FRing* FindRingOf(const AActor* Enemy, int32* OutClaim = nullptr);
	const FRing* FindRingOf(const AActor* Enemy, int32* OutClaim = nullptr) const;

	/** Recomputes slot offsets and usability around the ring's target. */
	void RefreshSlots(FRing& Ring) const;

	void Reassign(FRing& Ring, float Now);

	void DrawDebug(const FRing& Ring) const;

	TArray<FRing> Rings;
	int32 NextClaimId = 1;
};
