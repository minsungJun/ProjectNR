// Neural Enemy AI prototype module.

#include "Characters/BossEnemyCharacter.h"

#include "Characters/EnemyVisualSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

ABossEnemyCharacter::ABossEnemyCharacter()
{
	// The golem model at scale 4 is about 345 cm wide, 400 long and 208 tall: the capsule covers
	// its body (the legs splay out past it) and stands it on the floor. UEnemyVisualSettings::Boss
	// puts the model's feet at the capsule bottom (Offset Z = -104).
	GetCapsuleComponent()->InitCapsuleSize(145.0f, 104.0f);

	// A little faster than a crawler (400): with its size it closes the gap quickly.
	MovementSpeed = 420.0f;
	AttackDamage = 30.0f;
	AttackRange = 360.0f;
	AttackHalfAngle = 70.0f;
	AttackInterval = 2.5f;

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

const FEnemyVisual& ABossEnemyCharacter::GetVisual() const
{
	return GetDefault<UEnemyVisualSettings>()->Boss;
}

int32 ABossEnemyCharacter::PerformAttack()
{
	UWorld* World = GetWorld();
	if (!World || IsDead() || World->GetTimerManager().IsTimerActive(ImpactTimer))
	{
		return 0;
	}

	PlayAttackAnimation();
	if (AttackImpactDelay <= 0.0f)
	{
		ApplyAttackDamage();
		return 1;
	}

	// The legs are in the air until then; ApplyAttackDamage does nothing if it died meanwhile.
	World->GetTimerManager().SetTimer(ImpactTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		// The model's size mid-slam: a wrong import of the animation shows up here as a collapse.
		if (const USkeletalMeshComponent* Model = GetMesh())
		{
			UE_LOG(LogTemp, Log, TEXT("%s slam impact: model %s, bounds %s"), *GetName(),
				Model->IsVisible() ? TEXT("visible") : TEXT("hidden"), *(Model->Bounds.BoxExtent * 2.0).ToString());
		}
		ApplyAttackDamage();
	}), AttackImpactDelay, false);
	return 1;
}

void ABossEnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ImpactTimer);
	}
	Super::EndPlay(EndPlayReason);
}
