// Neural Enemy AI prototype module.

#include "Climb/EnemyClimbComponent.h"

#include "Characters/EnemyCharacter.h"
#include "Climb/EnemyClimbLinkSubsystem.h"
#include "Climb/EnemyClimbSettings.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NeuralAI.h"

/** Draws what the probe found and the climb under way. */
TAutoConsoleVariable<int32> CVarEnemyClimbDebug(
	TEXT("NeuralAI.Climb.Debug"),
	0,
	TEXT("1: draw climb probes and climbs under way. Same as Project Settings > Enemy Climb > Draw Debug."),
	ECVF_Default);

UEnemyClimbComponent::UEnemyClimbComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

ACharacter* UEnemyClimbComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

float UEnemyClimbComponent::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0f;
}

bool UEnemyClimbComponent::StartClimb(const FEnemyClimbLedge& InLedge)
{
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner());
	if (IsClimbing() || !GetCharacter() || (Enemy && (!Enemy->CanClimb() || Enemy->IsDead())))
	{
		return false;
	}
	// Someone already on that spot: the next free one along the wall, or wait a moment.
	const UEnemyClimbSettings* Settings = GetDefault<UEnemyClimbSettings>();
	UEnemyClimbLinkSubsystem* Spots = GetWorld() ? GetWorld()->GetSubsystem<UEnemyClimbLinkSubsystem>() : nullptr;
	FEnemyClimbLedge Free = InLedge;
	if (Spots)
	{
		if (Now() - LastSpotTakenTime < Settings->ClimbSpotRetryInterval)
		{
			return false;
		}
		if (!Spots->FindFreeSpot(GetCharacter(), InLedge, Free))
		{
			LastSpotTakenTime = Now();
			UE_LOG(LogNeuralAI, Verbose, TEXT("%s waits: every spot near %s is taken"), *GetNameSafe(GetOwner()), *InLedge.BaseCenter.ToCompactString());
			return false;
		}
		if (FVector::Dist2D(Free.BaseCenter, InLedge.BaseCenter) > 1.0f)
		{
			UE_LOG(LogNeuralAI, Log, TEXT("%s moves %.0f cm along the wall: its spot is taken"), *GetNameSafe(GetOwner()),
				FVector::Dist2D(Free.BaseCenter, InLedge.BaseCenter));
		}
		Spots->RegisterClimber(GetOwner(), Free);
	}
	Ledge = Free;
	ClimbStartTime = Now();
	LastBlocker = TEXT("nothing");
	SetComponentTickEnabled(true);
	UE_LOG(LogNeuralAI, Log, TEXT("%s climbing %s: %.0f cm up, foot %.0f cm away"), *GetNameSafe(GetOwner()),
		*GetNameSafe(Ledge.Wall.Get()), Ledge.GetHeight(), Ledge.GetDistanceTo(GetOwner()->GetActorLocation()));
	EnterPhase(EEnemyClimbPhase::Approach);
	return true;
}

void UEnemyClimbComponent::EnterPhase(EEnemyClimbPhase NewPhase)
{
	ACharacter* Character = GetCharacter();
	Phase = NewPhase;
	PhaseStartTime = Now();
	PhaseStartLocation = Character ? Character->GetActorLocation() : FVector::ZeroVector;
	PhaseStartRotation = Character ? Character->GetActorRotation() : FRotator::ZeroRotator;
	BestRemaining = TNumericLimits<float>::Max();
	LastProgressTime = PhaseStartTime;

	// Off the floor the movement component would only pull the body down and slide it about.
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Movement && NewPhase == EEnemyClimbPhase::Align)
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_None);
	}
}

void UEnemyClimbComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Character = GetCharacter();
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Character);
	if (!IsClimbing() || !Character || (Enemy && Enemy->IsDead()))
	{
		SetComponentTickEnabled(false);
		return;
	}
	const UEnemyClimbSettings* Settings = GetDefault<UEnemyClimbSettings>();
	const float Elapsed = Now() - PhaseStartTime;
	const FVector Location = Character->GetActorLocation();

	switch (Phase)
	{
	case EEnemyClimbPhase::Approach:
	{
		const float Remaining = FVector::Dist2D(Location, Ledge.BaseCenter);
		if (Remaining <= Settings->BaseArrivalRadius)
		{
			EnterPhase(EEnemyClimbPhase::Align);
			break;
		}
		if (IsStuck(Remaining))
		{
			Cancel(TEXT("could not reach the foot of the wall"));
			break;
		}
		Character->AddMovementInput((Ledge.BaseCenter - Location).GetSafeNormal2D(), 1.0f);
		break;
	}
	case EEnemyClimbPhase::Align:
	{
		// Square up to the wall, belly to its face, right over the foot.
		const float Alpha = Settings->AlignTime > 0.0f ? FMath::Clamp(Elapsed / Settings->AlignTime, 0.0f, 1.0f) : 1.0f;
		const FVector Target(Ledge.BaseCenter.X, Ledge.BaseCenter.Y, PhaseStartLocation.Z);
		const FRotator Facing(0.0f, (-Ledge.WallNormal).Rotation().Yaw, 0.0f);
		Character->SetActorRotation(FMath::Lerp(PhaseStartRotation, Facing, Alpha));
		SetPose(Alpha);
		if (!MoveBody(FMath::Lerp(PhaseStartLocation, Target, Alpha), Target))
		{
			break;
		}
		if (Alpha >= 1.0f)
		{
			EnterPhase(EEnemyClimbPhase::Climb);
		}
		break;
	}
	case EEnemyClimbPhase::Climb:
	{
		const float NewZ = FMath::Min(Location.Z + Settings->ClimbSpeed * DeltaTime, Ledge.EdgeCenter.Z);
		if (!MoveBody(FVector(Ledge.BaseCenter.X, Ledge.BaseCenter.Y, NewZ), Ledge.EdgeCenter))
		{
			break;
		}
		if (Character->GetActorLocation().Z >= Ledge.EdgeCenter.Z - 0.5f)
		{
			EnterPhase(EEnemyClimbPhase::Mantle);
		}
		break;
	}
	case EEnemyClimbPhase::Mantle:
	{
		const float Alpha = FMath::Clamp(Elapsed / Settings->MantleTime, 0.0f, 1.0f);
		SetPose(1.0f - Alpha);
		if (!MoveBody(FEnemyClimbProbe::GetMantlePoint(PhaseStartLocation, Ledge.StandCenter, Alpha), Ledge.StandCenter))
		{
			break;
		}
		if (Alpha >= 1.0f)
		{
			Finish();
		}
		break;
	}
	default:
		break;
	}

#if ENABLE_DRAW_DEBUG
	if (IsClimbing() && (Settings->bDrawDebug || CVarEnemyClimbDebug.GetValueOnGameThread() != 0))
	{
		FEnemyClimbProbe::DrawDebug(GetWorld(), Ledge, EEnemyClimbProbeResult::Ok, 0.0f);
	}
#endif
}

bool UEnemyClimbComponent::MoveBody(const FVector& Location, const FVector& Goal)
{
	ACharacter* Character = GetCharacter();
	FHitResult Hit;
	Character->SetActorLocation(Location, true, &Hit);
	if (Hit.bBlockingHit)
	{
		LastBlocker = FString::Printf(TEXT("%s/%s at %s"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToCompactString());
	}
	if (IsStuck(FVector::Dist(Character->GetActorLocation(), Goal)))
	{
		Cancel(*FString::Printf(TEXT("stuck %s (%s in the way)"),
			*StaticEnum<EEnemyClimbPhase>()->GetNameStringByValue(static_cast<int64>(Phase)), *GetNameSafe(Hit.GetActor())));
		return false;
	}
	return true;
}

bool UEnemyClimbComponent::IsStuck(float Remaining)
{
	// Progress is any centimetre nearer the goal; a body pressed against a crate makes none.
	if (Remaining < BestRemaining - 1.0f)
	{
		BestRemaining = Remaining;
		LastProgressTime = Now();
		return false;
	}
	return Now() - LastProgressTime > GetDefault<UEnemyClimbSettings>()->StuckTime;
}

void UEnemyClimbComponent::Finish()
{
	// Something standing on the edge (the player, as often as not) can keep it from getting all the
	// way in; it lets go there and the movement component sorts out where it lands.
	const float Short = FVector::Dist2D(GetOwner()->GetActorLocation(), Ledge.StandCenter);
	UE_LOG(LogNeuralAI, Log, TEXT("%s climbed %.0f cm in %.2f s, now at %s%s"), *GetNameSafe(GetOwner()), Ledge.GetHeight(),
		Now() - ClimbStartTime, *GetOwner()->GetActorLocation().ToCompactString(),
		Short > 10.0f ? *FString::Printf(TEXT(", %.0f cm short of where it meant to stand (%s in the way)"), Short, *LastBlocker) : TEXT(""));
	LetGo(true);
}

void UEnemyClimbComponent::Cancel(const TCHAR* Reason)
{
	if (!IsClimbing())
	{
		return;
	}
	UE_LOG(LogNeuralAI, Log, TEXT("%s gave up climbing after %.2f s at %s: %s"), *GetNameSafe(GetOwner()), Now() - ClimbStartTime,
		*GetOwner()->GetActorLocation().ToCompactString(), Reason);
	FailedWall = Ledge.Wall;
	FailedWallTime = Now();
	LetGo(IsOnWall());
}

bool UEnemyClimbComponent::IsWallExcluded(const AActor* Wall) const
{
	return Wall && FailedWall.Get() == Wall && Now() - FailedWallTime < GetDefault<UEnemyClimbSettings>()->FailedWallCooldown;
}

void UEnemyClimbComponent::DropDead()
{
	if (!IsClimbing())
	{
		return;
	}
	const bool bWasOnWall = IsOnWall();
	UE_LOG(LogNeuralAI, Log, TEXT("%s died climbing (%s)"), *GetNameSafe(GetOwner()), bWasOnWall ? TEXT("falls") : TEXT("on the floor"));
	LetGo(false);

	// The death handler has just switched collision and movement off, which would leave the body
	// hanging on the wall. Give it back just enough to fall onto the floor and lie there: the usual
	// pawn collision against the level, nothing against other pawns or traces. (Blocking only
	// WorldStatic was not enough: the body fell through Lvl_ClimbTest's floor.)
	ACharacter* Character = GetCharacter();
	if (bWasOnWall && Character)
	{
		UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	}
}

void UEnemyClimbComponent::LetGo(bool bFall)
{
	Phase = EEnemyClimbPhase::None;
	if (UEnemyClimbLinkSubsystem* Spots = GetWorld() ? GetWorld()->GetSubsystem<UEnemyClimbLinkSubsystem>() : nullptr)
	{
		Spots->UnregisterClimber(GetOwner());
	}
	SetComponentTickEnabled(false);
	SetPose(0.0f);
	if (ACharacter* Character = GetCharacter())
	{
		const FRotator Rotation = Character->GetActorRotation();
		Character->SetActorRotation(FRotator(0.0f, Rotation.Yaw, 0.0f));
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (Movement && bFall)
		{
			Movement->StopMovementImmediately();
			Movement->SetMovementMode(MOVE_Falling);
		}
	}
}

void UEnemyClimbComponent::SetPose(float Alpha) const
{
	if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner()))
	{
		Enemy->SetClimbPose(Alpha);
	}
}
