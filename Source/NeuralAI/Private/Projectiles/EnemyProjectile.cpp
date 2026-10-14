// Neural Enemy AI prototype module.

#include "Projectiles/EnemyProjectile.h"

#include "Characters/EnemyCharacter.h"
#include "Components/HealthComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NeuralAI.h"
#include "UObject/ConstructorHelpers.h"

AEnemyProjectile::AEnemyProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	// Walls stop it; pawns are only overlapped, so it can pass the shooter's allies and still
	// find the target. Query-only: it is swept by the movement component.
	CollisionSphere->InitSphereRadius(CollisionRadius);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->SetGenerateOverlapEvents(true);
	CollisionSphere->OnComponentBeginOverlap.AddDynamic(this, &AEnemyProjectile::OnSphereOverlap);
	CollisionSphere->OnComponentHit.AddDynamic(this, &AEnemyProjectile::OnSphereHit);
	SetRootComponent(CollisionSphere);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(CollisionSphere);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(SphereMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TintFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	TintMaterial = TintFinder.Object;
	// BasicShapes/Sphere is 100 cm across.
	MeshComponent->SetRelativeScale3D(FVector(CollisionRadius * 2.0f / 100.0f));

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionSphere;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;
	ProjectileMovement->ProjectileGravityScale = 0.0f;
	// Velocity is set by Launch; nothing moves before then.
	ProjectileMovement->bAutoActivate = false;
}

void AEnemyProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Shooter = GetInstigator())
	{
		CollisionSphere->IgnoreActorWhenMoving(Shooter, true);
	}
	if (UMaterialInstanceDynamic* Material = TintMaterial ? MeshComponent->CreateDynamicMaterialInstance(0, TintMaterial) : nullptr)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

void AEnemyProjectile::Launch(const FVector& Direction, float InSpeed, float InDamage, float Range)
{
	Damage = InDamage;
	const float Speed = FMath::Max(InSpeed, 1.0f);
	ProjectileMovement->InitialSpeed = Speed;
	ProjectileMovement->MaxSpeed = Speed;
	ProjectileMovement->Velocity = Direction.GetSafeNormal() * Speed;
	ProjectileMovement->Activate(true);
	SetLifeSpan(FMath::Max(Range, 100.0f) / Speed);
}

void AEnemyProjectile::OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Enemies do not shoot each other: fly on through the shooter's allies.
	if (bSpent || !OtherActor || OtherActor == GetInstigator() || OtherActor->IsA<AEnemyCharacter>())
	{
		return;
	}
	UHealthComponent* Health = OtherActor->FindComponentByClass<UHealthComponent>();
	if (!Health || Health->IsDead())
	{
		return;
	}
	bSpent = true;
	Health->ApplyDamage(Damage, GetInstigator(), ProjectileMovement->Velocity.GetSafeNormal());
	UE_LOG(LogNeuralAI, Log, TEXT("%s's shot hit %s"), *GetNameSafe(GetInstigator()), *GetNameSafe(OtherActor));
	Destroy();
}

void AEnemyProjectile::OnSphereHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Only walls block it.
	bSpent = true;
	Destroy();
}
