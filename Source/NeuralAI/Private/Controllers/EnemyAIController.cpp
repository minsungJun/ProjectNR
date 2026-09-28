// Neural Enemy AI prototype module.

#include "Controllers/EnemyAIController.h"

#include "Characters/EnemyCharacter.h"
#include "Components/EnemySensorComponent.h"
#include "Components/NeuralBrainComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Components/StateTreeAIComponent.h"
#include "Data/EnemyAIProfile.h"
#include "StateTree.h"
#include "NeuralAI.h"

/** Freezes the pawn while leaving sensing and thinking running, for inspecting senses. */
static TAutoConsoleVariable<int32> CVarNeuralMovement(
	TEXT("NeuralAI.NeuralMovement"),
	1,
	TEXT("1: the enemy moves from its neural output. 0: the enemy holds position but keeps thinking."),
	ECVF_Default);

/**
 * Lets the approach axis fall back to a straight line, for comparing against pathing.
 * With it off the enemy behaves exactly as it did before pathfinding was added.
 */
static TAutoConsoleVariable<int32> CVarNeuralPathfinding(
	TEXT("NeuralAI.Pathfinding"),
	1,
	TEXT("1: Approach follows a navmesh path. 0: Approach heads straight at the target."),
	ECVF_Default);

/** Per-second dump of every value between sensing and acting. Off by default. */
static TAutoConsoleVariable<int32> CVarMovementDiagnostics(
	TEXT("NeuralAI.MovementDiagnostics"),
	0,
	TEXT("1: log the sense-think-act pipeline once per second."),
	ECVF_Default);

/** Per-dodge dump of every neuron input that produced it. Off by default: six lines a dodge. */
static TAutoConsoleVariable<int32> CVarDodgeDiagnostics(
	TEXT("NeuralAI.DodgeDiagnostics"),
	0,
	TEXT("1: log the full neural input breakdown each time a dodge fires."),
	ECVF_Default);

AEnemyAIController::AEnemyAIController()
{
	PrimaryActorTick.bCanEverTick = true;

	// Present but inert until a state tree asset is assigned to it. With no asset the
	// controller keeps driving the pipeline itself, so the neural slice is never gated
	// behind content that may not exist yet.
	StateTreeComponent = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeComponent"));

	// Started by hand in OnPossess once the asset is known. Left automatic it would try
	// to start at BeginPlay with no asset set and log an initialization failure.
	StateTreeComponent->SetStartLogicAutomatically(false);
}

void AEnemyAIController::HaltActions()
{
	DesiredMovement = FVector::ZeroVector;
	CurrentAction = EEnemyActionType::None;

	if (ControlledEnemy && ControlledEnemy->GetCharacterMovement())
	{
		ControlledEnemy->GetCharacterMovement()->StopMovementImmediately();
	}
}

void AEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	ControlledEnemy = Cast<AEnemyCharacter>(InPawn);
	if (!ControlledEnemy)
	{
		UE_LOG(LogNeuralAI, Warning, TEXT("%s possessed %s, which is not an AEnemyCharacter"),
			*GetNameSafe(this), *GetNameSafe(InPawn));
		return;
	}

	// Action gating numbers come from the same archetype asset the pawn uses, so a
	// profile change retunes thresholds and commit times without touching this class.
	if (const UEnemyAIProfile* Profile = ControlledEnemy->GetAIProfile())
	{
		AttackThreshold = Profile->AttackThreshold;
		DodgeThreshold = Profile->DodgeThreshold;
		AttackCommitTime = Profile->AttackCommitTime;
		DodgeCommitTime = Profile->DodgeCommitTime;
	}

	UStateTree* CombatStateTree = ControlledEnemy->GetCombatStateTree();
	if (CombatStateTree && StateTreeComponent)
	{
		StateTreeComponent->SetStateTree(CombatStateTree);
		StateTreeComponent->StartLogic();

		UE_LOG(LogNeuralAI, Log, TEXT("%s possessed %s, gated by state tree %s"),
			*GetNameSafe(this), *GetNameSafe(InPawn), *GetNameSafe(CombatStateTree));
	}
	else
	{
		// No asset assigned: keep driving the pipeline from Tick so the enemy still works.
		UE_LOG(LogNeuralAI, Log, TEXT("%s possessed %s, running without a state tree"),
			*GetNameSafe(this), *GetNameSafe(InPawn));
	}
}

void AEnemyAIController::OnUnPossess()
{
	ControlledEnemy = nullptr;
	DesiredMovement = FVector::ZeroVector;
	CurrentAction = EEnemyActionType::None;

	Super::OnUnPossess();
}

void AEnemyAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// When a state tree is running, it decides whether this frame is a fighting frame.
	if (bDrivenByStateTree)
	{
		return;
	}

	RunNeuralPipeline(DeltaTime);
}

void AEnemyAIController::RunNeuralPipeline(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World || !ControlledEnemy || ControlledEnemy->IsDead())
	{
		DesiredMovement = FVector::ZeroVector;
		CurrentAction = EEnemyActionType::None;
		return;
	}

	UEnemySensorComponent* Sensor = ControlledEnemy->GetSensorComponent();
	UNeuralBrainComponent* Brain = ControlledEnemy->GetNeuralBrain();
	if (!Sensor || !Brain)
	{
		UE_LOG(LogNeuralAI, Error, TEXT("%s has no %s; the enemy cannot act"),
			*GetNameSafe(ControlledEnemy), Sensor ? TEXT("brain") : TEXT("sensor"));
		return;
	}

	// Sense, then think, then act. This is the only place the three stages meet.
	Brain->Advance(DeltaTime, Sensor->GetSensorData());

	const FEnemySensorData& Data = Sensor->GetSensorData();
	const float ImmediateThreat = FMath::Max3(
		Data.ProjectileThreatFront, Data.ProjectileThreatLeft, Data.ProjectileThreatRight);

	const float Now = World->GetTimeSeconds();

	// Retire a finished commitment before anything asks whether it may act.
	if (CurrentAction != EEnemyActionType::None && !IsCommitted(Now))
	{
		CurrentAction = EEnemyActionType::None;
	}

	// A stunned enemy keeps sensing and thinking - memory should still accumulate while
	// it is helpless - but nothing it wants is allowed to reach the body.
	if (ControlledEnemy->IsStunned())
	{
		HaltActions();
		return;
	}

	UpdatePathToTarget(Now);

	// Dodge goes first: it is the only action allowed to pre-empt another.
	TryDodge(Now, ImmediateThreat);
	TryAttack(Now, ImmediateThreat);
	ApplyNeuralMovement(Now);
}

float AEnemyAIController::GetCommitTime(EEnemyActionType Action) const
{
	switch (Action)
	{
	case EEnemyActionType::Attack: return AttackCommitTime;
	case EEnemyActionType::Dodge:  return DodgeCommitTime;
	default:                       return 0.0f;
	}
}

bool AEnemyAIController::IsCommitted(float Now) const
{
	if (CurrentAction == EEnemyActionType::None)
	{
		return false;
	}
	return (Now - ActionStartTime) < GetCommitTime(CurrentAction);
}

void AEnemyAIController::BeginAction(EEnemyActionType Action, float Now)
{
	CurrentAction = Action;
	ActionStartTime = Now;
}

float AEnemyAIController::GetActionElapsed() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() - ActionStartTime : 0.0f;
}

void AEnemyAIController::TryDodge(float Now, float ImmediateThreat)
{
	if (Now - LastDodgeTime < DodgeCooldown)
	{
		return;
	}

	const FNeuralBrainOutput& Output = ControlledEnemy->GetNeuralBrain()->GetOutput();
	const float Strongest = FMath::Max(Output.DodgeLeft, Output.DodgeRight);
	if (Strongest < DodgeThreshold)
	{
		return;
	}

	// The drive says a dodge is wanted and which way; this says whether there is still
	// anything to dodge. Direction and desire stay with the network - this only refuses
	// to spend the burst and its cooldown on a projectile that has already gone by.
	if (ImmediateThreat < MinLiveThreatForDodge)
	{
		return;
	}

	// Commitment stops the enemy rethinking every step, but a shot that is genuinely
	// about to land has to be able to break it. Anything less urgent waits its turn.
	if (IsCommitted(Now) && ImmediateThreat < InterruptThreatThreshold)
	{
		return;
	}

	// The dodge drives came from ego-centric projectile bearings, so they are spent in
	// that same frame: the enemy's own right vector, not the bearing to the player.
	const FVector EnemyRight = ControlledEnemy->GetActorRightVector().GetSafeNormal2D();
	if (EnemyRight.IsNearlyZero())
	{
		return;
	}

	const bool bDodgeRight = Output.DodgeRight >= Output.DodgeLeft;
	const FVector DodgeDirection = bDodgeRight ? EnemyRight : -EnemyRight;

	// Override XY but leave Z alone, so a dodge is a sidestep rather than a hop.
	ControlledEnemy->LaunchCharacter(DodgeDirection * DodgeImpulse, true, false);

	LastDodgeTime = Now;
	BeginAction(EEnemyActionType::Dodge, Now);

	const UNeuralBrainComponent* Brain = ControlledEnemy->GetNeuralBrain();
	UE_LOG(LogNeuralAI, Log, TEXT("%s dodged %s (L=%.2f R=%.2f sensorThreat=%.2f)"),
		*GetNameSafe(ControlledEnemy), bDodgeRight ? TEXT("RIGHT") : TEXT("LEFT"),
		Output.DodgeLeft, Output.DodgeRight, ImmediateThreat);

	if (CVarDodgeDiagnostics.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogNeuralAI, Log, TEXT("    DodgeRight: %s"), *Brain->DescribeInputs(ENeuronId::DodgeRight));
		UE_LOG(LogNeuralAI, Log, TEXT("    DodgeLeft : %s"), *Brain->DescribeInputs(ENeuronId::DodgeLeft));
		UE_LOG(LogNeuralAI, Log, TEXT("    LoomFront : %s"), *Brain->DescribeInputs(ENeuronId::LoomFront));
		UE_LOG(LogNeuralAI, Log, TEXT("    LoomLeft  : %s"), *Brain->DescribeInputs(ENeuronId::LoomLeft));
		UE_LOG(LogNeuralAI, Log, TEXT("    LoomRight : %s"), *Brain->DescribeInputs(ENeuronId::LoomRight));
	}
}

void AEnemyAIController::TryAttack(float Now, float ImmediateThreat)
{
	if (Now - LastAttackTime < AttackCooldown || IsCommitted(Now))
	{
		return;
	}

	const UEnemySensorComponent* Sensor = ControlledEnemy->GetSensorComponent();
	const FNeuralBrainOutput& Output = ControlledEnemy->GetNeuralBrain()->GetOutput();

	if (Output.Attack < AttackThreshold)
	{
		return;
	}

	// The brain may want to attack from across the room. Whether that is sensible is
	// decided here, not there.
	const FEnemySensorData& Data = Sensor->GetSensorData();
	if (Data.TargetDetected <= 0.0f || Sensor->GetTargetDistance() > ControlledEnemy->GetAttackRange())
	{
		return;
	}

	ControlledEnemy->PerformAttack();

	LastAttackTime = Now;
	BeginAction(EEnemyActionType::Attack, Now);
}

void AEnemyAIController::ApplyNeuralMovement(float Now)
{
	DesiredMovement = FVector::ZeroVector;

	// A committed action owns the pawn. Feeding walk input during a dodge would brake
	// the burst before it travelled far enough to matter.
	if (IsCommitted(Now))
	{
		return;
	}

	const UEnemySensorComponent* Sensor = ControlledEnemy->GetSensorComponent();
	const UNeuralBrainComponent* Brain = ControlledEnemy->GetNeuralBrain();

	const FEnemySensorData& Data = Sensor->GetSensorData();
	const FNeuralBrainOutput& Output = Brain->GetOutput();
	// Approach follows the route; strafe and dodge stay immediate.
	const FVector TargetDirection = GetApproachDirection();

	// Strafe shares the ego frame with the sensory readings that drive it.
	const FVector EnemyRight = ControlledEnemy->GetActorRightVector().GetSafeNormal2D();

	const FVector Blended =
		TargetDirection * (Output.Approach - Output.Retreat) +
		EnemyRight * (Output.StrafeRight - Output.StrafeLeft);

	const float Magnitude = Blended.Size();
	FVector Direction = FVector::ZeroVector;
	bool bNavigable = false;
	if (Magnitude > KINDA_SMALL_NUMBER)
	{
		bNavigable = FindNavigableDirection(Blended / Magnitude, Direction);
	}

	if (CVarMovementDiagnostics.GetValueOnGameThread() != 0 && Now - LastDiagnosticTime > 1.0f)
	{
		LastDiagnosticTime = Now;
		UE_LOG(LogNeuralAI, Log,
			TEXT("PIPELINE near=%.2f dist=%.0f | appr=%.2f retr=%.2f atk=%.2f | threatMem=%.2f painMem=%.2f | mag=%.2f nav=%d"),
			Data.TargetNear, Sensor->GetTargetDistance(),
			Output.Approach, Output.Retreat, Output.Attack,
			Brain->GetActivation(ENeuronId::ThreatMemory), Brain->GetActivation(ENeuronId::PainMemory),
			Magnitude, bNavigable ? 1 : 0);
	}

	if (CVarNeuralMovement.GetValueOnGameThread() == 0 || Magnitude < MovementDeadzone || !bNavigable)
	{
		return;
	}

	DesiredMovement = Direction * Magnitude;
	CurrentAction = EEnemyActionType::Move;

	// Magnitude doubles as throttle: a weak Approach produces a slow walk.
	ControlledEnemy->AddMovementInput(Direction, FMath::Min(Magnitude, 1.0f));
}

bool AEnemyAIController::FindNavigableDirection(const FVector& Desired, FVector& OutDirection) const
{
	// Straight ahead first, then progressively wider deflections to either side.
	// Refusing to move at all near a navmesh edge looks like a broken brain, when in
	// fact the brain is fine and only the exact heading is unavailable.
	static const float Deflections[] = { 0.0f, 25.0f, -25.0f, 50.0f, -50.0f, 75.0f, -75.0f };

	for (const float Angle : Deflections)
	{
		const FVector Candidate = Desired.RotateAngleAxis(Angle, FVector::UpVector);
		if (IsDirectionNavigable(Candidate))
		{
			OutDirection = Candidate;
			return true;
		}
	}

	return false;
}

bool AEnemyAIController::IsDirectionNavigable(const FVector& Direction) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSystem || !ControlledEnemy)
	{
		// No navigation data: allow movement rather than freezing, which would look like
		// a broken brain instead of a missing navmesh.
		return true;
	}

	const FVector ProbePoint = ControlledEnemy->GetActorLocation() + Direction * NavProbeDistance;

	FNavLocation Projected;
	return NavSystem->ProjectPointToNavigation(ProbePoint, Projected, FVector(50.0f, 50.0f, 200.0f));
}

void AEnemyAIController::UpdatePathToTarget(float Now)
{
	if (Now - LastPathUpdateTime < PathUpdateInterval)
	{
		return;
	}
	LastPathUpdateTime = Now;

	const UEnemySensorComponent* Sensor = ControlledEnemy->GetSensorComponent();
	AActor* Target = Sensor ? Sensor->GetCurrentTarget() : nullptr;
	if (!Target)
	{
		CachedPathPoints.Reset();
		return;
	}

	UNavigationPath* Path = UNavigationSystemV1::FindPathToActorSynchronously(
		this, ControlledEnemy->GetActorLocation(), Target);

	// A partial path still helps: it walks the enemy as far towards the target as the
	// navmesh allows, which beats pushing straight into whatever is in the way.
	if (Path && Path->IsValid() && Path->PathPoints.Num() > 1)
	{
		CachedPathPoints = Path->PathPoints;
	}
	else
	{
		CachedPathPoints.Reset();
	}
}

FVector AEnemyAIController::GetApproachDirection() const
{
	const UEnemySensorComponent* Sensor = ControlledEnemy->GetSensorComponent();
	const FVector StraightLine = Sensor ? Sensor->GetTargetDirection() : FVector::ZeroVector;

	if (CVarNeuralPathfinding.GetValueOnGameThread() == 0 || CachedPathPoints.Num() == 0)
	{
		return StraightLine;
	}

	const FVector SelfLocation = ControlledEnemy->GetActorLocation();
	const float AcceptanceSquared = FMath::Square(PathPointAcceptance);

	// Skip index 0: UNavigationPath stores the query's start location there, which is
	// where this enemy stood when the path was last built. Between refreshes it walks up
	// to a quarter second past that point, leaving a stale marker behind it that clears
	// the acceptance radius and steers it backwards - and flips again on the next
	// refresh, which reads as the heading jittering back and forth.
	for (int32 Index = 1; Index < CachedPathPoints.Num(); ++Index)
	{
		FVector ToPoint = CachedPathPoints[Index] - SelfLocation;
		ToPoint.Z = 0.0f;

		if (ToPoint.SizeSquared() > AcceptanceSquared)
		{
			return ToPoint.GetSafeNormal();
		}
	}

	// Every corner is behind us: the target is essentially reached.
	return StraightLine;
}
