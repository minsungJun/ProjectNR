// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/**
 * A shot fired by a ranged enemy at its target.
 *
 * Kept apart from AAITestProjectile on purpose: enemies sense and dodge those (the player's
 * shots), and they must not dodge each other's. It flies straight, passes through enemies,
 * damages the first other thing with health it touches and stops at walls.
 */
UCLASS()
class NEURALAI_API AEnemyProjectile : public AActor
{
	GENERATED_BODY()

public:
	AEnemyProjectile();

	/** Launches along Direction at Speed with Damage, flying at most Range cm. Call right after spawning. */
	void Launch(const FVector& Direction, float InSpeed, float InDamage, float Range);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnSphereHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/** Tint of the shot, so it reads as the enemy's and not the player's. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	FLinearColor Color = FLinearColor(1.0f, 0.35f, 0.05f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "1.0"))
	float CollisionRadius = 12.0f;

	/** Material tinted with Color: BasicShapeMaterial, whose "Color" parameter the basic meshes' own material lacks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UMaterialInterface> TintMaterial;

private:
	float Damage = 0.0f;
	bool bSpent = false;
};
