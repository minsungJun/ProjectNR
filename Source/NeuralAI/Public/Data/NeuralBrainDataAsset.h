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
 * A synapse an adaptation axis changes.
 *
 * Identified by its endpoints rather than by array index, so the list survives reordering of
 * the synapse array. With the axis at value v (-1..1) the synapse's weight becomes
 *
 *     Weight * max(0, 1 + v * Sensitivity) + v * AddedWeight
 *
 * Sensitivity scales a synapse the circuit already has; AddedWeight can also create one it
 * does not (the strafe wiring starts from nothing). A synapse onto StrafeRight is mirrored
 * onto StrafeLeft while the enemy circles the other way.
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
	EAdaptationAxis Axis = EAdaptationAxis::Dodge;

	/** Relative weight change per unit of the axis: 0.5 makes v = 1 scale the weight by 1.5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation", meta = (ClampMin = "-2.0", ClampMax = "2.0"))
	float Sensitivity = 0.0f;

	/** Weight added per unit of the axis, whether or not the circuit has this synapse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation", meta = (ClampMin = "-2.0", ClampMax = "2.0"))
	float AddedWeight = 0.0f;

	FAdaptableSynapse() = default;
	FAdaptableSynapse(ENeuronId InFrom, ENeuronId InTo, EAdaptationAxis InAxis, float InSensitivity, float InAddedWeight = 0.0f)
		: FromNeuron(InFrom), ToNeuron(InTo), Axis(InAxis), Sensitivity(InSensitivity), AddedWeight(InAddedWeight) {}
};

/** Resting drive an adaptation axis adds to a neuron: v * BiasPerUnit. Mirrored like FAdaptableSynapse. */
USTRUCT(BlueprintType)
struct NEURALAI_API FAdaptableBias
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation")
	ENeuronId Neuron = ENeuronId::Attack;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation")
	EAdaptationAxis Axis = EAdaptationAxis::Aggression;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float BiasPerUnit = 0.0f;

	FAdaptableBias() = default;
	FAdaptableBias(ENeuronId InNeuron, EAdaptationAxis InAxis, float InBiasPerUnit)
		: Neuron(InNeuron), Axis(InAxis), BiasPerUnit(InBiasPerUnit) {}
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

	/** Replaces the built-in adaptation wiring when not empty (together with AdaptableBiases). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation")
	TArray<FAdaptableSynapse> AdaptableSynapses;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Adaptation")
	TArray<FAdaptableBias> AdaptableBiases;
};
