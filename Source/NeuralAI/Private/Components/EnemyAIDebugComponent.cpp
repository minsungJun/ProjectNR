// Neural Enemy AI prototype module.

#include "Components/EnemyAIDebugComponent.h"

#include "Components/EnemySensorComponent.h"
#include "Components/NeuralBrainComponent.h"
#include "Controllers/EnemyAIController.h"
#include "GameFramework/Pawn.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

/**
 * Master switch for AI debug output.
 *   0 - nothing
 *   1 - world shapes for everyone, on-screen text for the first few (default)
 *   2 - everything, for every enemy
 *
 * The middle setting exists because thirty enemies each printing a twelve-line readout
 * fills the screen several times over and makes the tool useless exactly when the load
 * is worth looking at.
 */
static TAutoConsoleVariable<int32> CVarDebug(
	TEXT("NeuralAI.Debug"),
	1,
	TEXT("0: off. 1: shapes for all, text for the first few. 2: everything."),
	ECVF_Default);

/** How many enemies may print their readout per frame at debug level 1. */
static TAutoConsoleVariable<int32> CVarDebugTextBudget(
	TEXT("NeuralAI.DebugTextBudget"),
	3,
	TEXT("Maximum enemies printing an on-screen readout per frame."),
	ECVF_Default);

namespace
{
	/**
	 * Shared per-frame allowance for on-screen readouts.
	 *
	 * Reset by whichever component ticks first in a new frame rather than by a central
	 * manager, which keeps the budget working without anything having to own it.
	 */
	uint64 GDebugTextFrame = 0;
	int32 GDebugTextRemaining = 0;

	bool ClaimDebugTextSlot(uint64 FrameCounter, int32 Budget)
	{
		if (GDebugTextFrame != FrameCounter)
		{
			GDebugTextFrame = FrameCounter;
			GDebugTextRemaining = Budget;
		}

		if (GDebugTextRemaining <= 0)
		{
			return false;
		}

		--GDebugTextRemaining;
		return true;
	}
}

UEnemyAIDebugComponent::UEnemyAIDebugComponent()
{
	// Every frame, not on the sensor interval: debug lines drawn with a zero lifetime
	// only survive one frame, so a slower tick would make them strobe.
	PrimaryComponentTick.bCanEverTick = true;
}

void UEnemyAIDebugComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const AActor* Owner = GetOwner())
	{
		Sensor = Owner->FindComponentByClass<UEnemySensorComponent>();
		Brain = Owner->FindComponentByClass<UNeuralBrainComponent>();
		DebugMessageKeyBase = static_cast<int32>(Owner->GetUniqueID() % 100000) * 32;
	}
}

void UEnemyAIDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if ENABLE_DRAW_DEBUG
	const int32 DebugLevel = CVarDebug.GetValueOnGameThread();
	if (DebugLevel <= 0)
	{
		return;
	}

	if (bDrawWorldShapes)
	{
		DrawWorldShapes();
	}

	if (bShowOnScreenValues)
	{
		const bool bUnlimited = DebugLevel >= 2;
		if (bUnlimited || ClaimDebugTextSlot(GFrameCounter, CVarDebugTextBudget.GetValueOnGameThread()))
		{
			DrawOnScreenValues();
		}
	}
#endif
}

#if ENABLE_DRAW_DEBUG

void UEnemyAIDebugComponent::DrawOnScreenValues()
{
	if (!GEngine || !Sensor)
	{
		return;
	}

	const FEnemySensorData& Data = Sensor->GetSensorData();

	FString Text = FString::Printf(
		TEXT("[%s]\n")
		TEXT("  SENSOR  Detected %.2f  Near %.2f  Closing %.2f\n")
		TEXT("          Front %.2f  Left %.2f  Right %.2f\n")
		TEXT("  THREAT  F %.2f  L %.2f  R %.2f\n")
		TEXT("  SELF    Pain %.2f  LowHealth %.2f"),
		*GetNameSafe(GetOwner()),
		Data.TargetDetected, Data.TargetNear, Data.TargetClosingSpeed,
		Data.TargetFront, Data.TargetLeft, Data.TargetRight,
		Data.ProjectileThreatFront, Data.ProjectileThreatLeft, Data.ProjectileThreatRight,
		Data.Pain, Data.LowHealth);

	if (Brain)
	{
		const FNeuralBrainOutput& Out = Brain->GetOutput();
		Text += FString::Printf(
			TEXT("\n  NEURAL  Threat %.2f  Aggr %.2f  Caution %.2f  Pursuit %.2f\n")
			TEXT("          Loom  F %.2f  L %.2f  R %.2f\n")
			TEXT("          Memory  Threat %.2f  Pain %.2f\n")
			TEXT("  OUTPUT  Appr %.2f  Retr %.2f  StrL %.2f  StrR %.2f\n")
			TEXT("          Atk %.2f  DodgeL %.2f  DodgeR %.2f\n")
			TEXT("  BRAIN   %d neurons  %d synapses  step %d"),
			Brain->GetActivation(ENeuronId::Threat),
			Brain->GetActivation(ENeuronId::Aggression),
			Brain->GetActivation(ENeuronId::Caution),
			Brain->GetActivation(ENeuronId::Pursuit),
			Brain->GetActivation(ENeuronId::LoomFront),
			Brain->GetActivation(ENeuronId::LoomLeft),
			Brain->GetActivation(ENeuronId::LoomRight),
			Brain->GetActivation(ENeuronId::ThreatMemory),
			Brain->GetActivation(ENeuronId::PainMemory),
			Out.Approach, Out.Retreat, Out.StrafeLeft, Out.StrafeRight,
			Out.Attack, Out.DodgeLeft, Out.DodgeRight,
			Brain->GetNeuronCount(), Brain->GetSynapseCount(), Brain->GetStepCount());
	}

	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const AEnemyAIController* Controller = Cast<AEnemyAIController>(Pawn->GetController()))
		{
			const UEnum* ActionEnum = StaticEnum<EEnemyActionType>();
			const FString ActionName = ActionEnum
				? ActionEnum->GetNameStringByValue(static_cast<int64>(Controller->GetCurrentAction()))
				: TEXT("?");
			Text += FString::Printf(LINE_TERMINATOR TEXT("  ACTION  %s"), *ActionName);
		}
	}

	GEngine->AddOnScreenDebugMessage(DebugMessageKeyBase, 0.0f, TextColor, Text);
}

void UEnemyAIDebugComponent::DrawWorldShapes()
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World || !Sensor)
	{
		return;
	}

	const FVector Origin = Owner->GetActorLocation();

	// Detection radius, flat on the ground so it reads as a range ring.
	DrawDebugCircle(World, Origin, Sensor->GetDetectionRange(), 48, FColor(60, 60, 200), false, -1.0f, 0, 2.0f,
		FVector(1, 0, 0), FVector(0, 1, 0), false);

	// Facing.
	DrawDebugDirectionalArrow(World, Origin, Origin + Owner->GetActorForwardVector() * 150.0f,
		40.0f, FColor::White, false, -1.0f, 0, 3.0f);

	// Bearing to the sensed target, length scaled by TargetNear so proximity is visible.
	if (const AActor* Target = Sensor->GetCurrentTarget())
	{
		const FEnemySensorData& Data = Sensor->GetSensorData();
		const FVector ToTarget = Target->GetActorLocation() - Origin;
		DrawDebugDirectionalArrow(World, Origin, Origin + ToTarget.GetSafeNormal() * (100.0f + 300.0f * Data.TargetNear),
			50.0f, FColor::Yellow, false, -1.0f, 0, 4.0f);
		DrawDebugLine(World, Origin, Target->GetActorLocation(), FColor(255, 200, 0, 80), false, -1.0f, 0, 1.0f);
	}

	// Where the blended neural drives are actually pushing the pawn this frame.
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		if (const AEnemyAIController* Controller = Cast<AEnemyAIController>(Pawn->GetController()))
		{
			const FVector Movement = Controller->GetDesiredMovement();
			if (!Movement.IsNearlyZero())
			{
				DrawDebugDirectionalArrow(World, Origin, Origin + Movement.GetSafeNormal() * (200.0f * Movement.Size()),
					60.0f, FColor::Cyan, false, -1.0f, 0, 5.0f);
			}
		}
	}

	// Incoming projectile: where it is, and where it is predicted to pass us.
	// If the sphere is drawn far off to one side, the shot is going to miss and the
	// threat value should be correspondingly small.
	if (const AActor* Threat = Sensor->GetMostThreateningProjectile())
	{
		const float ThreatValue = Sensor->GetHighestProjectileThreat();
		const FColor ThreatColor = FColor(255, static_cast<uint8>(255 * (1.0f - ThreatValue)), 0);

		DrawDebugLine(World, Origin, Threat->GetActorLocation(), ThreatColor, false, -1.0f, 0, 3.0f);
		DrawDebugSphere(World, Sensor->GetPredictedClosestApproachPoint(), 30.0f, 12, ThreatColor, false, -1.0f, 0, 2.0f);
	}
}

#else

void UEnemyAIDebugComponent::DrawOnScreenValues() {}
void UEnemyAIDebugComponent::DrawWorldShapes() {}

#endif
