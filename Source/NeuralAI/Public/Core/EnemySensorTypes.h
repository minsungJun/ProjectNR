// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "EnemySensorTypes.generated.h"

/**
 * Everything the enemy knows about the world, as normalized scalars.
 *
 * This struct is the entire contract between the world and the brain. The sensor
 * writes it; the brain reads it and nothing else. Keeping every field in 0..1 means
 * neuron inputs need no per-field scaling, so a synapse weight means the same thing
 * no matter which sensory neuron it feeds.
 *
 * Directional fields are ego-centric: Front/Left/Right are relative to the enemy's
 * own facing, not to world axes.
 */
USTRUCT(BlueprintType)
struct NEURALAI_API FEnemySensorData
{
	GENERATED_BODY()

	/** 1 while a target is within range and in line of sight, otherwise 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Sensor|Target")
	float TargetDetected = 0.0f;

	/** 1 when the target is on top of us, falling to 0 at the detection range. */
	UPROPERTY(BlueprintReadOnly, Category = "Sensor|Target")
	float TargetNear = 0.0f;

	/** How much the target lies along our forward axis. */
	UPROPERTY(BlueprintReadOnly, Category = "Sensor|Target")
	float TargetFront = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Sensor|Target")
	float TargetLeft = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Sensor|Target")
	float TargetRight = 0.0f;

	/** 0.5 when the gap is constant, above when the target closes, below when it retreats. */
	UPROPERTY(BlueprintReadOnly, Category = "Sensor|Target")
	float TargetClosingSpeed = 0.5f;

	/** Strongest incoming-projectile threat from each side. Filled in Phase 4. */
	UPROPERTY(BlueprintReadOnly, Category = "Sensor|Projectile")
	float ProjectileThreatFront = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Sensor|Projectile")
	float ProjectileThreatLeft = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Sensor|Projectile")
	float ProjectileThreatRight = 0.0f;

	/** Spikes to 1 on damage and decays. Short-lived; long-term wariness is the brain's job. */
	UPROPERTY(BlueprintReadOnly, Category = "Sensor|Self")
	float Pain = 0.0f;

	/** 0 at full health, 1 at death's door. */
	UPROPERTY(BlueprintReadOnly, Category = "Sensor|Self")
	float LowHealth = 0.0f;

	void Reset()
	{
		*this = FEnemySensorData();
	}
};
