// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Core/NeuralTypes.h"
#include "Engine/DataAsset.h"
#include "NeuralBrainDataAsset.generated.h"

/** A field of FEnemySensorData, so input wiring can be described as data. */
UENUM(BlueprintType)
enum class ESensorChannel : uint8
{
	TargetDetected,
	TargetNear,
	TargetFront,
	TargetLeft,
	TargetRight,
	TargetClosingSpeed,
	ProjectileThreatFront,
	ProjectileThreatLeft,
	ProjectileThreatRight,
	Pain,
	LowHealth
};

/** A field of FNeuralBrainOutput. */
UENUM(BlueprintType)
enum class EMotorChannel : uint8
{
	Approach,
	Retreat,
	StrafeLeft,
	StrafeRight,
	Attack,
	DodgeLeft,
	DodgeRight
};

/** Which aspect of behaviour an adaptation modifier acts on. */
UENUM(BlueprintType)
enum class EAdaptationAxis : uint8
{
	Projectile,
	CloseRange,
	Mobility,
	Aggression
};

/** Routes one sensor reading onto one sensory neuron. */
USTRUCT(BlueprintType)
struct NEURALAI_API FNeuralInputMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	ESensorChannel Channel = ESensorChannel::TargetNear;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	ENeuronId Neuron = ENeuronId::PlayerNear;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	float Scale = 1.0f;

	/**
	 * Multiplies the reading by TargetDetected.
	 *
	 * Set on the player-bearing channels so losing sight silences them together instead
	 * of leaving a stale direction behind. Deliberately off for projectile threat: being
	 * shot from behind still matters.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	bool bGateOnTargetDetected = false;
};

/** Routes one motor neuron onto one output field. */
USTRUCT(BlueprintType)
struct NEURALAI_API FNeuralOutputMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	ENeuronId Neuron = ENeuronId::Approach;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	EMotorChannel Channel = EMotorChannel::Approach;
};

/**
 * Marks a synapse as something a future evolution system may tune.
 *
 * Identifying synapses by their endpoints rather than by array index means the list
 * survives reordering of the synapse array, which an index would not.
 */
USTRUCT(BlueprintType)
struct NEURALAI_API FAdaptableSynapse
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation")
	ENeuronId FromNeuron = ENeuronId::ProjectileLeft;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation")
	ENeuronId ToNeuron = ENeuronId::LoomLeft;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation")
	EAdaptationAxis Axis = EAdaptationAxis::Projectile;

	/**
	 * How strongly this synapse follows its axis.
	 *
	 * 1 means a modifier of 1.5 scales the weight by 1.5; 0.5 scales it by 1.25. Lets one
	 * axis move a whole cluster of synapses by different amounts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Sensitivity = 1.0f;
};

/**
 * A complete neural circuit as data.
 *
 * The brain falls back to its built-in circuit when no asset is assigned, so this is an
 * override rather than a requirement: the hardcoded version stays the reference, and an
 * asset is how a variant gets tried without a recompile.
 */
UCLASS(BlueprintType)
class NEURALAI_API UNeuralBrainDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** One entry per ENeuronId, in enum order. A short array leaves the rest at defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Neurons")
	TArray<FNeuronDefinition> NeuronDefinitions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Synapses")
	TArray<FNeuralSynapse> Synapses;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	TArray<FNeuralInputMapping> InputMappings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping")
	TArray<FNeuralOutputMapping> OutputMappings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation")
	TArray<FAdaptableSynapse> AdaptableSynapses;
};
