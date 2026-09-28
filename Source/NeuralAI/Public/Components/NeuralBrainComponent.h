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
	 * Scales the synapses a future evolution system is allowed to touch.
	 *
	 * Modifiers are applied to preserved base weights rather than to the live ones, so
	 * calling this repeatedly cannot compound: each call recomputes from the original
	 * circuit. Nothing in this prototype generates a profile; this is the seam where a
	 * generational system would attach.
	 */
	UFUNCTION(BlueprintCallable, Category = "Brain|Adaptation")
	void ApplyAdaptationProfile(const FEnemyAdaptationProfile& Profile);

	UFUNCTION(BlueprintPure, Category = "Brain|Adaptation")
	const FEnemyAdaptationProfile& GetAdaptationProfile() const { return AdaptationProfile; }

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

	/** Recomputes live synapse weights from the base circuit plus the adaptation profile. */
	void RebuildSynapses();

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

	FEnemyAdaptationProfile AdaptationProfile;

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
