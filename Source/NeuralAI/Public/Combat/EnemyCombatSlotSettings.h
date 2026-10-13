// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EnemyCombatSlotSettings.generated.h"

/**
 * Combat slots around the player: where awake enemies stand and how many may strike at once.
 * Project Settings > Game > Enemy Combat Slots. @see UEnemyCombatSlotSubsystem
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Enemy Combat Slots"))
class NEURALAI_API UEnemyCombatSlotSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** Off: enemies walk straight at the player and strike whenever in range, as before slots. */
	UPROPERTY(Config, EditAnywhere, Category = "Slots")
	bool bEnabled = true;

	/** Slots on the attack ring, evenly spaced. */
	UPROPERTY(Config, EditAnywhere, Category = "Slots", meta = (ClampMin = "1", ClampMax = "16"))
	int32 AttackSlotCount = 8;

	/** Attack ring radius, in cm. Keep it inside the enemies' attack range (220). */
	UPROPERTY(Config, EditAnywhere, Category = "Slots", meta = (ClampMin = "50.0"))
	float AttackRingRadius = 180.0f;

	/** Slots on the wait ring, for enemies that find the attack ring full. */
	UPROPERTY(Config, EditAnywhere, Category = "Slots", meta = (ClampMin = "0", ClampMax = "16"))
	int32 WaitSlotCount = 8;

	UPROPERTY(Config, EditAnywhere, Category = "Slots", meta = (ClampMin = "100.0"))
	float WaitRingRadius = 600.0f;

	/** Enemies allowed to strike at the same time (attack tokens). */
	UPROPERTY(Config, EditAnywhere, Category = "Attack", meta = (ClampMin = "1"))
	int32 MaxAttackers = 3;

	/**
	 * Seconds an attack token is kept after a strike - one enemy's turn - so the next one waits.
	 * With 3 tokens, 2 s holds the ring to about 1.5 strikes a second; at 1 s it hardly slowed
	 * five elites down (3 a second against their own 3.3).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Attack", meta = (ClampMin = "0.1"))
	float AttackTokenHold = 2.0f;

	/** Seconds between reassignments. */
	UPROPERTY(Config, EditAnywhere, Category = "Assignment", meta = (ClampMin = "0.05"))
	float ReassignInterval = 0.5f;

	/** Seconds a slot is kept once given, whatever else comes free. */
	UPROPERTY(Config, EditAnywhere, Category = "Assignment", meta = (ClampMin = "0.0"))
	float HoldTime = 1.0f;

	/** How much cheaper (fraction) a slot must be before an enemy moves to it. */
	UPROPERTY(Config, EditAnywhere, Category = "Assignment", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float SwitchGain = 0.3f;

	/** Distance from its slot, in cm, at which an enemy counts as arrived. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "10.0"))
	float ArrivalRadius = 60.0f;

	/** Share of the brain's strafe still applied once arrived: a little sway, not a walk away. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ArrivedStrafeScale = 0.3f;

	/** Turn rate towards the target while holding a slot, in degrees per second. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (ClampMin = "30.0"))
	float FaceTurnRate = 540.0f;
};
