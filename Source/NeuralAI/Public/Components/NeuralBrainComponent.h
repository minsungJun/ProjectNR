// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/EnemySensorTypes.h"
#include "Core/NeuralTypes.h"
#include "Data/NeuralBrainDataAsset.h"
#include "NeuralBrainComponent.generated.h"

class UEnemyAIProfile;

/**
 * A small rate-based leaky neural network driving the Crawler's behaviour.
 *
 * Hard rule: this component never touches the world. It has no trace, no actor lookup,
 * no access to its owner's transform. Its entire input is an FEnemySensorData handed to
 * Advance(), and its entire output is an FNeuralBrainOutput of behavioural desires.
 * Nothing here decides to move or attack.
 *
 * Neurons are plain structs in contiguous arrays rather than UObjects, so a step is a
 * couple of flat loops over 25 neurons and their synapses.
 */
UCLASS(ClassGroup = (NeuralAI), meta = (BlueprintSpawnableComponent))
class NEURALAI_API UNeuralBrainComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNeuralBrainComponent();

	/**
	 * Feeds time and senses to the brain. Call every frame; the brain decides internally
	 * when enough time has passed to run a step, so callers need no timer of their own.
	 */
	UFUNCTION(BlueprintCallable, Category = "Brain")
	void Advance(float DeltaTime, const FEnemySensorData& SensorInput);

	UFUNCTION(BlueprintPure, Category = "Brain")
	const FNeuralBrainOutput& GetOutput() const { return Output; }

	UFUNCTION(BlueprintPure, Category = "Brain")
	float GetActivation(ENeuronId Neuron) const;

	UFUNCTION(BlueprintPure, Category = "Brain")
	int32 GetNeuronCount() const { return States.Num(); }

	UFUNCTION(BlueprintPure, Category = "Brain")
	int32 GetSynapseCount() const { return Synapses.Num(); }

	/** Steps run since possession. Used to confirm the 20Hz rate in tests. */
	UFUNCTION(BlueprintPure, Category = "Brain")
	int32 GetStepCount() const { return StepCount; }

	/**
	 * Spells out every input that fed a neuron on the last step, with the exact
	 * activations the synapses actually read.
	 *
	 * Deliberately reports measured values rather than anything recomputed: when observed
	 * behaviour stops matching what the weights predict, the discrepancy is the finding,
	 * and a recomputation would hide it.
	 */
	FString DescribeInputs(ENeuronId Neuron) const;

	UFUNCTION(BlueprintPure, Category = "Brain")
	float GetPotential(ENeuronId Neuron) const;

	/** Copies tick rate and temperament bias out of an archetype profile. */
	void ApplyProfile(const UEnemyAIProfile* Profile);

	/**
	 * Adapts behaviour along the six axes (Dodge, Strafe, Caution, Approach, Retreat,
	 * Aggression): the adaptation wiring scales or adds synapses and resting bias.
	 *
	 * Always recomputed from the circuit as authored, so calling this repeatedly replaces the
	 * previous adaptation instead of stacking on it. Safe before or after BeginPlay.
	 */
	UFUNCTION(BlueprintCallable, Category = "Brain|Adaptation")
	void ApplyAdaptationProfile(const FEnemyAdaptationProfile& Profile);

	UFUNCTION(BlueprintPure, Category = "Brain|Adaptation")
	const FEnemyAdaptationProfile& GetAdaptationProfile() const { return AdaptationProfile; }

	/**
	 * Which way the adapted strafe wiring circles: onto StrafeRight, or mirrored onto StrafeLeft.
	 * Picked at random at BeginPlay and swapped every few seconds while the Strafe axis is up,
	 * so an adapted enemy weaves rather than orbiting one way for ever.
	 */
	UFUNCTION(BlueprintCallable, Category = "Brain|Adaptation")
	void SetStrafeSide(bool bRight);

	UFUNCTION(BlueprintPure, Category = "Brain|Adaptation")
	bool IsStrafingRight() const { return bStrafeRight; }

	/** Summed weight of the live synapses From -> To, 0 when there is none. For tests and diagnostics. */
	float GetSynapseWeight(ENeuronId From, ENeuronId To) const;

	/** Resting drive the adaptation adds to a neuron. For tests and diagnostics. */
	float GetAdaptiveBias(ENeuronId Neuron) const;

	/** Builds the brain now instead of at BeginPlay, for tests that run it without a world. */
	void InitializeForTest() { BuildDefaultBrain(); }

	/** Turns the random swap of strafe side on or off (tests). */
	void SetStrafeSwapEnabled(bool bEnabled) { bStrafeSwapEnabled = bEnabled; }

protected:
	virtual void BeginPlay() override;

	/** Populates neuron definitions and the synapse list. Circuits are added per phase. */
	void BuildDefaultBrain();

	/** Copies sensor readings onto the sensory neurons' external drive. */
	void ApplySensoryInput(const FEnemySensorData& SensorInput);

	/** One discrete update of the whole network. */
	void StepBrain();

	/** Copies motor neuron activations into the output struct. */
	void ReadMotorOutput();

	void Connect(ENeuronId From, ENeuronId To, float Weight);

	/** Identity input and output routing, used when no data asset overrides it. */
	void BuildDefaultMappings();

	/** Replaces definitions, synapses and mappings with the asset's, where it provides them. */
	void ApplyBrainAsset();

	/** Recomputes live synapse weights and adaptive bias from the base circuit plus the adaptation profile. */
	void RebuildSynapses();

	/** The built-in adaptation wiring, used unless a brain asset brings its own. */
	void BuildDefaultAdaptation();

	/** StrafeRight onto the side currently circled; every other neuron unchanged. */
	ENeuronId MirrorStrafe(ENeuronId Neuron) const;

	/** Seconds until the next strafe side swap, drawn anew after each swap. */
	void ResetStrafeSwapTimer();

	/**
	 * Checks the wiring for feedback that can never settle, and reports the half-life of
	 * every memory neuron. Runs once at BeginPlay: a latching loop is invisible until it
	 * has already ruined the behaviour, so it is worth catching at the source.
	 */
	void ValidateBrainStability() const;

	/** Steps per second. 20Hz is deliberate: behaviour does not need frame resolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brain", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float BrainTickRate = 20.0f;

	/** Guards against a hitch dumping a hundred steps into one frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brain", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxStepsPerFrame = 3;

private:
	UPROPERTY(EditAnywhere, Category = "Brain", meta = (AllowPrivateAccess = "true"))
	TArray<FNeuronDefinition> Definitions;

	/** Optional override for the built-in circuit. Empty means use the hardcoded one. */
	UPROPERTY(EditAnywhere, Category = "Brain", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNeuralBrainDataAsset> BrainAsset;

	/** The circuit as authored, before any adaptation modifiers. */
	TArray<FNeuralSynapse> BaseSynapses;

	/** The circuit actually simulated. */
	TArray<FNeuralSynapse> Synapses;

	TArray<FNeuralInputMapping> InputMappings;
	TArray<FNeuralOutputMapping> OutputMappings;
	TArray<FAdaptableSynapse> AdaptableSynapses;
	TArray<FAdaptableBias> AdaptableBiases;

	FEnemyAdaptationProfile AdaptationProfile;

	/** Resting drive per neuron from the adaptation, on top of the definition's own bias. */
	TArray<float> AdaptiveBias;

	bool bStrafeRight = true;
	bool bStrafeSwapEnabled = true;
	float StrafeSwapSecondsLeft = 0.0f;

	TArray<FNeuronRuntimeState> States;

	/** Per-step scratch: sensory drive injected from outside the network. */
	TArray<float> ExternalInput;

	/** Per-step scratch: total drive per neuron before integration. */
	TArray<float> DriveScratch;

	/** Drive actually used by the last step, kept for diagnostics. */
	TArray<float> LastDrive;

	/**
	 * Activations as they stood when the last step's synapses read them.
	 *
	 * Needed because by the time anything inspects the network, States already holds the
	 * post-step values. Reading those back would describe a different step than the one
	 * that produced the drive.
	 */
	TArray<float> PrevActivations;

	FNeuralBrainOutput Output;

	float StepAccumulator = 0.0f;
	int32 StepCount = 0;
};
