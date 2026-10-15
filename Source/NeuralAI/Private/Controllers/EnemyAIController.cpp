// Neural Enemy AI prototype module.

#include "Controllers/EnemyAIController.h"

#include "Characters/EnemyCharacter.h"
#include "Climb/EnemyClimbComponent.h"
#include "Climb/EnemyClimbLinkSubsystem.h"
#include "Climb/EnemyClimbNavTypes.h"
#include "Climb/EnemyClimbProbe.h"
#include "Climb/EnemyClimbSettings.h"
#include "NavMesh/NavMeshPath.h"
#include "Combat/AttackTokenSubsystem.h"
#include "Combat/PositionalSlotSettings.h"
#include "Combat/PositionalSlotSubsystem.h"
#include "Components/EnemySensorComponent.h"
#include "Components/HealthComponent.h"
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
	ReleaseSlotAndToken();
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
		ReleaseSlotAndToken();
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
		if (CurrentAction == EEnemyActionType::Dodge)
		{
			UE_LOG(LogNeuralAI, Log, TEXT("%s dodge carried it %.0f cm in %.2f s"), *GetNameSafe(ControlledEnemy),
				FVector::Dist2D(DodgeStartLocation, ControlledEnemy->GetActorLocation()), Now - ActionStartTime);
		}
		CurrentAction = EEnemyActionType::None;
	}

	// Before the stun check: a stunned enemy keeps its slot and goes back to it afterwards.
	UpdatePositionalSlot();

	// On the way up a wall the climb owns the body - no strikes, no dodges, no walking - until it
	// is over the top. Sensing, thinking and the slot carry on.
	if (const UEnemyClimbComponent* Climb = ControlledEnemy->GetClimbComponent(); Climb && Climb->IsClimbing())
	{
		DesiredMovement = FVector::ZeroVector;
		CurrentAction = EEnemyActionType::Climb;
		return;
	}

	// A stunned enemy keeps sensing and thinking - memory should still accumulate while
	// it is helpless - but nothing it wants is allowed to reach the body.
	if (ControlledEnemy->IsStunned())
	{
		HaltActions();
		return;
	}

	UpdatePathToTarget(Now);
	// Holding position (NeuralAI.NeuralMovement 0) holds off climbing too; a climb by command still runs.
	if (CVarNeuralMovement.GetValueOnGameThread() != 0 && (TryClimbAlongPath() || TryClimbOnDemand(Now)))
	{
		return;
	}

	// Dodge goes first: it is the only action allowed to pre-empt another.
	TryDodge(Now, ImmediateThreat);
	FaceTargetAtSlot(DeltaTime);
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

	if (UHealthComponent* Health = ControlledEnemy->GetHealthComponent())
	{
		Health->SetInvulnerableFor(DodgeInvulnerableTime);
	}

	LastDodgeTime = Now;
	DodgeStartLocation = ControlledEnemy->GetActorLocation();
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
	const float Interval = ControlledEnemy->GetAttackInterval() > 0.0f ? ControlledEnemy->GetAttackInterval() : AttackCooldown;
	if (Now - LastAttackTime < Interval || IsCommitted(Now))
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

	// Which of the enemies in reach strikes, and in what order, is the attack tokens' call. Where
	// the enemy stands (its positional slot) plays no part in it.
	// Shots from afar are paced by their own interval and do not crowd the target, so only
	// close-in strikes take turns.
	UAttackTokenSubsystem* Tokens = GetWorld()->GetSubsystem<UAttackTokenSubsystem>();
	if (ControlledEnemy->UsesAttackTokens() && Tokens && !Tokens->TryTakeToken(ControlledEnemy, Sensor->GetCurrentTarget()))
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

	FVector Blended =
		TargetDirection * (Output.Approach - Output.Retreat) +
		EnemyRight * (Output.StrafeRight - Output.StrafeLeft);

	// In its slot the enemy stops closing in: Approach would only push it into the target or a
	// neighbour. Backing off still works, and a little strafe keeps it from standing like a post.
	FVector SlotLocation;
	if (GetDrivingSlot(SlotLocation) && IsAtSlot(SlotLocation))
	{
		Blended = -Sensor->GetTargetDirection() * Output.Retreat
			+ EnemyRight * (Output.StrafeRight - Output.StrafeLeft) * GetDefault<UPositionalSlotSettings>()->ArrivedStrafeScale;
	}

	const float Magnitude = Blended.Size();
	FVector Direction = FVector::ZeroVector;
	bool bNavigable = false;
	if (Magnitude > KINDA_SMALL_NUMBER && IsAtWalkOffDrop())
	{
		// At the top of a drop its path goes down: off the edge, where the navmesh check sees none.
		Direction = Blended / Magnitude;
		bNavigable = true;
	}
	else if (Magnitude > KINDA_SMALL_NUMBER)
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

	const UEnemySensorComponent* Sensor = ControlledEnemy->GetSensorComponent();
	AActor* Target = Sensor ? Sensor->GetCurrentTarget() : nullptr;
	if (!Target)
	{
		CachedPathPoints.Reset();
		CachedClimbLinkIndex = INDEX_NONE;
		return;
	}

	// With a positional slot the route goes to the slot - round the target, not through it - and
	// is rebuilt only once the slot has moved on, not every update while the target stands still.
	FVector Goal;
	const bool bSlotGoal = GetDrivingSlot(Goal);
	if (bSlotGoal)
	{
		const UPositionalSlotSettings* Settings = GetDefault<UPositionalSlotSettings>();
		Goal = GetSteerPoint(Goal);
		if (CachedPathPoints.Num() > 0 && FVector::Dist2D(Goal, LastPathGoal) < Settings->RepathDistance
			&& Now - LastPathUpdateTime < Settings->MaxPathAge + PathUpdateInterval)
		{
			return;
		}
	}
	LastPathGoal = Goal;
	// Paths may go up walls (climb links); those that do not climb keep to the floor.
	const TSubclassOf<UNavigationQueryFilter> Filter = ControlledEnemy->CanClimb() ? nullptr : UNavFilter_EnemyNoClimb::StaticClass();
	UNavigationPath* Path = bSlotGoal
		? UNavigationSystemV1::FindPathToLocationSynchronously(this, ControlledEnemy->GetActorLocation(), Goal, nullptr, Filter)
		: UNavigationSystemV1::FindPathToActorSynchronously(this, ControlledEnemy->GetActorLocation(), Target, 50.0f, nullptr, Filter);

	// A partial path still helps: it walks the enemy as far towards the target as the
	// navmesh allows, which beats pushing straight into whatever is in the way.
	LastPathUpdateTime = Now;
	CachedClimbLinkIndex = INDEX_NONE;
	bCachedWalkOffDrop = false;
	bCachedPathPartial = Path && Path->IsPartial();
	CachedPathLength = Path && Path->IsValid() ? Path->GetPathLength() : 0.0f;
	if (Path && Path->IsValid() && Path->PathPoints.Num() > 1)
	{
		CachedPathPoints = Path->PathPoints;

		// Where the route first goes up a wall: a link point in the climb area.
		const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		const ANavigationData* NavData = Navigation ? Navigation->GetDefaultNavDataInstance() : nullptr;
		const int32 ClimbArea = NavData ? NavData->GetAreaID(UNavArea_Climb::StaticClass()) : INDEX_NONE;
		const int32 ClimbDownArea = NavData ? NavData->GetAreaID(UNavArea_ClimbDown::StaticClass()) : INDEX_NONE;
		const TArray<FNavPathPoint>& Points = Path->GetPath()->GetPathPoints();
		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			const FNavMeshNodeFlags Flags(Points[Index].Flags);
			if (Flags.IsNavLink() && (Flags.Area == ClimbArea || Flags.Area == ClimbDownArea))
			{
				CachedClimbLinkIndex = Index;
				bCachedClimbLinkDown = Flags.Area == ClimbDownArea;
				bCachedWalkOffDrop = bCachedClimbLinkDown && !ControlledEnemy->CanClimb();
				break;
			}
		}
	}
	else
	{
		CachedPathPoints.Reset();
	}
}

bool AEnemyAIController::HasOwnSlot() const
{
	const UPositionalSlotSubsystem* Slots = GetWorld() ? GetWorld()->GetSubsystem<UPositionalSlotSubsystem>() : nullptr;
	FVector Location;
	bool bHasSlot = false;
	return Slots && ControlledEnemy && Slots->GetStandPoint(ControlledEnemy, Location, bHasSlot) && bHasSlot;
}

bool AEnemyAIController::IsTargetElevated() const
{
	const UEnemySensorComponent* Sensor = ControlledEnemy ? ControlledEnemy->GetSensorComponent() : nullptr;
	const AActor* Target = Sensor ? Sensor->GetCurrentTarget() : nullptr;
	if (!Target)
	{
		return false;
	}
	const float TargetFeet = Target->GetActorLocation().Z - Target->GetSimpleCollisionHalfHeight();
	const float OwnFeet = ControlledEnemy->GetActorLocation().Z - ControlledEnemy->GetSimpleCollisionHalfHeight();
	return TargetFeet - OwnFeet >= GetDefault<UEnemyClimbSettings>()->ElevatedTargetHeight;
}

bool AEnemyAIController::TryClimbOnDemand(float Now)
{
	const UEnemyClimbSettings* Settings = GetDefault<UEnemyClimbSettings>();
	UEnemyClimbComponent* Climb = ControlledEnemy->GetClimbComponent();
	const UEnemySensorComponent* Sensor = ControlledEnemy->GetSensorComponent();
	AActor* Target = Sensor ? Sensor->GetCurrentTarget() : nullptr;
	if (!Target || !Climb || Climb->IsClimbing() || !ControlledEnemy->CanClimb() || Now - LastOnDemandClimbCheck < Settings->OnDemandInterval)
	{
		return false;
	}

	// A shooter with a place to shoot from stays there: it climbs only on its way to the target
	// itself, when no ranged slot has the target in sight.
	if (ControlledEnemy->GetPreferredSlotRing() != 0 && HasOwnSlot())
	{
		return false;
	}

	// Only two reasons, and a path that already goes up a link needs neither.
	const bool bUnreachable = CachedPathPoints.Num() == 0 || bCachedPathPartial;
	const bool bShortcut = !bUnreachable && CachedClimbLinkIndex == INDEX_NONE && IsTargetElevated();
	if (!bUnreachable && !bShortcut)
	{
		return false;
	}
	LastOnDemandClimbCheck = Now;

	// Walls ahead, towards the target and up to 60 degrees either side, each weighed as: walk to its
	// foot, the climb (dearer than walking), and the route from its top to the target.
	UWorld* World = GetWorld();
	const FVector From = ControlledEnemy->GetActorLocation();
	const FVector Toward = (Target->GetActorLocation() - From).GetSafeNormal2D();
	FEnemyClimbLedge Best;
	float BestRoute = TNumericLimits<float>::Max();
	for (const float Angle : { 0.0f, 30.0f, -30.0f, 60.0f, -60.0f })
	{
		FEnemyClimbLedge Ledge;
		if (FEnemyClimbProbe::ProbeDirection(World, ControlledEnemy, Toward.RotateAngleAxis(Angle, FVector::UpVector), *Settings, Ledge) != EEnemyClimbProbeResult::Ok
			|| Climb->IsWallExcluded(Ledge.Wall.Get()))
		{
			continue;
		}
		UNavigationPath* FromTop = UNavigationSystemV1::FindPathToLocationSynchronously(this, Ledge.StandCenter, Target->GetActorLocation());
		if (!FromTop || !FromTop->IsValid() || FromTop->IsPartial())
		{
			continue;
		}
		const float Route = Ledge.GetDistanceTo(From) + Ledge.GetHeight() * Settings->ClimbCostFactor + FromTop->GetPathLength();
		if (Route < BestRoute)
		{
			BestRoute = Route;
			Best = Ledge;
		}
	}
	if (BestRoute == TNumericLimits<float>::Max() || (bShortcut && BestRoute >= CachedPathLength * Settings->ShortcutGain))
	{
		return false;
	}
	if (!Climb->StartClimb(Best))
	{
		return false;
	}
	UE_LOG(LogNeuralAI, Log, TEXT("%s climbs on demand (%s): route %.0f cm against a path of %.0f cm"), *GetNameSafe(ControlledEnemy),
		bUnreachable ? TEXT("no path reaches") : TEXT("shorter than walking round"), BestRoute, CachedPathLength);
	CachedPathPoints.Reset();
	CachedClimbLinkIndex = INDEX_NONE;
	LastPathUpdateTime = -1000.0f;
	DesiredMovement = FVector::ZeroVector;
	CurrentAction = EEnemyActionType::Climb;
	return true;
}

bool AEnemyAIController::IsAtWalkOffDrop() const
{
	if (!bCachedWalkOffDrop || !CachedPathPoints.IsValidIndex(CachedClimbLinkIndex))
	{
		return false;
	}
	const FVector Feet = ControlledEnemy->GetActorLocation() - FVector(0.0f, 0.0f, ControlledEnemy->GetSimpleCollisionHalfHeight());
	const FVector Top = CachedPathPoints[CachedClimbLinkIndex];
	return FVector::Dist2D(Feet, Top) <= GetDefault<UEnemyClimbSettings>()->LinkTriggerDistance && FMath::Abs(Feet.Z - Top.Z) <= 100.0f;
}

bool AEnemyAIController::TryClimbAlongPath()
{
	if (CachedClimbLinkIndex == INDEX_NONE || CachedClimbLinkIndex + 1 >= CachedPathPoints.Num() || !ControlledEnemy->CanClimb())
	{
		return false;
	}
	UEnemyClimbComponent* Climb = ControlledEnemy->GetClimbComponent();
	const UEnemyClimbLinkSubsystem* Links = GetWorld()->GetSubsystem<UEnemyClimbLinkSubsystem>();
	const FVector Start = CachedPathPoints[CachedClimbLinkIndex];
	const FVector End = CachedPathPoints[CachedClimbLinkIndex + 1];
	const FVector Feet = ControlledEnemy->GetActorLocation() - FVector(0.0f, 0.0f, ControlledEnemy->GetSimpleCollisionHalfHeight());
	if (!Climb || Climb->IsClimbing() || !Links
		|| FVector::Dist2D(Feet, Start) > GetDefault<UEnemyClimbSettings>()->LinkTriggerDistance || FMath::Abs(Feet.Z - Start.Z) > 100.0f)
	{
		return false;
	}

	FEnemyClimbLedge Ledge;
	if (!Links->FindLedge(Start, End, Ledge, 60.0f, bCachedClimbLinkDown))
	{
		UE_LOG(LogNeuralAI, Warning, TEXT("%s: path goes %s a climb link at %s with no climb known there"),
			*GetNameSafe(ControlledEnemy), bCachedClimbLinkDown ? TEXT("down") : TEXT("up"), *Start.ToCompactString());
		CachedClimbLinkIndex = INDEX_NONE;
		return false;
	}
	// Down: a low drop is stepped off, a high one climbed down.
	const bool bStarted = !bCachedClimbLinkDown ? Climb->StartClimb(Ledge)
		: Ledge.GetHeight() < GetDefault<UEnemyClimbSettings>()->ClimbDownMinHeight ? Climb->StartDrop(Ledge)
		: Climb->StartClimbDown(Ledge);
	if (!bStarted)
	{
		return false;
	}
	// Over the top the route is planned afresh.
	CachedPathPoints.Reset();
	CachedClimbLinkIndex = INDEX_NONE;
	LastPathUpdateTime = -1000.0f;
	DesiredMovement = FVector::ZeroVector;
	CurrentAction = EEnemyActionType::Climb;
	return true;
}

FVector AEnemyAIController::GetApproachDirection() const
{
	const UEnemySensorComponent* Sensor = ControlledEnemy->GetSensorComponent();
	FVector StraightLine = Sensor ? Sensor->GetTargetDirection() : FVector::ZeroVector;
	FVector SlotLocation;
	if (GetDrivingSlot(SlotLocation))
	{
		StraightLine = (GetSteerPoint(SlotLocation) - ControlledEnemy->GetActorLocation()).GetSafeNormal2D();
	}

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

void AEnemyAIController::UpdatePositionalSlot()
{
	UPositionalSlotSubsystem* Slots = GetWorld() ? GetWorld()->GetSubsystem<UPositionalSlotSubsystem>() : nullptr;
	if (!Slots || !ControlledEnemy)
	{
		return;
	}
	const UEnemySensorComponent* Sensor = ControlledEnemy->GetSensorComponent();
	AActor* Target = Sensor ? Sensor->GetCurrentTarget() : nullptr;
	if (Slots->IsAssigning() && Target && !Sensor->IsDormant())
	{
		// The brain does not pick the slot, but what it wants tilts the choice: closing in favours
		// near slots, strafing the neighbour on that side, caution the target's flanks.
		FPositionalSlotAssigner::FIntent Intent;
		if (const UNeuralBrainComponent* Brain = ControlledEnemy->GetNeuralBrain())
		{
			const FNeuralBrainOutput& Output = Brain->GetOutput();
			Intent.Approach = Output.Approach;
			Intent.Strafe = Output.StrafeRight - Output.StrafeLeft;
			Intent.Caution = Brain->GetActivation(ENeuronId::Caution);
		}
		Slots->RequestSlot(ControlledEnemy, Target, Intent, ControlledEnemy->GetPreferredSlotRing());
	}
	else
	{
		Slots->ReleaseSlot(ControlledEnemy, TEXT("target lost or dormant"));
	}
}

bool AEnemyAIController::GetDrivingSlot(FVector& OutLocation) const
{
	const UPositionalSlotSubsystem* Slots = GetWorld() ? GetWorld()->GetSubsystem<UPositionalSlotSubsystem>() : nullptr;
	bool bHasSlot = false;
	// Without a slot of its own the enemy still has a place: its hold point, out of the way.
	if (!Slots || !ControlledEnemy || !Slots->IsDriving() || !Slots->GetStandPoint(ControlledEnemy, OutLocation, bHasSlot))
	{
		return false;
	}
	// Except with the target up high and no slot up there: one that climbs goes for the target
	// itself, up whatever is in the way, rather than waiting at the foot.
	return bHasSlot || !ControlledEnemy->CanClimb() || !IsTargetElevated();
}

FVector AEnemyAIController::GetSteerPoint(const FVector& Goal) const
{
	const UEnemySensorComponent* Sensor = ControlledEnemy ? ControlledEnemy->GetSensorComponent() : nullptr;
	const AActor* Target = Sensor ? Sensor->GetCurrentTarget() : nullptr;
	if (!Target)
	{
		return Goal;
	}
	const UPositionalSlotSettings* Settings = GetDefault<UPositionalSlotSettings>();
	const FVector Center = Target->GetActorLocation();
	const FVector From = ControlledEnemy->GetActorLocation();

	// Up on a platform (or down from one) the way there is the navmesh's, not round the ring.
	if (FMath::Abs(Goal.Z - From.Z) > Settings->OrbitMaxHeightGap)
	{
		return Goal;
	}

	// Lined up with the goal, seen from the target: straight in, between the neighbouring slots.
	const float FromBearing = FMath::RadiansToDegrees(FMath::Atan2(From.Y - Center.Y, From.X - Center.X));
	const float GoalBearing = FMath::RadiansToDegrees(FMath::Atan2(Goal.Y - Center.Y, Goal.X - Center.X));
	const float Delta = FMath::FindDeltaAngleDegrees(FromBearing, GoalBearing);
	if (FMath::Abs(Delta) <= Settings->OrbitAlignDegrees)
	{
		return Goal;
	}

	// Further round: step round the shorter way, outside the ring, clear of the enemies standing in
	// it; not swinging wider than half as much again.
	const float Step = FMath::Clamp(Delta, -Settings->OrbitStepDegrees, Settings->OrbitStepDegrees);
	const float Bearing = FMath::DegreesToRadians(FromBearing + Step);
	const float Outside = Settings->GetRingRadius(ControlledEnemy->GetPreferredSlotRing()) + Settings->OrbitMargin;
	const float Ring = FMath::Clamp(FVector::Dist2D(From, Center), Outside, Outside * 1.5f);
	return FVector(Center.X + FMath::Cos(Bearing) * Ring, Center.Y + FMath::Sin(Bearing) * Ring, Goal.Z);
}

void AEnemyAIController::ReleaseSlotAndToken()
{
	UWorld* World = GetWorld();
	if (!World || !ControlledEnemy)
	{
		return;
	}
	if (UPositionalSlotSubsystem* Slots = World->GetSubsystem<UPositionalSlotSubsystem>())
	{
		Slots->ReleaseSlot(ControlledEnemy, TEXT("died or unpossessed"));
	}
	if (UAttackTokenSubsystem* Tokens = World->GetSubsystem<UAttackTokenSubsystem>())
	{
		Tokens->Release(ControlledEnemy);
	}
}

bool AEnemyAIController::IsAtSlot(const FVector& SlotLocation) const
{
	return ControlledEnemy
		&& FVector::Dist2D(ControlledEnemy->GetActorLocation(), SlotLocation) <= GetDefault<UPositionalSlotSettings>()->ArrivalRadius;
}

void AEnemyAIController::FaceTargetAtSlot(float DeltaTime)
{
	FVector SlotLocation;
	const UEnemySensorComponent* Sensor = ControlledEnemy ? ControlledEnemy->GetSensorComponent() : nullptr;
	if (!Sensor || !GetDrivingSlot(SlotLocation) || !IsAtSlot(SlotLocation) || Sensor->GetTargetDirection().IsNearlyZero())
	{
		return;
	}
	const FRotator Current = ControlledEnemy->GetActorRotation();
	const FRotator Wanted(0.0f, Sensor->GetTargetDirection().Rotation().Yaw, 0.0f);
	ControlledEnemy->SetActorRotation(FMath::RInterpConstantTo(Current, Wanted, DeltaTime,
		GetDefault<UPositionalSlotSettings>()->FaceTurnRate));
}
