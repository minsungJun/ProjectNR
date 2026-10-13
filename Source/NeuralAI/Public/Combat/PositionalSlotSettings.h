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
