// Neural Enemy AI prototype module.

#include "Components/EnemySensorComponent.h"

#include "Components/HealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Data/EnemyAIProfile.h"
#include "Kismet/GameplayStatics.h"
#include "Projectiles/AITestProjectile.h"
#include "NeuralAI.h"

TArray<TWeakObjectPtr<UEnemySensorComponent>> UEnemySensorComponent::ActiveSensors;

namespace
{
	/** Projects onto the ground plane and normalizes. The crawler fights in 2D. */
	FVector FlattenAndNormalize(const FVector& In)
	{
		FVector Flat(In.X, In.Y, 0.0f);
		return Flat.GetSafeNormal();
	}
}

UEnemySensorComponent::UEnemySensorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = SensorInterval;
}

void UEnemySensorComponent::BeginPlay()
{
	Super::BeginPlay();

	ActiveSensors.Add(this);

	PrimaryComponentTick.TickInterval = SensorInterval;
	if (bStartDormant)
	{
		SetDormant(true);
	}

	if (AActor* Owner = GetOwner())
	{
		OwnerHealth = Owner->FindComponentByClass<UHealthComponent>();
		if (OwnerHealth)
		{
			OwnerHealth->OnDamaged.AddDynamic(this, &UEnemySensorComponent::HandleOwnerDamaged);
		}
		else
		{
			UE_LOG(LogNeuralAI, Warning, TEXT("%s has a sensor but no HealthComponent; Pain and LowHealth will stay at 0"),
				*GetNameSafe(Owner));
		}
	}
}

void UEnemySensorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ActiveSensors.RemoveAllSwap([this](const TWeakObjectPtr<UEnemySensorComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == this;
	});

	Super::EndPlay(EndPlayReason);
}

void UEnemySensorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateSensorData(DeltaTime);
}

void UEnemySensorComponent::UpdateSensorData(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_NeuralAI_SensorUpdate);

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FVector SelfLocation = Owner->GetActorLocation();
	const FVector Forward = FlattenAndNormalize(Owner->GetActorForwardVector());
	const FVector Right = FlattenAndNormalize(Owner->GetActorRightVector());

	UpdateTargetSenses(SelfLocation, Forward, Right);
	UpdateProjectileSenses(SelfLocation, Owner->GetVelocity(), Forward, Right);
	UpdateShotSense(DeltaTime);
	UpdateSelfSenses(DeltaTime);
}

void UEnemySensorComponent::UpdateTargetSenses(const FVector& SelfLocation, const FVector& Forward, const FVector& Right)
{
	// Dormant: behave exactly as if nothing were in sight and no ally had called.
	if (bDormant)
	{
		AlertTarget = nullptr;
	}
	AActor* Target = bDormant ? nullptr : AcquireTarget(SelfLocation);
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;

	// A fresh sighting is news worth passing on; a continuing one was already passed on.
	const bool bSeesDirectly = Target != nullptr;
	if (bSeesDirectly && !bSawTargetDirectly)
	{
		BroadcastAlert(Target);
	}
	bSawTargetDirectly = bSeesDirectly;
	if (bSeesDirectly)
	{
		LastSeenTarget = Target;
		LastSeenTime = Now;
		LastSeenLocation = Target->GetActorLocation();
	}
	else if (!bDormant && LastSeenTarget.IsValid())
	{
		if (Now - LastSeenTime <= SightGraceTime)
		{
			// Just gone behind something: still counts as seen for a moment.
			Target = LastSeenTarget.Get();
		}
		else
		{
			// Gone for good: all that is left is where it was, to go and look.
			AlertTarget = LastSeenTarget;
			AlertLocation = LastSeenLocation;
			AlertExpireTime = Now + AlertDuration;
			LastSeenTarget = nullptr;
		}
	}

	// Nothing in sight: fall back to where an ally saw it, or it was heard or last seen, while that
	// is fresh and within chase range - the place, not the actor, which may have moved on behind a
	// wall since. Past that, or once there with nothing to see, the enemy goes back to idle.
	bSensingByAlert = false;
	if (!Target)
	{
		AActor* Alerted = AlertTarget.Get();
		if (World && Alerted && Now < AlertExpireTime
			&& FVector::DistSquared2D(SelfLocation, AlertLocation) <= FMath::Square(AlertChaseRange)
			&& FVector::DistSquared2D(SelfLocation, AlertLocation) > FMath::Square(SearchGiveUpRadius))
		{
			Target = Alerted;
			bSensingByAlert = true;
		}
		else
		{
			AlertTarget = nullptr;
		}
	}

	CurrentTarget = Target;

	if (!Target)
	{
		SensorData.TargetDetected = 0.0f;
		SensorData.TargetNear = 0.0f;
		SensorData.TargetFront = 0.0f;
		SensorData.TargetLeft = 0.0f;
		SensorData.TargetRight = 0.0f;
		SensorData.TargetClosingSpeed = 0.5f;
		TargetDirection = FVector::ZeroVector;
		TargetDistance = 0.0f;
		bHasPreviousTargetLocation = false;
		return;
	}

	const FVector TargetLocation = bSensingByAlert ? AlertLocation : Target->GetActorLocation();
	KnownTargetLocation = TargetLocation;
	const FVector ToTarget(TargetLocation.X - SelfLocation.X, TargetLocation.Y - SelfLocation.Y, 0.0f);
	const float Distance = ToTarget.Size();
	const FVector ToTargetDir = ToTarget.GetSafeNormal();

	TargetDirection = ToTargetDir;
	TargetDistance = Distance;

	SensorData.TargetDetected = 1.0f;

	// Linear falloff: 1 when touching, 0 at the edge of perception - which, for a target known
	// only from an alert, is the chase range rather than our own eyesight.
	const float NearRange = bSensingByAlert ? AlertChaseRange : DetectionRange;
	SensorData.TargetNear = FMath::Clamp(1.0f - (Distance / NearRange), 0.0f, 1.0f);

	// Ego-centric bearing. A target at 45 degrees lights up Front and one side together,
	// which is what lets the brain blend approach with a strafe instead of snapping.
	SensorData.TargetFront = FMath::Clamp(FVector::DotProduct(Forward, ToTargetDir), 0.0f, 1.0f);
	const float RightDot = FVector::DotProduct(Right, ToTargetDir);
	SensorData.TargetRight = FMath::Clamp(RightDot, 0.0f, 1.0f);
	SensorData.TargetLeft = FMath::Clamp(-RightDot, 0.0f, 1.0f);

	// Closing speed from the target's own motion along the line between us.
	// Signed rate folded into 0..1 with 0.5 meaning "gap is holding steady".
	float ClosingRate = 0.0f;
	if (bHasPreviousTargetLocation && !ToTargetDir.IsNearlyZero())
	{
		const FVector TargetVelocity = (TargetLocation - PreviousTargetLocation) / FMath::Max(SensorInterval, KINDA_SMALL_NUMBER);
		const FVector FlatVelocity(TargetVelocity.X, TargetVelocity.Y, 0.0f);
		// Negative dot means the target is moving back down the line towards us.
		ClosingRate = -FVector::DotProduct(FlatVelocity, ToTargetDir);
	}
	PreviousTargetLocation = TargetLocation;
	bHasPreviousTargetLocation = true;

	SensorData.TargetClosingSpeed =
		0.5f + 0.5f * FMath::Clamp(ClosingRate / MaxClosingSpeed, -1.0f, 1.0f);
}

void UEnemySensorComponent::UpdateProjectileSenses(const FVector& SelfLocation, const FVector& SelfVelocity,
	const FVector& Forward, const FVector& Right)
{
	SCOPE_CYCLE_COUNTER(STAT_NeuralAI_ProjectileThreat);
	SET_DWORD_STAT(STAT_NeuralAI_LiveProjectiles, AAITestProjectile::GetActiveProjectiles().Num());

	SensorData.ProjectileThreatFront = 0.0f;
	SensorData.ProjectileThreatLeft = 0.0f;
	SensorData.ProjectileThreatRight = 0.0f;
	MostThreateningProjectile = nullptr;
	HighestProjectileThreat = 0.0f;

	const float SenseRangeSquared = FMath::Square(ProjectileSenseRange);

	for (const TWeakObjectPtr<AAITestProjectile>& Entry : AAITestProjectile::GetActiveProjectiles())
	{
		const AAITestProjectile* Projectile = Entry.Get();
		if (!Projectile)
		{
			continue;
		}

		const FVector ProjectileLocation = Projectile->GetActorLocation();

		// Relative position and velocity, flattened: the crawler dodges sideways, so the
		// vertical component of a shot only matters through whether it will actually hit.
		FVector RelativePosition = ProjectileLocation - SelfLocation;
		RelativePosition.Z = 0.0f;

		if (RelativePosition.SizeSquared() > SenseRangeSquared)
		{
			continue;
		}

		FVector RelativeVelocity = Projectile->GetProjectileVelocity() - SelfVelocity;
		RelativeVelocity.Z = 0.0f;

		const float RelativeSpeedSquared = RelativeVelocity.SizeSquared();
		if (RelativeSpeedSquared < KINDA_SMALL_NUMBER)
		{
			// Drifting together: it will never get any closer than it already is.
			continue;
		}

		// Time at which the gap is smallest, from d/dt |R + Vr*t|^2 = 0.
		// Negative means that moment is in the past: the projectile is opening the
		// distance, so it is harmless no matter how close it currently sits.
		const float TimeToClosestApproach =
			-FVector::DotProduct(RelativePosition, RelativeVelocity) / RelativeSpeedSquared;

		if (TimeToClosestApproach <= 0.0f || TimeToClosestApproach > ProjectileThreatMaxTime)
		{
			continue;
		}

		const FVector ClosestApproachOffset = RelativePosition + RelativeVelocity * TimeToClosestApproach;
		const float ClosestApproachDistance = ClosestApproachOffset.Size();

		// Will it actually come near us? A shot screaming past at 10m is not a threat.
		const float MissFactor = 1.0f - FMath::Clamp(ClosestApproachDistance / ProjectileMissRadius, 0.0f, 1.0f);
		if (MissFactor <= 0.0f)
		{
			continue;
		}

		// How soon? Imminent impacts dominate distant ones.
		const float TimeFactor = 1.0f - FMath::Clamp(TimeToClosestApproach / ProjectileThreatMaxTime, 0.0f, 1.0f);

		const float Threat = FMath::Clamp(MissFactor * TimeFactor, 0.0f, 1.0f);
		if (Threat <= 0.0f)
		{
			continue;
		}

		// Split by the bearing the shot is coming from, so the brain can dodge away from it.
		const FVector ThreatDirection = RelativePosition.GetSafeNormal();
		const float FrontDot = FVector::DotProduct(Forward, ThreatDirection);
		const float RightDot = FVector::DotProduct(Right, ThreatDirection);

		// Max rather than sum: two shots from the left are not twice as dodgeable, and
		// summing would push the sensor past its 0..1 contract.
		SensorData.ProjectileThreatFront =
			FMath::Max(SensorData.ProjectileThreatFront, Threat * FMath::Clamp(FrontDot, 0.0f, 1.0f));
		SensorData.ProjectileThreatRight =
			FMath::Max(SensorData.ProjectileThreatRight, Threat * FMath::Clamp(RightDot, 0.0f, 1.0f));
		SensorData.ProjectileThreatLeft =
			FMath::Max(SensorData.ProjectileThreatLeft, Threat * FMath::Clamp(-RightDot, 0.0f, 1.0f));
		if (Threat > HighestProjectileThreat)
		{
			HighestProjectileThreat = Threat;
			MostThreateningProjectile = Entry.Get();
			PredictedClosestApproachPoint = SelfLocation + ClosestApproachOffset;
		}
	}
}

void UEnemySensorComponent::UpdateShotSense(float DeltaTime)
{
	if (ShotFlinchRemaining <= 0.0f)
	{
		return;
	}
	ShotFlinchRemaining -= DeltaTime;
	if (bDormant)
	{
		ShotFlinchRemaining = 0.0f;
		return;
	}
	// Max, like two real shots: the flinch never weakens a projectile already coming in.
	float& Threat = ShotFlinchSide > 0 ? SensorData.ProjectileThreatRight : SensorData.ProjectileThreatLeft;
	Threat = FMath::Max(Threat, ShotFlinchThreat);
}

void UEnemySensorComponent::NotifyShotAt(AActor* Shooter, const FVector& LinePoint)
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner || !Shooter || bDormant || (OwnerHealth && OwnerHealth->IsDead()))
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	if (ShotFlinchRemaining > 0.0f || Now < ShotFlinchReadyTime)
	{
		return;
	}

	// Away from the shot line: a line passing on our right reads as a shot from the right, which
	// the brain answers by breaking left. Dead centre picks a side too - the way we are already
	// moving, or either - because a frontal threat splits across both dodges and barely clears
	// the controller's threshold.
	const FVector Right = FlattenAndNormalize(Owner->GetActorRightVector());
	FVector Offset = LinePoint - Owner->GetActorLocation();
	Offset.Z = 0.0f;
	const float Side = FVector::DotProduct(Offset, Right);
	if (FMath::Abs(Side) > ShotCentreTolerance)
	{
		ShotFlinchSide = Side > 0.0f ? 1 : -1;
	}
	else
	{
		// Moving right: the threat goes on the left, so the dodge carries on to the right.
		const float Drift = FVector::DotProduct(Owner->GetVelocity(), Right);
		ShotFlinchSide = FMath::Abs(Drift) > 50.0f ? (Drift > 0.0f ? -1 : 1) : (FMath::RandBool() ? 1 : -1);
	}
	ShotFlinchRemaining = ShotFlinchDuration;
	ShotFlinchReadyTime = Now + ShotFlinchDuration + ShotFlinchCooldown;
	UE_LOG(LogNeuralAI, Log, TEXT("%s saw %s fire (line %.0f cm to the %s): flinching %s"), *GetNameSafe(Owner),
		*GetNameSafe(Shooter), FMath::Abs(Side), Side > 0.0f ? TEXT("right") : TEXT("left"), ShotFlinchSide > 0 ? TEXT("left") : TEXT("right"));
}

bool UEnemySensorComponent::HearShot(AActor* Shooter)
{
	const UWorld* World = GetWorld();
	if (!World || !Shooter || bDormant || (OwnerHealth && OwnerHealth->IsDead()))
	{
		return false;
	}
	const bool bNews = AlertTarget.Get() != Shooter || World->GetTimeSeconds() >= AlertExpireTime;
	AlertTarget = Shooter;
	AlertLocation = Shooter->GetActorLocation();
	AlertExpireTime = World->GetTimeSeconds() + AlertDuration;
	return bNews;
}

void UEnemySensorComponent::UpdateSelfSenses(float DeltaTime)
{
	if (OwnerHealth)
	{
		SensorData.LowHealth = FMath::Clamp(1.0f - OwnerHealth->GetHealthNormalized(), 0.0f, 1.0f);
	}

	// Exponential decay towards zero. Half-life is easier to reason about than a rate:
	// "pain is half gone after PainHalfLife seconds" is directly tunable against the
	// action commit times added in a later phase.
	if (SensorData.Pain > 0.0f && DeltaTime > 0.0f)
	{
		const float DecayFactor = FMath::Pow(0.5f, DeltaTime / FMath::Max(PainHalfLife, KINDA_SMALL_NUMBER));
		SensorData.Pain *= DecayFactor;
		if (SensorData.Pain < 0.001f)
		{
			SensorData.Pain = 0.0f;
		}
	}
}

AActor* UEnemySensorComponent::AcquireTarget(const FVector& SelfLocation) const
{
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		return nullptr;
	}

	const float DistanceSquared = FVector::DistSquared2D(SelfLocation, PlayerPawn->GetActorLocation());
	if (DistanceSquared > FMath::Square(DetectionRange))
	{
		return nullptr;
	}

	if (bRequireLineOfSight && !HasLineOfSightTo(PlayerPawn))
	{
		return nullptr;
	}

	return PlayerPawn;
}

bool UEnemySensorComponent::HasLineOfSightTo(const AActor* Target) const
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !Target || !World)
	{
		return false;
	}

	const FVector Start = Owner->GetActorLocation() + FVector(0.0f, 0.0f, EyeHeight);
	const FVector End = Target->GetActorLocation();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemySensorLineOfSight), false, Owner);
	Params.AddIgnoredActor(Target);

	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
	return !bBlocked;
}

void UEnemySensorComponent::BroadcastAlert(AActor* Target) const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Target || AlertRadius <= 0.0f)
	{
		return;
	}

	const FVector Origin = Owner->GetActorLocation();
	const float RadiusSquared = FMath::Square(AlertRadius);
	int32 Alerted = 0;
	for (const TWeakObjectPtr<UEnemySensorComponent>& Entry : ActiveSensors)
	{
		UEnemySensorComponent* Other = Entry.Get();
		if (!Other || Other == this || !Other->GetOwner())
		{
			continue;
		}
		if (FVector::DistSquared2D(Origin, Other->GetOwner()->GetActorLocation()) <= RadiusSquared)
		{
			Other->ReceiveAlert(Target, this);
			++Alerted;
		}
	}

	if (Alerted > 0)
	{
		UE_LOG(LogNeuralAI, Log, TEXT("%s spotted %s and alerted %d ally(s)"),
			*GetNameSafe(Owner), *GetNameSafe(Target), Alerted);
	}
}

void UEnemySensorComponent::ReceiveAlert(AActor* Target, const UEnemySensorComponent* From)
{
	const UWorld* World = GetWorld();
	if (!World || !Target)
	{
		return;
	}

	// A dead ally cannot be alerted into action; the controller would refuse anyway, but the
	// log would claim otherwise. A dormant one is waiting in a room the player has not
	// entered, and a call from the next room over must not wake it.
	if ((OwnerHealth && OwnerHealth->IsDead()) || bDormant)
	{
		return;
	}

	AlertTarget = Target;
	AlertLocation = Target->GetActorLocation();
	AlertExpireTime = World->GetTimeSeconds() + AlertDuration;
}

void UEnemySensorComponent::SetDormant(bool bInDormant)
{
	bDormant = bInDormant;
	if (bDormant)
	{
		AlertTarget = nullptr;
		LastSeenTarget = nullptr;
		bSawTargetDirectly = false;
		ShotFlinchRemaining = 0.0f;
	}
}

void UEnemySensorComponent::HandleOwnerDamaged(float Damage, AActor* DamageInstigator, const FVector& HitDirection)
{
	// Accumulate so a burst of hits hurts more than one, but never exceed the 0..1 contract.
	const float Spike = Damage / FMath::Max(PainReferenceDamage, KINDA_SMALL_NUMBER);
	SensorData.Pain = FMath::Clamp(SensorData.Pain + Spike, 0.0f, 1.0f);
}

void UEnemySensorComponent::ApplyProfile(const UEnemyAIProfile* Profile)
{
	if (!Profile)
	{
		return;
	}

	DetectionRange = Profile->DetectionRange;

	// Sensitivity scales the spike by dividing the reference damage: a sensitivity of 2
	// means half as much damage produces a full Pain reading.
	PainReferenceDamage = PainReferenceDamage / FMath::Max(Profile->PainSensitivity, KINDA_SMALL_NUMBER);

	// Keep the sensor in step with the brain so no reading is ever a step stale.
	SensorInterval = 1.0f / FMath::Max(Profile->BrainTickRate, 1.0f);
	PrimaryComponentTick.TickInterval = SensorInterval;
}
