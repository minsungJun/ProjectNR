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

	/** Ring radius, in cm. The enemies' attack range is 220, so at 350 nobody strikes from a slot. */
	UPROPERTY(Config, EditAnywhere, Category = "Slots", meta = (ClampMin = "50.0"))
	float SlotRadius = 350.0f;

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

	/** Spreading out from the other enemies: the smallest angle to their slots over 180 degrees. */
	UPROPERTY(Config, EditAnywhere, Category = "Scoring", meta = (ClampMin = "0.0"))
	float SeparationWeight = 2.0f;

	/** The target's sides and back over its front: 0 in front, 0.5 at the side, 1 behind. Caution raises it. */
	UPROPERTY(Config, EditAnywhere, Category = "Scoring", meta = (ClampMin = "0.0"))
	float FlankWeight = 1.5f;

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
