// Neural Enemy AI prototype module.

// Checks that each adaptation axis moves the brain's desires the way it says, from the brain
// alone (no world): a settled reading under a fixed sensor snapshot, adapted against neutral.
// Also that applying twice does not stack, neutral restores the circuit, and the strafe wiring
// mirrors with the side. Automation test NeuralAI.Brain.Adaptation.

#include "Components/NeuralBrainComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace NeuralBrainAdaptationTests
{
	UNeuralBrainComponent* MakeBrain(const FEnemyAdaptationProfile& Profile, bool bStrafeRight = true)
	{
		UNeuralBrainComponent* Brain = NewObject<UNeuralBrainComponent>(GetTransientPackage());
		Brain->InitializeForTest();
		Brain->SetStrafeSwapEnabled(false);
		Brain->SetStrafeSide(bStrafeRight);
		Brain->ApplyAdaptationProfile(Profile);
		return Brain;
	}

	/** Steps the brain three seconds under Sensor and returns what it then wants. */
	FNeuralBrainOutput Settle(UNeuralBrainComponent* Brain, const FEnemySensorData& Sensor)
	{
		for (int32 Step = 0; Step < 60; ++Step)
		{
			Brain->Advance(0.05f, Sensor);
		}
		return Brain->GetOutput();
	}

	/** The player close and in front, nothing incoming. */
	FEnemySensorData PlayerClose()
	{
		FEnemySensorData Sensor;
		Sensor.TargetDetected = 1.0f;
		Sensor.TargetNear = 0.8f;
		Sensor.TargetFront = 1.0f;
		return Sensor;
	}

	/** The player at mid range, a shot coming in from the left. */
	FEnemySensorData ShotFromLeft()
	{
		FEnemySensorData Sensor;
		Sensor.TargetDetected = 1.0f;
		Sensor.TargetNear = 0.4f;
		Sensor.TargetFront = 1.0f;
		Sensor.ProjectileThreatLeft = 0.5f;
		return Sensor;
	}

	FEnemyAdaptationProfile One(EAdaptationAxis Axis, float Value)
	{
		FEnemyAdaptationProfile Profile;
		Profile.Set(Axis, Value);
		return Profile;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNeuralBrainAdaptationTest, "NeuralAI.Brain.Adaptation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNeuralBrainAdaptationTest::RunTest(const FString& Parameters)
{
	using namespace NeuralBrainAdaptationTests;
	using A = EAdaptationAxis;
	const FEnemyAdaptationProfile Neutral;

	// Neutral is the circuit as authored: nothing ever drives the strafe neurons.
	const FNeuralBrainOutput Base = Settle(MakeBrain(Neutral), PlayerClose());
	const FNeuralBrainOutput BaseShot = Settle(MakeBrain(Neutral), ShotFromLeft());
	TestEqual(TEXT("Neutral: no strafe"), Base.StrafeLeft + Base.StrafeRight, 0.0f);
	AddInfo(FString::Printf(TEXT("Neutral close: approach %.2f retreat %.2f attack %.2f"), Base.Approach, Base.Retreat, Base.Attack));

	// Approach -1 closes in less; +1 more.
	const FNeuralBrainOutput LessApproach = Settle(MakeBrain(One(A::Approach, -1.0f)), PlayerClose());
	const FNeuralBrainOutput MoreApproach = Settle(MakeBrain(One(A::Approach, 1.0f)), PlayerClose());
	TestTrue(FString::Printf(TEXT("Approach -1 lowers approach (%.2f < %.2f)"), LessApproach.Approach, Base.Approach),
		LessApproach.Approach < Base.Approach - 0.1f);
	TestTrue(FString::Printf(TEXT("Approach +1 does not lower approach (%.2f >= %.2f)"), MoreApproach.Approach, Base.Approach),
		MoreApproach.Approach >= Base.Approach - KINDA_SMALL_NUMBER);

	// Retreat +1 backs off from a close player.
	const FNeuralBrainOutput Retreat = Settle(MakeBrain(One(A::Retreat, 1.0f)), PlayerClose());
	TestTrue(FString::Printf(TEXT("Retreat +1 raises retreat (%.2f > %.2f)"), Retreat.Retreat, Base.Retreat),
		Retreat.Retreat > Base.Retreat + 0.2f);

	// Aggression +1 wants to strike more. At mid range: close in, attack is already saturated.
	FEnemySensorData MidRange = PlayerClose();
	MidRange.TargetNear = 0.4f;
	const FNeuralBrainOutput BaseMid = Settle(MakeBrain(Neutral), MidRange);
	const FNeuralBrainOutput Aggressive = Settle(MakeBrain(One(A::Aggression, 1.0f)), MidRange);
	TestTrue(FString::Printf(TEXT("Aggression +1 raises attack (%.2f > %.2f)"), Aggressive.Attack, BaseMid.Attack),
		Aggressive.Attack > BaseMid.Attack + 0.05f);
	// Under fire it is held back less.
	const FNeuralBrainOutput AggressiveShot = Settle(MakeBrain(One(A::Aggression, 1.0f)), ShotFromLeft());
	TestTrue(FString::Printf(TEXT("Aggression +1 attacks more under fire (%.2f >= %.2f)"), AggressiveShot.Attack, BaseShot.Attack),
		AggressiveShot.Attack >= BaseShot.Attack);

	// Strafe +1 circles, on the side chosen, and only there.
	const FNeuralBrainOutput StrafeRight = Settle(MakeBrain(One(A::Strafe, 1.0f), true), PlayerClose());
	const FNeuralBrainOutput StrafeLeft = Settle(MakeBrain(One(A::Strafe, 1.0f), false), PlayerClose());
	TestTrue(FString::Printf(TEXT("Strafe +1 right side circles right (%.2f)"), StrafeRight.StrafeRight),
		StrafeRight.StrafeRight > 0.3f && StrafeRight.StrafeLeft <= KINDA_SMALL_NUMBER);
	TestTrue(FString::Printf(TEXT("Strafe +1 left side circles left (%.2f)"), StrafeLeft.StrafeLeft),
		StrafeLeft.StrafeLeft > 0.3f && StrafeLeft.StrafeRight <= KINDA_SMALL_NUMBER);

	// Dodge +1 breaks away from the shot harder.
	const FNeuralBrainOutput Dodgy = Settle(MakeBrain(One(A::Dodge, 1.0f)), ShotFromLeft());
	TestTrue(FString::Printf(TEXT("Dodge +1 raises the dodge away from the shot (%.2f > %.2f)"), Dodgy.DodgeRight, BaseShot.DodgeRight),
		Dodgy.DodgeRight > BaseShot.DodgeRight + 0.1f);

	// Caution +1: the same shot registers as more caution.
	UNeuralBrainComponent* CautiousBrain = MakeBrain(One(A::Caution, 1.0f));
	UNeuralBrainComponent* PlainBrain = MakeBrain(Neutral);
	Settle(CautiousBrain, ShotFromLeft());
	Settle(PlainBrain, ShotFromLeft());
	TestTrue(FString::Printf(TEXT("Caution +1 raises caution (%.2f > %.2f)"),
		CautiousBrain->GetActivation(ENeuronId::Caution), PlainBrain->GetActivation(ENeuronId::Caution)),
		CautiousBrain->GetActivation(ENeuronId::Caution) > PlainBrain->GetActivation(ENeuronId::Caution) + 0.05f);

	// Applying twice replaces, and neutral restores the authored weights.
	UNeuralBrainComponent* Brain = MakeBrain(Neutral);
	const float AuthoredWeight = Brain->GetSynapseWeight(ENeuronId::Aggression, ENeuronId::Approach);
	Brain->ApplyAdaptationProfile(One(A::Approach, -1.0f));
	const float Once = Brain->GetSynapseWeight(ENeuronId::Aggression, ENeuronId::Approach);
	Brain->ApplyAdaptationProfile(One(A::Approach, -1.0f));
	TestEqual(TEXT("Applying twice does not stack"), Brain->GetSynapseWeight(ENeuronId::Aggression, ENeuronId::Approach), Once);
	TestTrue(TEXT("Approach -1 halves Aggression->Approach"), FMath::IsNearlyEqual(Once, AuthoredWeight * 0.5f, 0.001f));
	Brain->ApplyAdaptationProfile(Neutral);
	TestEqual(TEXT("Neutral restores the authored weight"), Brain->GetSynapseWeight(ENeuronId::Aggression, ENeuronId::Approach), AuthoredWeight);
	TestEqual(TEXT("Neutral clears the adaptive bias"), Brain->GetAdaptiveBias(ENeuronId::Attack), 0.0f);

	// Swapping side moves the strafe wiring, not adds to it.
	Brain->ApplyAdaptationProfile(One(A::Strafe, 1.0f));
	Brain->SetStrafeSide(true);
	const float Right = Brain->GetSynapseWeight(ENeuronId::PlayerNear, ENeuronId::StrafeRight);
	Brain->SetStrafeSide(false);
	TestEqual(TEXT("Side swap moves PlayerNear->Strafe to the left"), Brain->GetSynapseWeight(ENeuronId::PlayerNear, ENeuronId::StrafeLeft), Right);
	TestEqual(TEXT("Side swap leaves nothing on the right"), Brain->GetSynapseWeight(ENeuronId::PlayerNear, ENeuronId::StrafeRight), 0.0f);
	return true;
}

#endif
