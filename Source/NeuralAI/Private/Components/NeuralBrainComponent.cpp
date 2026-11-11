// Neural Enemy AI prototype module.

#include "Components/NeuralBrainComponent.h"
#include "Data/EnemyAIProfile.h"
#include "NeuralAI.h"

UNeuralBrainComponent::UNeuralBrainComponent()
{
	// The brain is stepped by the AI controller, which owns the sensor data. Ticking
	// itself would force it to go looking for that data, which is exactly the coupling
	// this component is built to avoid.
	PrimaryComponentTick.bCanEverTick = false;
}

void UNeuralBrainComponent::BeginPlay()
{
	Super::BeginPlay();

	bStrafeRight = FMath::RandBool();
	ResetStrafeSwapTimer();
	BuildDefaultBrain();

	UE_LOG(LogNeuralAI, Log, TEXT("%s brain built: %d neurons, %d synapses, %.0fHz"),
		*GetNameSafe(GetOwner()), States.Num(), Synapses.Num(), BrainTickRate);

	ValidateBrainStability();
}

void UNeuralBrainComponent::BuildDefaultBrain()
{
	const int32 Count = NeuronCount();

	Definitions.SetNum(Count);
	States.SetNum(Count);
	ExternalInput.SetNumZeroed(Count);
	DriveScratch.SetNumZeroed(Count);
	LastDrive.SetNumZeroed(Count);
	PrevActivations.SetNumZeroed(Count);
	AdaptiveBias.SetNumZeroed(Count);

	// Leak per role. This single number is what separates a reflex from a memory:
	// a sensory neuron must mirror its input this instant, while ThreatMemory must
	// still be talking several seconds after the danger passed.
	auto Define = [this](ENeuronId Id, float Bias, float Leak)
	{
		FNeuronDefinition& Def = Definitions[NeuronIndex(Id)];
		Def.Bias = Bias;
		Def.Leak = Leak;
	};

	// Sensory: pure pass-through of the sensor snapshot.
	Define(ENeuronId::PlayerNear, 0.0f, 1.0f);
	Define(ENeuronId::PlayerFront, 0.0f, 1.0f);
	Define(ENeuronId::PlayerLeft, 0.0f, 1.0f);
	Define(ENeuronId::PlayerRight, 0.0f, 1.0f);
	Define(ENeuronId::ProjectileFront, 0.0f, 1.0f);
	Define(ENeuronId::ProjectileLeft, 0.0f, 1.0f);
	Define(ENeuronId::ProjectileRight, 0.0f, 1.0f);
	Define(ENeuronId::Pain, 0.0f, 1.0f);
	Define(ENeuronId::LowHealth, 0.0f, 1.0f);

	// Processing: fast enough to feel reactive, damped enough not to chatter.
	Define(ENeuronId::Threat, 0.0f, 0.5f);
	Define(ENeuronId::Aggression, 0.0f, 0.4f);
	Define(ENeuronId::Caution, 0.0f, 0.4f);
	Define(ENeuronId::Pursuit, 0.0f, 0.4f);

	// Looming detectors sit right behind the projectile senses, so they stay quick.
	//
	// Raised from 0.8 after instrumentation showed dodges firing on the previous step's
	// loom value: projectile threat collapses to zero the instant a shot passes its
	// closest approach, but at the slower rate loom still read 0.98 one step later,
	// which was enough to launch a dodge at something already gone.
	Define(ENeuronId::LoomFront, 0.0f, 0.95f);
	Define(ENeuronId::LoomLeft, 0.0f, 0.95f);
	Define(ENeuronId::LoomRight, 0.0f, 0.95f);

	// Memory: a low leak is the whole mechanism. At 20Hz a leak of 0.05 decays to
	// roughly a third of its value after one second.
	Define(ENeuronId::ThreatMemory, 0.0f, 0.028f);
	Define(ENeuronId::PainMemory, 0.0f, 0.017f);

	// Motor: responsive, but slow enough that a single noisy step cannot flip a decision.
	Define(ENeuronId::Approach, 0.0f, 0.6f);
	Define(ENeuronId::Retreat, 0.0f, 0.6f);
	Define(ENeuronId::StrafeLeft, 0.0f, 0.6f);
	Define(ENeuronId::StrafeRight, 0.0f, 0.6f);
	Define(ENeuronId::Attack, -0.35f, 0.6f);

	// Dodges must fire almost immediately or the shot has already landed.
	//
	// The negative bias is what keeps them honest. Threat alone, sustained by memory long
	// after a shot has passed, used to lift both dodges together until one crossed the
	// controller's threshold by a hair - the crawler would leap sideways at nothing, and
	// the direction it picked was noise rather than a decision. Requiring the drive to
	// overcome a resting deficit means only a genuine looming signal, which is
	// directional, can produce a dodge.
	Define(ENeuronId::DodgeLeft, -0.25f, 0.9f);
	Define(ENeuronId::DodgeRight, -0.25f, 0.9f);

	Synapses.Reset();

	// --- Phase 6 circuit: seeing the player makes the Crawler want to close in.
	//
	//     PlayerNear  --(2.0)-->  Aggression  --(1.2)-->  Approach
	//     PlayerFront --(1.0)-->  Pursuit     --(0.4)-->  Approach
	//
	// There is no distance test and no call to a move function anywhere in this path.
	// The enemy walks forward only because the Approach neuron happens to be excited,
	// and it speeds up as PlayerNear rises, purely as a consequence of the weights.
	Connect(ENeuronId::PlayerNear, ENeuronId::Aggression, 2.0f);
	Connect(ENeuronId::PlayerFront, ENeuronId::Pursuit, 1.0f);
	Connect(ENeuronId::Aggression, ENeuronId::Approach, 1.2f);
	Connect(ENeuronId::Pursuit, ENeuronId::Approach, 0.4f);

	// Looming and dodging arrive in Phase 7, memory in Phase 8.

	// --- Phase 7 circuit: incoming fire becomes an urge to break sideways.
	//
	//   ProjectileLeft --> LoomLeft --> Threat
	//                          |
	//                          +------> DodgeRight        (escape away from the shot)
	//
	//   Threat --| Approach, Attack                       (inhibitory)
	//   Threat --> Caution --> Retreat
	//
	// Note the shape: direction is carried by the Loom neurons, while Threat is a single
	// scalar that says how much danger there is at all. That split is what lets one
	// signal both choose a side and suppress aggression, without Threat needing to know
	// left from right.

	// A projectile predicted to arrive excites the looming detector on its side.
	Connect(ENeuronId::ProjectileLeft, ENeuronId::LoomLeft, 1.4f);
	Connect(ENeuronId::ProjectileRight, ENeuronId::LoomRight, 1.4f);
	Connect(ENeuronId::ProjectileFront, ENeuronId::LoomFront, 1.4f);

	// Any looming contributes to the shared sense of danger.
	Connect(ENeuronId::LoomLeft, ENeuronId::Threat, 0.8f);
	Connect(ENeuronId::LoomRight, ENeuronId::Threat, 0.8f);
	Connect(ENeuronId::LoomFront, ENeuronId::Threat, 1.0f);

	// Escape sideways, away from where the shot is coming from.
	Connect(ENeuronId::LoomLeft, ENeuronId::DodgeRight, 1.3f);
	Connect(ENeuronId::LoomRight, ENeuronId::DodgeLeft, 1.3f);

	// A head-on shot has no side to run from. Both dodges are excited, but unevenly:
	// a perfectly symmetric wiring would leave the crawler frozen between two equal
	// options, so the tie is broken in the weights rather than by a coin flip later.
	Connect(ENeuronId::LoomFront, ENeuronId::DodgeRight, 0.9f);
	Connect(ENeuronId::LoomFront, ENeuronId::DodgeLeft, 0.7f);

	// Threat deliberately does NOT feed the dodges.
	//
	// It used to, on the reasoning that danger should lift them towards the controller's
	// threshold. In practice Threat is held up for seconds after a shot by ThreatMemory,
	// and its contribution cancelled the dodges' resting bias exactly when memory was
	// strongest. With the guard gone, loom residue of 0.12 was enough to launch a dodge
	// at a projectile that had already flown past. Threat earns its keep elsewhere, by
	// suppressing Approach and Attack and by driving Caution; the dodges take direction
	// from looming alone, which is the only signal that knows which way to go.

	// Mutual inhibition. Whichever dodge is ahead suppresses the other, so the crawler
	// commits to one side instead of splitting the difference and stepping nowhere.
	// Kept moderate on purpose: strong reciprocal inhibition on fast neurons oscillates.
	Connect(ENeuronId::DodgeLeft, ENeuronId::DodgeRight, -0.8f);
	Connect(ENeuronId::DodgeRight, ENeuronId::DodgeLeft, -0.8f);

	// Threat overrides aggression. This is why the crawler stops walking into fire:
	// not because anything checks for a projectile, but because Approach is being
	// actively pushed down while Aggression is still pushing it up.
	Connect(ENeuronId::Threat, ENeuronId::Approach, -1.0f);
	Connect(ENeuronId::Threat, ENeuronId::Attack, -1.2f);

	// Sustained danger turns into backing off.
	Connect(ENeuronId::Threat, ENeuronId::Caution, 0.9f);
	Connect(ENeuronId::Caution, ENeuronId::Retreat, 0.8f);

	// --- Phase 8a: wanting to attack.
	//
	// Attack carries a negative bias, so it needs several agreeing reasons before it
	// rises at all. Excitation is kept deliberately modest: if a point-blank target
	// drove Attack far past 1, the inhibition from Threat could never pull it back
	// down, and the crawler would keep swinging while being shot at.
	Connect(ENeuronId::PlayerNear, ENeuronId::Attack, 0.8f);
	Connect(ENeuronId::PlayerFront, ENeuronId::Attack, 0.5f);
	Connect(ENeuronId::Aggression, ENeuronId::Attack, 0.3f);

	// --- Phase 8b: memory.
	//
	//   Pain ---> PainMemory ---.
	//                 |          |
	//   Threat ---> ThreatMemory <-'        (both self-connected)
	//                 |
	//                 +--> Threat           (danger outlives its cause)
	//                 +--> Caution
	//                 +--| Approach
	//
	// The self-connections are what make these neurons remember. With a self weight w
	// and leak L, an isolated neuron decays by a factor of (1 - L(1-w)) per step, so at
	// 20Hz these values give a half-life of roughly 2.5s for threat and 4s for pain.
	//
	// Keeping w below 1 is not a stylistic choice: at w = 1 the neuron would hold its
	// value forever, and above 1 it would run away until it saturated and never fall.
	// w also sets how much the neuron amplifies its input, by 1/(1 - w), which is why
	// these sit at 0.5 rather than something higher: the amplification is part of every
	// loop that passes through them.
	Connect(ENeuronId::ThreatMemory, ENeuronId::ThreatMemory, 0.5f);
	Connect(ENeuronId::PainMemory, ENeuronId::PainMemory, 0.5f);

	Connect(ENeuronId::Threat, ENeuronId::ThreatMemory, 0.9f);
	Connect(ENeuronId::Pain, ENeuronId::ThreatMemory, 0.8f);
	Connect(ENeuronId::Pain, ENeuronId::PainMemory, 1.2f);
	Connect(ENeuronId::PainMemory, ENeuronId::ThreatMemory, 0.3f);

	// Feeding memory back into Threat is what makes danger fade instead of vanishing the
	// instant the projectile is gone.
	//
	// The loop gain here is easy to get wrong. It is NOT simply the product of the two
	// weights: a self-connected neuron amplifies whatever reaches it by 1/(1 - w_self),
	// so that factor belongs in the round trip too.
	//
	//     gain = (Threat->Memory) * 1/(1 - w_self) * (Memory->Threat)
	//          = 0.9 * 1/(1 - 0.5) * 0.25
	//          = 0.45
	//
	// Above 1 the pair latches: any momentary threat drives both to saturation and they
	// hold each other there forever, leaving the crawler permanently retreating from a
	// danger that ended seconds ago.
	Connect(ENeuronId::ThreatMemory, ENeuronId::Threat, 0.25f);

	// A remembered beating keeps the crawler wary: more cautious, less eager to close.
	Connect(ENeuronId::ThreatMemory, ENeuronId::Caution, 0.5f);
	Connect(ENeuronId::ThreatMemory, ENeuronId::Approach, -0.4f);
	Connect(ENeuronId::PainMemory, ENeuronId::Caution, 0.4f);

	// Everything above wrote into Synapses via Connect(). Promote that to the base
	// circuit, lay down default routing, let an asset override any of it, then derive
	// the live weights.
	//
	// This must stay after the last Connect(). It used to sit between Phase 7 and 8, which
	// left the attack and memory synapses out of BaseSynapses: RebuildSynapses() would
	// silently drop them, and an asset's synapses would get Phase 8 stacked on top.
	BaseSynapses = Synapses;
	BuildDefaultMappings();
	BuildDefaultAdaptation();
	ApplyBrainAsset();
	RebuildSynapses();
}

void UNeuralBrainComponent::Connect(ENeuronId From, ENeuronId To, float Weight)
{
	Synapses.Emplace(From, To, Weight);
}

void UNeuralBrainComponent::Advance(float DeltaTime, const FEnemySensorData& SensorInput)
{
	if (States.Num() != NeuronCount())
	{
		return;
	}

	const float StepInterval = 1.0f / FMath::Max(BrainTickRate, 1.0f);
	StepAccumulator += DeltaTime;

	int32 StepsThisFrame = 0;
	while (StepAccumulator >= StepInterval && StepsThisFrame < MaxStepsPerFrame)
	{
		StepAccumulator -= StepInterval;
		++StepsThisFrame;

		ApplySensoryInput(SensorInput);
		StepBrain();
	}

	// After a long hitch, drop the backlog rather than spending the next second catching up.
	if (StepsThisFrame >= MaxStepsPerFrame)
	{
		StepAccumulator = 0.0f;
	}

	if (StepsThisFrame > 0)
	{
		ReadMotorOutput();
	}

	// An adapted strafer swaps sides now and then, so it weaves instead of orbiting.
	if (bStrafeSwapEnabled && AdaptationProfile.Strafe > 0.05f)
	{
		StrafeSwapSecondsLeft -= DeltaTime;
		if (StrafeSwapSecondsLeft <= 0.0f)
		{
			ResetStrafeSwapTimer();
			SetStrafeSide(!bStrafeRight);
		}
	}
}

void UNeuralBrainComponent::ApplySensoryInput(const FEnemySensorData& SensorInput)
{
	for (float& Value : ExternalInput)
	{
		Value = 0.0f;
	}

	auto ReadChannel = [&SensorInput](ESensorChannel Channel)
	{
		switch (Channel)
		{
		case ESensorChannel::TargetDetected:         return SensorInput.TargetDetected;
		case ESensorChannel::TargetNear:             return SensorInput.TargetNear;
		case ESensorChannel::TargetFront:            return SensorInput.TargetFront;
		case ESensorChannel::TargetLeft:             return SensorInput.TargetLeft;
		case ESensorChannel::TargetRight:            return SensorInput.TargetRight;
		case ESensorChannel::TargetClosingSpeed:     return SensorInput.TargetClosingSpeed;
		case ESensorChannel::ProjectileThreatFront:  return SensorInput.ProjectileThreatFront;
		case ESensorChannel::ProjectileThreatLeft:   return SensorInput.ProjectileThreatLeft;
		case ESensorChannel::ProjectileThreatRight:  return SensorInput.ProjectileThreatRight;
		case ESensorChannel::Pain:                   return SensorInput.Pain;
		case ESensorChannel::LowHealth:              return SensorInput.LowHealth;
		default:                                     return 0.0f;
		}
	};

	for (const FNeuralInputMapping& Mapping : InputMappings)
	{
		const int32 Index = NeuronIndex(Mapping.Neuron);
		if (!ExternalInput.IsValidIndex(Index))
		{
			continue;
		}

		float Value = ReadChannel(Mapping.Channel) * Mapping.Scale;
		if (Mapping.bGateOnTargetDetected)
		{
			Value *= SensorInput.TargetDetected;
		}

		ExternalInput[Index] += Value;
	}
}

void UNeuralBrainComponent::StepBrain()
{
	SCOPE_CYCLE_COUNTER(STAT_NeuralAI_BrainStep);
	INC_DWORD_STAT(STAT_NeuralAI_StepsPerFrame);

	const int32 Count = States.Num();

	// 0. Snapshot the activations the synapses are about to read. Integration overwrites
	//    them below, and without this copy any later inspection would be describing the
	//    result of the step rather than its inputs.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		PrevActivations[Index] = States[Index].Activation;
	}

	// 1. Start every neuron at its constant bias plus whatever the senses injected.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		DriveScratch[Index] = Definitions[Index].Bias + AdaptiveBias[Index] + ExternalInput[Index];
	}

	// 2. Accumulate synapses using LAST step's activations. Reading the previous state
	//    rather than the one being written is what makes the update order irrelevant and
	//    lets recurrent connections work without feeding back within a single step.
	for (const FNeuralSynapse& Synapse : Synapses)
	{
		const int32 From = NeuronIndex(Synapse.FromNeuron);
		const int32 To = NeuronIndex(Synapse.ToNeuron);
		if (From < Count && To < Count)
		{
			DriveScratch[To] += Synapse.Weight * States[From].Activation;
		}
	}

	LastDrive = DriveScratch;

	// 3. Leaky integration, then the activation function.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FNeuronRuntimeState& State = States[Index];
		const FNeuronDefinition& Def = Definitions[Index];

		State.Potential += (DriveScratch[Index] - State.Potential) * Def.Leak;

		// Half-wave rectify and saturate. Keeping activations in 0..1 means a synapse
		// weight of 1.0 means the same thing everywhere in the network, which is what
		// makes a hand-wired circuit tunable at all.
		State.Activation = FMath::Clamp(State.Potential, 0.0f, 1.0f);

		// A single non-finite value would poison the whole network through the synapses.
		if (!FMath::IsFinite(State.Potential))
		{
			UE_LOG(LogNeuralAI, Error, TEXT("Neuron %d went non-finite; resetting"), Index);
			State.Potential = 0.0f;
			State.Activation = 0.0f;
		}
	}

	++StepCount;
}

void UNeuralBrainComponent::ReadMotorOutput()
{
	Output = FNeuralBrainOutput();

	for (const FNeuralOutputMapping& Mapping : OutputMappings)
	{
		const float Activation = GetActivation(Mapping.Neuron);
		switch (Mapping.Channel)
		{
		case EMotorChannel::Approach:    Output.Approach = Activation; break;
		case EMotorChannel::Retreat:     Output.Retreat = Activation; break;
		case EMotorChannel::StrafeLeft:  Output.StrafeLeft = Activation; break;
		case EMotorChannel::StrafeRight: Output.StrafeRight = Activation; break;
		case EMotorChannel::Attack:      Output.Attack = Activation; break;
		case EMotorChannel::DodgeLeft:   Output.DodgeLeft = Activation; break;
		case EMotorChannel::DodgeRight:  Output.DodgeRight = Activation; break;
		}
	}
}

float UNeuralBrainComponent::GetActivation(ENeuronId Neuron) const
{
	const int32 Index = NeuronIndex(Neuron);
	return States.IsValidIndex(Index) ? States[Index].Activation : 0.0f;
}

void UNeuralBrainComponent::ValidateBrainStability() const
{
	const int32 Count = Definitions.Num();

	// Self weight per neuron, and the DC amplification it causes. A neuron that feeds
	// itself with weight w multiplies anything arriving at it by 1/(1 - w) once settled.
	TArray<float> SelfWeight;
	TArray<float> Amplification;
	SelfWeight.SetNumZeroed(Count);
	Amplification.Init(1.0f, Count);

	for (const FNeuralSynapse& Synapse : Synapses)
	{
		if (Synapse.FromNeuron == Synapse.ToNeuron)
		{
			SelfWeight[NeuronIndex(Synapse.FromNeuron)] += Synapse.Weight;
		}
	}

	const UEnum* NeuronEnum = StaticEnum<ENeuronId>();
	auto NameOf = [NeuronEnum](int32 Index)
	{
		return NeuronEnum ? NeuronEnum->GetNameStringByValue(Index) : FString::FromInt(Index);
	};

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float W = SelfWeight[Index];
		if (W >= 1.0f)
		{
			UE_LOG(LogNeuralAI, Error,
				TEXT("Neuron %s has self weight %.2f: at or above 1 it can never decay."),
				*NameOf(Index), W);
			Amplification[Index] = BIG_NUMBER;
			continue;
		}

		if (W > 0.0f)
		{
			Amplification[Index] = 1.0f / (1.0f - W);

			// Half-life follows from the per-step decay factor 1 - L(1 - w).
			const float Leak = Definitions[Index].Leak;
			const float PerStepDecay = 1.0f - Leak * (1.0f - W);
			const float HalfLifeSeconds = (PerStepDecay > 0.0f && PerStepDecay < 1.0f)
				? FMath::Loge(0.5f) / FMath::Loge(PerStepDecay) / BrainTickRate
				: 0.0f;

			UE_LOG(LogNeuralAI, Log,
				TEXT("Memory neuron %s: self %.2f, leak %.3f -> amplification %.2fx, half-life %.1fs"),
				*NameOf(Index), W, Leak, Amplification[Index], HalfLifeSeconds);
		}
	}

	// Two-neuron positive feedback loops, with each neuron's self-amplification included.
	// Leaving that factor out is exactly how this brain once ended up latching Threat on
	// permanently, so the check exists to make the same mistake fail loudly next time.
	for (const FNeuralSynapse& Forward : Synapses)
	{
		if (Forward.FromNeuron == Forward.ToNeuron)
		{
			continue;
		}

		for (const FNeuralSynapse& Back : Synapses)
		{
			if (Back.FromNeuron != Forward.ToNeuron || Back.ToNeuron != Forward.FromNeuron)
			{
				continue;
			}

			const int32 A = NeuronIndex(Forward.FromNeuron);
			const int32 B = NeuronIndex(Forward.ToNeuron);
			const float LoopGain = Forward.Weight * Amplification[B] * Back.Weight * Amplification[A];

			// Negative loops are mutual inhibition, which settles on its own.
			if (LoopGain >= 1.0f)
			{
				UE_LOG(LogNeuralAI, Error,
					TEXT("Loop %s <-> %s has gain %.2f (>= 1). These two will latch each other on."),
					*NameOf(A), *NameOf(B), LoopGain);
			}
			else if (LoopGain >= 0.8f)
			{
				UE_LOG(LogNeuralAI, Warning,
					TEXT("Loop %s <-> %s has gain %.2f, close to the stability limit."),
					*NameOf(A), *NameOf(B), LoopGain);
			}
		}
	}
}

FString UNeuralBrainComponent::DescribeInputs(ENeuronId Neuron) const
{
	const int32 Index = NeuronIndex(Neuron);
	if (!States.IsValidIndex(Index))
	{
		return TEXT("(invalid neuron)");
	}

	const UEnum* NeuronEnum = StaticEnum<ENeuronId>();
	FString Result = FString::Printf(TEXT("bias %.2f"), Definitions[Index].Bias + (AdaptiveBias.IsValidIndex(Index) ? AdaptiveBias[Index] : 0.0f));

	for (const FNeuralSynapse& Synapse : Synapses)
	{
		if (NeuronIndex(Synapse.ToNeuron) != Index)
		{
			continue;
		}

		const int32 From = NeuronIndex(Synapse.FromNeuron);
		const float SourceActivation = PrevActivations.IsValidIndex(From) ? PrevActivations[From] : 0.0f;
		const float Contribution = SourceActivation * Synapse.Weight;
		if (FMath::Abs(Contribution) < 0.005f)
		{
			continue;
		}

		Result += FString::Printf(TEXT(" + %s(%.2f)x%.2f=%.2f"),
			*NeuronEnum->GetNameStringByValue(From), SourceActivation, Synapse.Weight, Contribution);
	}

	Result += FString::Printf(TEXT(" => drive %.2f, potential %.2f, activation %.2f"),
		LastDrive.IsValidIndex(Index) ? LastDrive[Index] : 0.0f,
		States[Index].Potential,
		States[Index].Activation);

	return Result;
}

float UNeuralBrainComponent::GetPotential(ENeuronId Neuron) const
{
	const int32 Index = NeuronIndex(Neuron);
	return States.IsValidIndex(Index) ? States[Index].Potential : 0.0f;
}

void UNeuralBrainComponent::BuildDefaultMappings()
{
	InputMappings.Reset();
	OutputMappings.Reset();

	// Player bearings are gated on detection so losing sight silences the whole branch
	// at once rather than leaving a stale direction behind.
	InputMappings.Add({ ESensorChannel::TargetNear, ENeuronId::PlayerNear, 1.0f, true });
	InputMappings.Add({ ESensorChannel::TargetFront, ENeuronId::PlayerFront, 1.0f, true });
	InputMappings.Add({ ESensorChannel::TargetLeft, ENeuronId::PlayerLeft, 1.0f, true });
	InputMappings.Add({ ESensorChannel::TargetRight, ENeuronId::PlayerRight, 1.0f, true });

	// Projectile threat is never gated: being shot from behind still hurts.
	InputMappings.Add({ ESensorChannel::ProjectileThreatFront, ENeuronId::ProjectileFront, 1.0f, false });
	InputMappings.Add({ ESensorChannel::ProjectileThreatLeft, ENeuronId::ProjectileLeft, 1.0f, false });
	InputMappings.Add({ ESensorChannel::ProjectileThreatRight, ENeuronId::ProjectileRight, 1.0f, false });

	InputMappings.Add({ ESensorChannel::Pain, ENeuronId::Pain, 1.0f, false });
	InputMappings.Add({ ESensorChannel::LowHealth, ENeuronId::LowHealth, 1.0f, false });

	OutputMappings.Add({ ENeuronId::Approach, EMotorChannel::Approach });
	OutputMappings.Add({ ENeuronId::Retreat, EMotorChannel::Retreat });
	OutputMappings.Add({ ENeuronId::StrafeLeft, EMotorChannel::StrafeLeft });
	OutputMappings.Add({ ENeuronId::StrafeRight, EMotorChannel::StrafeRight });
	OutputMappings.Add({ ENeuronId::Attack, EMotorChannel::Attack });
	OutputMappings.Add({ ENeuronId::DodgeLeft, EMotorChannel::DodgeLeft });
	OutputMappings.Add({ ENeuronId::DodgeRight, EMotorChannel::DodgeRight });
}

void UNeuralBrainComponent::ApplyBrainAsset()
{
	if (!BrainAsset)
	{
		return;
	}

	// Each section overrides independently, so an asset can retune weights while leaving
	// neuron definitions and routing on the built-in defaults.
	const int32 Count = Definitions.Num();
	for (int32 Index = 0; Index < Count && Index < BrainAsset->NeuronDefinitions.Num(); ++Index)
	{
		Definitions[Index] = BrainAsset->NeuronDefinitions[Index];
	}

	if (BrainAsset->Synapses.Num() > 0)
	{
		BaseSynapses = BrainAsset->Synapses;
	}
	if (BrainAsset->InputMappings.Num() > 0)
	{
		InputMappings = BrainAsset->InputMappings;
	}
	if (BrainAsset->OutputMappings.Num() > 0)
	{
		OutputMappings = BrainAsset->OutputMappings;
	}
	if (BrainAsset->AdaptableSynapses.Num() > 0 || BrainAsset->AdaptableBiases.Num() > 0)
	{
		AdaptableSynapses = BrainAsset->AdaptableSynapses;
		AdaptableBiases = BrainAsset->AdaptableBiases;
	}

	UE_LOG(LogNeuralAI, Log, TEXT("%s brain overridden by %s"),
		*GetNameSafe(GetOwner()), *GetNameSafe(BrainAsset));
}

void UNeuralBrainComponent::BuildDefaultAdaptation()
{
	using N = ENeuronId;
	using A = EAdaptationAxis;
	AdaptableSynapses.Reset();
	AdaptableBiases.Reset();

	// Each axis at +1 moves its cluster this far, -1 the same distance the other way. Kept
	// modest so the base circuit's balance (threat beats aggression, dodges need a looming
	// signal) survives a fully adapted enemy.

	// Dodge: looming turns into a sidestep sooner and harder. The resting deficit shrinks but
	// stays negative (-0.25 + 0.12), so a dodge still needs a shot actually coming.
	AdaptableSynapses.Emplace(N::LoomLeft, N::DodgeRight, A::Dodge, 0.5f);
	AdaptableSynapses.Emplace(N::LoomRight, N::DodgeLeft, A::Dodge, 0.5f);
	AdaptableSynapses.Emplace(N::LoomFront, N::DodgeRight, A::Dodge, 0.5f);
	AdaptableSynapses.Emplace(N::LoomFront, N::DodgeLeft, A::Dodge, 0.5f);
	AdaptableBiases.Emplace(N::DodgeLeft, A::Dodge, 0.12f);
	AdaptableBiases.Emplace(N::DodgeRight, A::Dodge, 0.12f);

	// Strafe: the base circuit never drives the strafe neurons. Adaptation wires them from
	// nothing - a nearby target and danger make the enemy circle. Written onto StrafeRight and
	// mirrored onto StrafeLeft while it circles the other way.
	AdaptableSynapses.Emplace(N::PlayerNear, N::StrafeRight, A::Strafe, 0.0f, 0.6f);
	AdaptableSynapses.Emplace(N::Threat, N::StrafeRight, A::Strafe, 0.0f, 0.5f);
	AdaptableSynapses.Emplace(N::ThreatMemory, N::StrafeRight, A::Strafe, 0.0f, 0.3f);

	// Caution: danger registers more and keeps the enemy cautious longer.
	AdaptableSynapses.Emplace(N::LoomLeft, N::Threat, A::Caution, 0.4f);
	AdaptableSynapses.Emplace(N::LoomRight, N::Threat, A::Caution, 0.4f);
	AdaptableSynapses.Emplace(N::LoomFront, N::Threat, A::Caution, 0.4f);
	AdaptableSynapses.Emplace(N::Threat, N::Caution, A::Caution, 0.5f);
	AdaptableSynapses.Emplace(N::ThreatMemory, N::Caution, A::Caution, 0.5f);
	AdaptableBiases.Emplace(N::Caution, A::Caution, 0.1f);

	// Approach: the urge to close. Below 0 a near target also holds it back.
	AdaptableSynapses.Emplace(N::Aggression, N::Approach, A::Approach, 0.5f);
	AdaptableSynapses.Emplace(N::Pursuit, N::Approach, A::Approach, 0.5f);
	AdaptableSynapses.Emplace(N::PlayerNear, N::Approach, A::Approach, 0.0f, 0.3f);

	// Retreat: caution turns into backing off, and a close target is a reason to open the gap.
	AdaptableSynapses.Emplace(N::Caution, N::Retreat, A::Retreat, 0.6f);
	AdaptableSynapses.Emplace(N::PlayerNear, N::Retreat, A::Retreat, 0.0f, 0.5f);
	AdaptableBiases.Emplace(N::Retreat, A::Retreat, 0.05f);

	// Aggression: an opening turns into an attack more readily, and danger holds it back less.
	AdaptableSynapses.Emplace(N::PlayerNear, N::Aggression, A::Aggression, 0.3f);
	AdaptableSynapses.Emplace(N::Aggression, N::Attack, A::Aggression, 0.6f);
	AdaptableSynapses.Emplace(N::Threat, N::Attack, A::Aggression, -0.3f);
	AdaptableBiases.Emplace(N::Attack, A::Aggression, 0.12f);
}

ENeuronId UNeuralBrainComponent::MirrorStrafe(ENeuronId Neuron) const
{
	if (bStrafeRight)
	{
		return Neuron;
	}
	return Neuron == ENeuronId::StrafeRight ? ENeuronId::StrafeLeft
		: Neuron == ENeuronId::StrafeLeft ? ENeuronId::StrafeRight : Neuron;
}

void UNeuralBrainComponent::ResetStrafeSwapTimer()
{
	StrafeSwapSecondsLeft = FMath::FRandRange(1.5f, 3.5f);
}

void UNeuralBrainComponent::RebuildSynapses()
{
	Synapses = BaseSynapses;
	AdaptiveBias.SetNumZeroed(NeuronCount());
	for (float& Bias : AdaptiveBias)
	{
		Bias = 0.0f;
	}

	for (const FAdaptableSynapse& Adaptable : AdaptableSynapses)
	{
		const float Value = AdaptationProfile.Get(Adaptable.Axis);
		if (FMath::IsNearlyZero(Value))
		{
			continue;
		}
		const ENeuronId To = MirrorStrafe(Adaptable.ToNeuron);

		// Scaling never flips a synapse's sign: an excitatory one at most falls silent.
		const float Scale = FMath::Max(0.0f, 1.0f + Value * Adaptable.Sensitivity);
		bool bFound = false;
		for (FNeuralSynapse& Synapse : Synapses)
		{
			if (Synapse.FromNeuron == Adaptable.FromNeuron && Synapse.ToNeuron == To)
			{
				Synapse.Weight = Synapse.Weight * Scale + Value * Adaptable.AddedWeight;
				bFound = true;
			}
		}
		if (!bFound && !FMath::IsNearlyZero(Adaptable.AddedWeight))
		{
			Synapses.Emplace(Adaptable.FromNeuron, To, Value * Adaptable.AddedWeight);
		}
	}

	for (const FAdaptableBias& Adaptable : AdaptableBiases)
	{
		const int32 Index = NeuronIndex(MirrorStrafe(Adaptable.Neuron));
		if (AdaptiveBias.IsValidIndex(Index))
		{
			AdaptiveBias[Index] += AdaptationProfile.Get(Adaptable.Axis) * Adaptable.BiasPerUnit;
		}
	}
}

void UNeuralBrainComponent::ApplyAdaptationProfile(const FEnemyAdaptationProfile& Profile)
{
	AdaptationProfile = Profile;

	// Rebuilt from BaseSynapses, never from the live ones, so repeated calls replace the
	// previous adaptation instead of stacking on top of it.
	RebuildSynapses();

	UE_LOG(LogNeuralAI, Log, TEXT("%s adaptation applied: %s"), *GetNameSafe(GetOwner()), *Profile.ToString());
}

void UNeuralBrainComponent::SetStrafeSide(bool bRight)
{
	if (bStrafeRight == bRight)
	{
		return;
	}
	bStrafeRight = bRight;
	RebuildSynapses();
}

float UNeuralBrainComponent::GetSynapseWeight(ENeuronId From, ENeuronId To) const
{
	float Weight = 0.0f;
	for (const FNeuralSynapse& Synapse : Synapses)
	{
		if (Synapse.FromNeuron == From && Synapse.ToNeuron == To)
		{
			Weight += Synapse.Weight;
		}
	}
	return Weight;
}

float UNeuralBrainComponent::GetAdaptiveBias(ENeuronId Neuron) const
{
	const int32 Index = NeuronIndex(Neuron);
	return AdaptiveBias.IsValidIndex(Index) ? AdaptiveBias[Index] : 0.0f;
}

void UNeuralBrainComponent::ApplyProfile(const UEnemyAIProfile* Profile)
{
	if (!Profile)
	{
		return;
	}

	BrainTickRate = Profile->BrainTickRate;

	// Temperament is expressed as resting bias rather than as different wiring: the same
	// circuit with a standing head start on aggression behaves like a bolder animal.
	if (Definitions.IsValidIndex(NeuronIndex(ENeuronId::Aggression)))
	{
		Definitions[NeuronIndex(ENeuronId::Aggression)].Bias += Profile->AggressionBias;
	}
	if (Definitions.IsValidIndex(NeuronIndex(ENeuronId::Threat)))
	{
		Definitions[NeuronIndex(ENeuronId::Threat)].Bias += Profile->ThreatBias;
	}
}
