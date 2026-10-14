// Neural Enemy AI prototype module.

#include "Characters/RangedEnemyCharacter.h"

#include "Components/EnemySensorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NeuralAI.h"
#include "Projectiles/EnemyProjectile.h"
#include "UObject/ConstructorHelpers.h"

ARangedEnemyCharacter::ARangedEnemyCharacter()
{
	ProjectileClass = AEnemyProjectile::StaticClass();
	AttackRange = 3000.0f;
	AttackInterval = 2.0f;
	AttackDamage = 10.0f;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TintFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	TintMaterial = TintFinder.Object;

	// Taller and narrower than a crawler, so it reads as a different enemy at a glance.
	if (BodyMesh)
	{
		BodyMesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.0f));
		BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -10.0f));
	}
}

void ARangedEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (BodyMesh && TintMaterial)
	{
		if (UMaterialInstanceDynamic* Material = BodyMesh->CreateDynamicMaterialInstance(0, TintMaterial))
		{
			Material->SetVectorParameterValue(TEXT("Color"), BodyColor);
		}
	}
}

int32 ARangedEnemyCharacter::PerformAttack()
{
	UWorld* World = GetWorld();
	const UEnemySensorComponent* Sensor = GetSensorComponent();
	const AActor* Target = Sensor ? Sensor->GetCurrentTarget() : nullptr;
	if (!World || IsDead() || !Target || !ProjectileClass)
	{
		return 0;
	}

	// Turn to the target and fire from the muzzle at its middle; no leading, so a moving target
	// can sidestep.
	const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	SetActorRotation(FRotator(0.0f, ToTarget.Rotation().Yaw, 0.0f));
	const FVector Muzzle = GetActorLocation() + GetActorRotation().RotateVector(MuzzleOffset);
	const FVector Direction = (Target->GetActorLocation() - Muzzle).GetSafeNormal();

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyProjectile* Shot = World->SpawnActor<AEnemyProjectile>(ProjectileClass, Muzzle, Direction.Rotation(), Params);
	if (!Shot)
	{
		return 0;
	}
	Shot->Launch(Direction, ProjectileSpeed, AttackDamage, AttackRange);

	UE_LOG(LogNeuralAI, Log, TEXT("%s fired at %s from %.0f cm"), *GetNameSafe(this), *GetNameSafe(Target), ToTarget.Size2D());
	return 1;
}
