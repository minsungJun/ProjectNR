// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "NeuralTypes.generated.h"

/**
 * Every neuron in the Crawler's brain, in layout order.
 *
 * The enum value IS the index into the runtime arrays, so circuit code reads as
 * Connect(ENeuronId::ProjectileLeft, ENeuronId::LoomLeft, 1.0f) while the simulation
 * still walks flat contiguous arrays. Phase 10 moves the concrete values into a
 * DataAsset; this enum stays as the stable naming layer.
 */
UENUM(BlueprintType)
enum class ENeuronId : uint8
{
	// --- SENSORY: driven directly from FEnemySensorData, never from synapses.
	PlayerNear,
	PlayerFront,
	PlayerLeft,
	PlayerRight,
	ProjectileFront,
	ProjectileLeft,
	ProjectileRight,
	Pain,
	LowHealth,

	// --- PROCESSING
	Threat,
	Aggression,
	Caution,
	Pursuit,
	LoomFront,
	LoomLeft,
	LoomRight,

	// --- MEMORY: low leak plus recurrent self-connections, so activation outlives its cause.
	ThreatMemory,
	PainMemory,

	// --- MOTOR: read out as behavioural drives. These do not act, they only want.
	Approach,
	Retreat,
	StrafeLeft,
	StrafeRight,
	Attack,
	DodgeLeft,
	DodgeRight,

	Num UMETA(Hidden)
};

/** Static description of one neuron. Never changes during a step. */
USTRUCT(BlueprintType)
struct NEURALAI_API FNeuronDefinition
{
	GENERATED_BODY()

	/** Constant drive added every step. Negative values make a neuron need convincing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neuron")
	float Bias = 0.0f;

	/**
	 * Fraction of the gap between the current potential and the incoming drive that is
	 * closed each step, in 0..1.
	 *   1.0 -> no memory, the neuron mirrors its input immediately (sensory neurons).
	 *   0.1 -> slow integrator that keeps responding after the input stops (memory neurons).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neuron", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float Leak = 1.0f;
};

/** Mutable per-step state. Kept separate from the definition so the hot loop stays compact. */
USTRUCT(BlueprintType)
struct NEURALAI_API FNeuronRuntimeState
{
	GENERATED_BODY()

	/** Integrated drive. May sit outside 0..1; the activation function is what clamps. */
	UPROPERTY(BlueprintReadOnly, Category = "Neuron")
	float Potential = 0.0f;

	/** What downstream neurons actually see. Always 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Neuron")
	float Activation = 0.0f;
};

/** One directed connection. Positive excites, negative inhibits. */
USTRUCT(BlueprintType)
struct NEURALAI_API FNeuralSynapse
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synapse")
	ENeuronId FromNeuron = ENeuronId::PlayerNear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synapse")
	ENeuronId ToNeuron = ENeuronId::Approach;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synapse")
	float Weight = 0.0f;

	FNeuralSynapse() = default;
	FNeuralSynapse(ENeuronId InFrom, ENeuronId InTo, float InWeight)
		: FromNeuron(InFrom), ToNeuron(InTo), Weight(InWeight) {}
};

/**
 * What the brain wants, not what the enemy does.
 *
 * Every field is a desire in 0..1. Turning these into movement, attacks and dodges,
 * with thresholds, cooldowns and commitment, is the AI controller's job.
 */
USTRUCT(BlueprintType)
struct NEURALAI_API FNeuralBrainOutput
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Brain")
	float Approach = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Brain")
	float Retreat = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Brain")
	float StrafeLeft = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Brain")
	float StrafeRight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Brain")
	float Attack = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Brain")
	float DodgeLeft = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Brain")
	float DodgeRight = 0.0f;
};

/**
 * Hook for a future evolution system (spec section 19).
 *
 * Nothing generates these values yet. They exist so the brain already has a documented
 * place to apply generational modifiers, rather than needing surgery later.
 */
USTRUCT(BlueprintType)
struct NEURALAI_API FEnemyAdaptationProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptation", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ProjectileAdaptation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptation", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float CloseRangeAdaptation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptation", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MobilityAdaptation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adaptation", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AggressionAdaptation = 1.0f;
};

/** Index helper so call sites do not repeat the cast. */
FORCEINLINE int32 NeuronIndex(ENeuronId Id) { return static_cast<int32>(Id); }

/** Number of neurons in the fixed layout. */
FORCEINLINE int32 NeuronCount() { return static_cast<int32>(ENeuronId::Num); }
