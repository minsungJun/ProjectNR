// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyAIProfile.generated.h"

/**
 * Everything about one enemy archetype that is a number rather than a wiring decision.
 *
 * Tuning these used to mean editing a constructor and closing the editor. As a data asset
 * they can be changed between play sessions, and a second archetype is a second asset
 * rather than a second class.
 *
 * The neural circuit itself is not here: that lives in UNeuralBrainDataAsset, because a
 * weight between two neurons is a structural choice, not a stat.
 */
UCLASS(BlueprintType)
class NEURALAI_API UEnemyAIProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Perception

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "100.0"))
	float DetectionRange = 2000.0f;

	/**
	 * Range the enemy would rather fight at.
	 *
	 * Not yet consumed: closing all the way is still the emergent result of Approach with
	 * nothing opposing it at short range. Recorded here so the value has one home when a
	 * spacing circuit is added.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float PreferredDistance = 250.0f;

	// --- Movement

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "1.0"))
	float MovementSpeed = 400.0f;

	// --- Action gating

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AttackThreshold = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DodgeThreshold = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action", meta = (ClampMin = "0.0"))
	float AttackCommitTime = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action", meta = (ClampMin = "0.0"))
	float DodgeCommitTime = 0.3f;

	// --- Temperament
	//
	// Applied as additive bias on the matching neurons, so a timid archetype is the same
	// circuit with a different resting state rather than different wiring.

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Temperament", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float AggressionBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Temperament", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float ThreatBias = 0.0f;

	/** Multiplies the Pain spike a hit produces. Above 1 makes the enemy flinch harder. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Temperament", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float PainSensitivity = 1.0f;

	// --- Simulation

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float BrainTickRate = 20.0f;
};
