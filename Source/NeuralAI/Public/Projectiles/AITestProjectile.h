// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AITestProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/**
 * Test projectile fired by the player.
 *
 * Every live instance registers itself in a static list. The enemy sensor walks
 * that list instead of running an overlap query per enemy per tick, which is what
 * keeps the 30-enemy target affordable. The list is an implementation detail of
 * the *sensor* side of the pipeline; the neural brain never sees it.
 */
UCLASS()
class NEURALAI_API AAITestProjectile : public AActor
{
	GENERATED_BODY()

public:
	AAITestProjectile();

	/** All projectiles currently alive in the world. May contain stale weak pointers. */
	static const TArray<TWeakObjectPtr<AAITestProjectile>>& GetActiveProjectiles() { return ActiveProjectiles; }

	/** Current world-space velocity in cm/s, straight from the movement component. */
	UFUNCTION(BlueprintPure, Category = "Projectile")
	FVector GetProjectileVelocity() const;

	/** Launches along Direction at InitialSpeed. Call right after spawning. */
	UFUNCTION(BlueprintCallable, Category = "Projectile")
	void LaunchInDirection(const FVector& Direction);

	UFUNCTION(BlueprintPure, Category = "Projectile")
	float GetDamage() const { return Damage; }

	UFUNCTION(BlueprintPure, Category = "Projectile")
	float GetCollisionRadius() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnSphereHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
	float Damage = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "0.1"))
	float LifeSeconds = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "1.0"))
	float InitialSpeed = 2000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "1.0"))
	float CollisionRadius = 12.0f;

	/**
	 * How far the shot may travel before it is removed, in cm. 0 means no range limit and
	 * the projectile lives for @LifeSeconds instead.
	 *
	 * Enforced as a lifespan set at launch: flight is a straight line at constant speed, so
	 * range / speed is exactly the time it takes, and nothing has to measure distance per frame.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile", meta = (ClampMin = "0.0"))
	float MaxRange = 0.0f;

	/**
	 * Pushes CollisionRadius and InitialSpeed into the components.
	 *
	 * The base constructor builds the components from its own defaults, so a subclass that
	 * changes those values in its constructor must call this afterwards or the change never
	 * reaches the sphere, the mesh scale or the movement component.
	 */
	void ApplyProjectileSettings();

private:
	static TArray<TWeakObjectPtr<AAITestProjectile>> ActiveProjectiles;
};
