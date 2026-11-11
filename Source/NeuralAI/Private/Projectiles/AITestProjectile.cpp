// Neural Enemy AI prototype module.

#include "Projectiles/AITestProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/HealthComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NeuralAI.h"
#include "UObject/ConstructorHelpers.h"

TArray<TWeakObjectPtr<AAITestProjectile>> AAITestProjectile::ActiveProjectiles;

AAITestProjectile::AAITestProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	// Query-only: the projectile is swept by the movement component, it is not a rigid body.
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	CollisionSphere->SetGenerateOverlapEvents(false);
	CollisionSphere->OnComponentHit.AddDynamic(this, &AAITestProjectile::OnSphereHit);
	SetRootComponent(CollisionSphere);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(CollisionSphere);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(SphereMesh.Object);
	}

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionSphere;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	// No gravity: the threat maths assumes a straight line, and a ballistic arc would
	// make time-to-closest-approach wrong without buying anything for this prototype.
	ProjectileMovement->ProjectileGravityScale = 0.0f;

	ApplyProjectileSettings();
}

void AAITestProjectile::ApplyProjectileSettings()
{
	CollisionSphere->InitSphereRadius(CollisionRadius);

	// BasicShapes/Sphere is 100cm across, so scale it down to the collision radius.
	MeshComponent->SetRelativeScale3D(FVector((CollisionRadius * 2.0f) / 100.0f));

	ProjectileMovement->InitialSpeed = InitialSpeed;
	ProjectileMovement->MaxSpeed = InitialSpeed;

	InitialLifeSpan = LifeSeconds;
}

void AAITestProjectile::BeginPlay()
{
	Super::BeginPlay();

	// Never collide with whoever fired us, nor with anything else they fired. A shotgun
	// spawns all its pellets in the same spot, and without this they would block each
	// other's sweeps and destroy themselves before leaving the muzzle.
	if (AActor* MyInstigator = GetInstigator())
	{
		CollisionSphere->IgnoreActorWhenMoving(MyInstigator, true);

		for (const TWeakObjectPtr<AAITestProjectile>& Entry : ActiveProjectiles)
		{
			AAITestProjectile* Other = Entry.Get();
			if (Other && Other->GetInstigator() == MyInstigator)
			{
				CollisionSphere->IgnoreActorWhenMoving(Other, true);
				Other->CollisionSphere->IgnoreActorWhenMoving(this, true);
			}
		}
	}

	ActiveProjectiles.Add(this);
}

void AAITestProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ActiveProjectiles.RemoveAllSwap([this](const TWeakObjectPtr<AAITestProjectile>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == this;
	});

	Super::EndPlay(EndPlayReason);
}

FVector AAITestProjectile::GetProjectileVelocity() const
{
	return ProjectileMovement ? ProjectileMovement->Velocity : FVector::ZeroVector;
}

float AAITestProjectile::GetCollisionRadius() const
{
	return CollisionSphere ? CollisionSphere->GetScaledSphereRadius() : CollisionRadius;
}

void AAITestProjectile::LaunchInDirection(const FVector& Direction)
{
	if (!ProjectileMovement)
	{
		return;
	}

	const FVector Unit = Direction.GetSafeNormal();
	if (Unit.IsNearlyZero())
	{
		UE_LOG(LogNeuralAI, Warning, TEXT("LaunchInDirection called with a zero direction; using actor forward"));
		ProjectileMovement->Velocity = GetActorForwardVector() * InitialSpeed;
	}
	else
	{
		ProjectileMovement->Velocity = Unit * InitialSpeed;
	}

	ProjectileMovement->UpdateComponentVelocity();

	if (MaxRange > 0.0f)
	{
		SetLifeSpan(MaxRange / FMath::Max(InitialSpeed, 1.0f));
	}
}

void AAITestProjectile::OnSphereHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherActor && OtherActor != this && OtherActor != GetInstigator())
	{
		if (UHealthComponent* Health = OtherActor->FindComponentByClass<UHealthComponent>())
		{
			// Direction of travel is what the victim needs to know, not the surface normal.
			const FVector HitDirection = GetProjectileVelocity().GetSafeNormal();
			Health->ApplyDamage(Damage, GetInstigator(), HitDirection, DamageSource);
		}
	}

	Destroy();
}
