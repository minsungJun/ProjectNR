// Neural Enemy AI prototype module.

#include "Characters/BossEnemyCharacter.h"

#include "Animation/AnimSequence.h"
#include "Characters/EnemyVisualSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/EnemySensorComponent.h"
#include "Components/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "NeuralAI.h"

namespace
{
	const TCHAR* PatternName(EBossPattern Pattern)
	{
		switch (Pattern)
		{
		case EBossPattern::Charge: return TEXT("charge");
		case EBossPattern::Sweep:  return TEXT("sweep");
		case EBossPattern::Slam:   return TEXT("slam");
		default:                   return TEXT("none");
		}
	}

	// NR.Boss.Pattern charge|sweep|slam: every boss in the game world starts that pattern now.
	FAutoConsoleCommandWithWorldAndArgs GBossPatternCommand(
		TEXT("NR.Boss.Pattern"),
		TEXT("NR.Boss.Pattern charge|sweep|slam - every boss starts that pattern now, ignoring distance and cooldowns."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.Num() == 0)
			{
				return;
			}
			const FString Name = Args[0].ToLower();
			const EBossPattern Pattern = Name == TEXT("charge") ? EBossPattern::Charge
				: Name == TEXT("sweep") ? EBossPattern::Sweep
				: Name == TEXT("slam") ? EBossPattern::Slam : EBossPattern::None;
			for (TActorIterator<ABossEnemyCharacter> It(World); It; ++It)
			{
				UE_LOG(LogNeuralAI, Log, TEXT("%s %s: %s"), *It->GetName(), *Name,
					It->StartPattern(Pattern) ? TEXT("started") : TEXT("could not start"));
			}
		}));
}

ABossEnemyCharacter::ABossEnemyCharacter()
{
	// The patterns run in Tick (the base enemy does not tick).
	PrimaryActorTick.bCanEverTick = true;

	// The golem model at scale 4 is about 345 cm wide, 400 long and 208 tall: the capsule covers
	// its body (the legs splay out past it) and stands it on the floor. UEnemyVisualSettings::Boss
	// puts the model's feet at the capsule bottom (Offset Z = -104).
	GetCapsuleComponent()->InitCapsuleSize(145.0f, 104.0f);

	// A little faster than a crawler (400): with its size it closes the gap quickly.
	MovementSpeed = 420.0f;
	AttackDamage = 30.0f;
	// The controller lets a close-in attack go inside this; PerformAttack picks slam or sweep.
	AttackRange = 650.0f;
	AttackHalfAngle = 70.0f;
	// Patterns pace themselves (PatternRecovery, cooldowns); this only stops the controller asking every tick.
	AttackInterval = 1.0f;
	// A_RockGolem_Slam: the front legs come down here.
	AttackImpactDelay = 0.6f;

	RoarAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Tripo/Characters/RockGolem_01/A_RockGolem_Roar.A_RockGolem_Roar")));
	ChargeAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Tripo/Characters/RockGolem_01/A_RockGolem_Charge.A_RockGolem_Charge")));
	SweepAnimation = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/Tripo/Characters/RockGolem_01/A_RockGolem_Sweep.A_RockGolem_Sweep")));

	if (HealthComponent)
	{
		HealthComponent->SetMaxHealth(1500.0f);
	}

	// Without the model (a checkout lacking the content) the placeholder cube is boss-sized too.
	if (BodyMesh)
	{
		BodyMesh->SetRelativeScale3D(FVector(3.2f, 2.8f, 1.9f));
		BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -10.0f));
	}
}

void ABossEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Missing content (it lives under Content/Tripo, outside the repository) leaves them null: the
	// patterns still run, without their animation.
	LoadedRoar = RoarAnimation.LoadSynchronous();
	LoadedCharge = ChargeAnimation.LoadSynchronous();
	LoadedSweep = SweepAnimation.LoadSynchronous();

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	NextChargeTime = Now + FirstChargeDelay;
}

const FEnemyVisual& ABossEnemyCharacter::GetVisual() const
{
	return GetDefault<UEnemyVisualSettings>()->Boss;
}

EBossPattern ABossEnemyCharacter::ChooseCloseInPattern(float Distance, float InSlamMaxDistance, bool bSlamReady)
{
	return (Distance <= InSlamMaxDistance && bSlamReady) ? EBossPattern::Slam : EBossPattern::Sweep;
}

bool ABossEnemyCharacter::ShouldCharge(float Distance, bool bTargetInSight, float MinDistance, float MaxDistance, bool bCooldownOver)
{
	return bTargetInSight && bCooldownOver && Distance >= MinDistance && Distance <= MaxDistance;
}

float ABossEnemyCharacter::GetTargetDistance2D() const
{
	const UEnemySensorComponent* Sensor = GetSensorComponent();
	const AActor* Target = Sensor ? Sensor->GetCurrentTarget() : nullptr;
	return Target ? FVector::Dist2D(Target->GetActorLocation(), GetActorLocation()) : TNumericLimits<float>::Max();
}

int32 ABossEnemyCharacter::PerformAttack()
{
	const UWorld* World = GetWorld();
	if (!World || IsAttackLocked() || World->GetTimeSeconds() < NextPatternTime)
	{
		return 0;
	}
	const EBossPattern Choice = ChooseCloseInPattern(GetTargetDistance2D(), SlamMaxDistance, World->GetTimeSeconds() >= NextSlamTime);
	return StartPattern(Choice) ? 1 : 0;
}

bool ABossEnemyCharacter::StartPattern(EBossPattern NewPattern)
{
	UWorld* World = GetWorld();
	if (!World || NewPattern == EBossPattern::None || IsDead() || IsStunned() || IsAttackLocked() || IsAttackWindingUp())
	{
		return false;
	}

	const float Now = World->GetTimeSeconds();
	const UEnemySensorComponent* Sensor = GetSensorComponent();
	const AActor* Target = Sensor ? Sensor->GetCurrentTarget() : nullptr;
	if (NewPattern == EBossPattern::Charge && !Target)
	{
		return false;
	}

	// Every pattern opens facing the target.
	if (Target)
	{
		const FVector To = Target->GetActorLocation() - GetActorLocation();
		SetActorRotation(FRotator(0.0f, To.Rotation().Yaw, 0.0f));
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	SavedMaxWalkSpeed = Movement->MaxWalkSpeed;
	SavedMaxAcceleration = Movement->MaxAcceleration;
	Movement->StopMovementImmediately();

	switch (NewPattern)
	{
	case EBossPattern::Slam:
		// The slam is the base attack: animation now, ReleaseAttack on the impact frame.
		if (Super::PerformAttack() == 0)
		{
			return false;
		}
		PatternEndTime = Now + SlamDuration;
		NextSlamTime = Now + SlamCooldown;
		break;

	case EBossPattern::Sweep:
		PlayLoopingAnimation(LoadedSweep, SweepDuration);
		Movement->MaxWalkSpeed = SweepSpeed;
		SweepSwingsDone = 0;
		PatternEndTime = Now + SweepDuration;
		break;

	case EBossPattern::Charge:
		PlayOneShotAnimation(LoadedRoar);
		ChargePhase = 0;
		ChargePhaseTime = Now;
		ChargeHit.Reset();
		PatternEndTime = Now + ChargeRoarTime + ChargeMaxTime + ChargeRecoverTime;
		NextChargeTime = Now + ChargeCooldown;
		break;

	default:
		return false;
	}

	Pattern = NewPattern;
	PatternStartTime = Now;
	UE_LOG(LogNeuralAI, Log, TEXT("%s boss pattern %s (target %.0f cm)"), *GetName(), PatternName(Pattern), GetTargetDistance2D());
	return true;
}

void ABossEnemyCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	if (Pattern != EBossPattern::None)
	{
		// Death or a stun ends it where it stands.
		if (IsDead() || IsStunned())
		{
			EndPattern(Now);
			return;
		}
		switch (Pattern)
		{
		case EBossPattern::Charge: TickCharge(Now, DeltaSeconds); break;
		case EBossPattern::Sweep:  TickSweep(Now, DeltaSeconds); break;
		default: break;
		}
		if (Pattern != EBossPattern::None && Now >= PatternEndTime)
		{
			EndPattern(Now);
		}
		return;
	}

	// The charge is the boss's own call: the brain's attack drive only rises close in, and this
	// is how it closes a long gap. Only when the controller has it and nothing else holds it.
	if (IsDead() || IsStunned() || !GetController() || Now < NextPatternTime || IsAttackWindingUp())
	{
		return;
	}
	const UEnemySensorComponent* Sensor = GetSensorComponent();
	if (Sensor && ShouldCharge(GetTargetDistance2D(), Sensor->IsTargetInSight(), ChargeMinDistance, ChargeMaxStartDistance, Now >= NextChargeTime))
	{
		StartPattern(EBossPattern::Charge);
	}
}

void ABossEnemyCharacter::TickCharge(float Now, float DeltaSeconds)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const UEnemySensorComponent* Sensor = GetSensorComponent();
	const AActor* Target = Sensor ? Sensor->GetCurrentTarget() : nullptr;

	if (ChargePhase == 0)
	{
		// Roaring: it keeps its eyes on the target, so where it runs is where the target is when the roar ends.
		if (Target)
		{
			const FVector To = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
			if (!To.IsNearlyZero())
			{
				ChargeDirection = To;
				SetActorRotation(FRotator(0.0f, To.Rotation().Yaw, 0.0f));
			}
		}
		if (Now - ChargePhaseTime >= ChargeRoarTime)
		{
			ChargePhase = 1;
			ChargePhaseTime = Now;
			Movement->MaxWalkSpeed = ChargeSpeed;
			// Up to speed in about a quarter of a second rather than the walk's two thirds.
			Movement->MaxAcceleration = FMath::Max(SavedMaxAcceleration, ChargeSpeed * 4.0f);
			PlayLoopingAnimation(LoadedCharge, ChargeMaxTime);
			UE_LOG(LogNeuralAI, Log, TEXT("%s charge: runs (target %.0f cm)"), *GetName(), GetTargetDistance2D());
		}
		return;
	}

	if (ChargePhase == 1)
	{
		AddMovementInput(ChargeDirection, 1.0f);
		SetActorRotation(FRotator(0.0f, ChargeDirection.Rotation().Yaw, 0.0f));

		const float Run = Now - ChargePhaseTime;
		const FVector Front = GetActorLocation() + ChargeDirection * (GetCapsuleComponent()->GetScaledCapsuleRadius() * 0.5f);
		const int32 Hits = DamageInArc(Front, ChargeDirection, ChargeHitRadius, 70.0f, AttackDamage * ChargeDamageScale, &ChargeHit, false);
		// Stopped by a wall once it had got going, or out of time.
		const bool bBlocked = Run > 0.3f && GetVelocity().Size2D() < ChargeSpeed * 0.3f;
		if (Hits > 0 || bBlocked || Run >= ChargeMaxTime)
		{
			UE_LOG(LogNeuralAI, Log, TEXT("%s charge: stops after %.2f s (%s)"), *GetName(), Run,
				Hits > 0 ? TEXT("hit") : bBlocked ? TEXT("blocked") : TEXT("time"));
			ChargePhase = 2;
			ChargePhaseTime = Now;
			Movement->MaxWalkSpeed = SavedMaxWalkSpeed;
			Movement->MaxAcceleration = SavedMaxAcceleration;
			Movement->StopMovementImmediately();
			// Back to the rest pose: it stands there, open.
			FinishOneShotAnimation();
			PatternEndTime = Now + ChargeRecoverTime;
		}
	}
}

void ABossEnemyCharacter::TickSweep(float Now, float DeltaSeconds)
{
	const UEnemySensorComponent* Sensor = GetSensorComponent();
	const AActor* Target = Sensor ? Sensor->GetCurrentTarget() : nullptr;

	// Turns after the target at a walk's pace, so it can be stepped round.
	if (Target)
	{
		const float Wanted = (Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw;
		const FRotator Current = GetActorRotation();
		SetActorRotation(FRotator(0.0f, FMath::FixedTurn(Current.Yaw, Wanted, SweepTurnRate * DeltaSeconds), 0.0f));
	}
	// Forward, slowly; not into the target's lap.
	if (GetTargetDistance2D() > SweepRange * 0.6f)
	{
		AddMovementInput(GetActorForwardVector().GetSafeNormal2D(), 1.0f);
	}

	if (SweepHitTimes.Num() == 0 || SweepCycle <= 0.0f)
	{
		return;
	}
	const float Elapsed = Now - PatternStartTime;
	const int32 PerCycle = SweepHitTimes.Num();
	const int32 Cycle = SweepSwingsDone / PerCycle;
	const float NextHit = Cycle * SweepCycle + SweepHitTimes[SweepSwingsDone % PerCycle];
	if (Elapsed >= NextHit && NextHit <= SweepDuration)
	{
		++SweepSwingsDone;
		const int32 Hits = DamageInArc(GetActorLocation(), GetActorForwardVector().GetSafeNormal2D(), SweepRange, SweepHalfAngle,
			AttackDamage * SweepDamageScale);
		UE_LOG(LogNeuralAI, Log, TEXT("%s sweep swing %d at %.2f s, %d hit"), *GetName(), SweepSwingsDone, Elapsed, Hits);
	}
}

void ABossEnemyCharacter::EndPattern(float Now)
{
	UE_LOG(LogNeuralAI, Log, TEXT("%s boss pattern %s ends after %.2f s"), *GetName(), PatternName(Pattern), Now - PatternStartTime);
	if (Pattern == EBossPattern::Sweep || Pattern == EBossPattern::Charge)
	{
		GetCharacterMovement()->MaxWalkSpeed = SavedMaxWalkSpeed;
		GetCharacterMovement()->MaxAcceleration = SavedMaxAcceleration;
	}
	Pattern = EBossPattern::None;
	ChargePhase = 0;
	NextPatternTime = Now + PatternRecovery;
}

int32 ABossEnemyCharacter::ReleaseAttack()
{
	// The model's size mid-slam: a wrong import of the animation shows up here as a collapse.
	if (const USkeletalMeshComponent* Model = GetMesh())
	{
		UE_LOG(LogTemp, Log, TEXT("%s slam impact: model %s, bounds %s"), *GetName(),
			Model->IsVisible() ? TEXT("visible") : TEXT("hidden"), *(Model->Bounds.BoxExtent * 2.0).ToString());
	}
	if (IsDead())
	{
		return 0;
	}
	const int32 Hits = DamageInArc(GetActorLocation(), GetActorForwardVector().GetSafeNormal2D(), SlamRadius, 180.0f,
		AttackDamage * SlamDamageScale);
	UE_LOG(LogNeuralAI, Log, TEXT("%s slam lands, %d hit within %.0f cm"), *GetName(), Hits, SlamRadius);
	return Hits;
}
