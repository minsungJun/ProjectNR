// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PositionalSlotSettings.generated.h"

/**
 * Positional slots around the player: where awake enemies stand. Only position - which enemy
 * strikes, and when, is UAttackTokenSettings. Project Settings > Game > Positional Slots.
 * @see UPositionalSlotSubsystem
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Positional Slots"))
class NEURALAI_API UPositionalSlotSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** Off: enemies walk straight at the player, as before slots. */
	UPROPERTY(Config, EditAnywhere, Category = "Slots")
	bool bEnabled = true;

	/** Slots on the ring, evenly spaced. */
	UPROPERTY(Config, EditAnywhere, Category = "Slots", meta = (ClampMin = "1", ClampMax = "32"))
	int32 SlotCount = 8;

	/**
	 * Ring radius, in cm. Keep it inside the enemies' attack range (220) less the arrival radius:
	 * an enemy stops up to ArrivalRadius short of its slot, on the outside, and must still be in
	 * reach. At 160 they stand about 210 cm off. Was 350 until 2026-10-14, where nobody struck.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Slots", meta = (ClampMin = "50.0"))
	float SlotRadius = 160.0f;

	/** Slots on the ranged ring (ring 1), for enemies that shoot. Set half a slot round from the close ring. */
	UPROPERTY(Config, EditAnywhere, Category = "Ranged Ring", meta = (ClampMin = "1", ClampMax = "32"))
	int32 RangedSlotCount = 8;

	/**
	 * Ranged ring radius, in cm. Inside the shooters' range (30 m) and inside a room (18 m from
	 * the middle to a wall); slots off the navmesh or out of sight of the target drop out as usual.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Ranged Ring", meta = (ClampMin = "100.0"))
	float RangedSlotRadius = 1200.0f;

	/** Rings there are: 0 close in, 1 ranged. */
	static constexpr int32 RingCount = 2;

	int32 GetRingSlotCount(int32 Ring) const { return Ring == 1 ? RangedSlotCount : SlotCount; }
	float GetRingRadius(int32 Ring) const { return Ring == 1 ? RangedSlotRadius : SlotRadius; }

	/** How far up or down, in cm, a slot may be moved to land on the navmesh. */
	UPROPERTY(Config, EditAnywhere, Category = "Slots", meta = (ClampMin = "10.0"))
	float NavProjectionHeight = 250.0f;

	/** Also drop slots the target cannot see straight to (behind a wall or pillar). */
	UPROPERTY(Config, EditAnywhere, Category = "Slots")
	bool bRequireLineOfSight = true;

	/** Seconds between reassignments. A newcomer is placed straight away. */
	UPROPERTY(Config, EditAnywhere, Category = "Assignment", meta = (ClampMin = "0.05"))
	float ReassignInterval = 0.5f;

	/** Seconds a slot is kept once given, whatever else comes free. */
	UPROPERTY(Config, EditAnywhere, Category = "Assignment", meta = (ClampMin = "0.0"))
	float HoldTime = 1.0f;

	/** Added to the score of the slot an enemy already holds, so it does not hop for a hair's gain. */
	UPROPERTY(Config, EditAnywhere, Category = "Assignment", meta = (ClampMin = "0.0"))
	float CurrentSlotBonus = 0.2f;

	/** How much more than its own slot (bonus included) a new one must score before the enemy moves. */
	UPROPERTY(Config, EditAnywhere, Category = "Assignment", meta = (ClampMin = "0.0"))
	float SwitchThreshold = 0.1f;

	/**
	 * Once an enemy holds a slot, a nearer one earns back only this share of the walk it saves (a
	 * further one costs all of the extra walk). Keeps an enemy on its way round the far side from
	 * dropping its slot for whatever free slot it passes.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Assignment", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ShorterWalkReward = 0.25f;

	/**
	 * Spreading out from the other enemies: the smallest angle to their slots over 180 degrees.
	 * 0 since 2026-10-14 (was 2.0): with Flank also 0, each enemy takes the nearest free slot
	 * (by the walk there), nearest enemy first. Raise both again to have them surround the target.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Scoring", meta = (ClampMin = "0.0"))
	float SeparationWeight = 0.0f;

	/**
	 * The target's sides and back over its front: 0 in front, 0.5 at the side, 1 behind. Caution
	 * raises it. 0 since 2026-10-14 (was 1.5).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Scoring", meta = (ClampMin = "0.0"))
	float FlankWeight = 0.0f;

	/** Minus the walk to the slot over DistanceScale. Approach raises it. */
	UPROPERTY(Config, EditAnywhere, Category = "Scoring", meta = (ClampMin = "0.0"))
	float DistanceWeight = 1.0f;

	/** Walk, in cm, that costs one point of distance score. */
	UPROPERTY(Config, EditAnywhere, Category = "Scoring", meta = (ClampMin = "100.0"))
	float DistanceScale = 1000.0f;

	/** Bonus for the neighbouring slot on the side the enemy is strafing to, at full strafe. */
	UPROPERTY(Config, EditAnywhere, Category = "Scoring", meta = (ClampMin = "0.0"))
	float StrafeWeight = 0.5f;

	/** Enemies left without a slot wait this far from the target, on their own side, in cm. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "100.0"))
	float NoSlotHoldDistance = 650.0f;

	/** Distance from its slot, in cm, at which an enemy counts as arrived. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "10.0"))
	float ArrivalRadius = 60.0f;

	/**
	 * Seconds between route updates to a slot are the controller's PathUpdateInterval; on top of
	 * that the route is only rebuilt once the slot has moved this far (cm) since the last one, so a
	 * target standing still or shuffling costs nothing.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0"))
	float RepathDistance = 50.0f;

	/** Seconds after which the route to a slot is rebuilt even if the slot has hardly moved. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "0.1"))
	float MaxPathAge = 1.0f;

	/**
	 * An enemy comes in to its slot straight only once its bearing from the target is within this
	 * many degrees of the slot's, between the neighbouring slots (45 degrees off at 8 slots).
	 * Further round, it first circles outside the ring: the navmesh knows neither the target nor
	 * the enemies already standing round it.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float OrbitAlignDegrees = 20.0f;

	/** How far outside SlotRadius, in cm, an enemy circles: clear of those standing in their slots. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0"))
	float OrbitMargin = 150.0f;

	/** How far round the ring, in degrees, each step of going round aims. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "10.0", ClampMax = "90.0"))
	float OrbitStepDegrees = 60.0f;

	/** Share of the brain's strafe still applied once arrived: a little sway, not a walk away. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ArrivedStrafeScale = 0.3f;

	/** Turn rate towards the target while holding a slot, in degrees per second. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "30.0"))
	float FaceTurnRate = 540.0f;

	/** Draw the slots, who holds them and their scores. Same as NeuralAI.PositionalSlots.Debug 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bDrawDebug = false;
};
