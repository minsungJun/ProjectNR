// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/EnemySensorTypes.h"
#include "EnemySensorComponent.generated.h"

class UHealthComponent;
class UEnemyAIProfile;

/**
 * The enemy's only window onto the world.
 *
 * Every trace, every actor lookup, every distance test lives here. The neural brain
 * receives an FEnemySensorData and has no way to reach the world at all, which is the
 * separation the whole design rests on: swap this component and the brain is unchanged.
 */
UCLASS(ClassGroup = (NeuralAI), meta = (BlueprintSpawnableComponent))
class NEURALAI_API UEnemySensorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemySensorComponent();

	/** Latest snapshot. Refreshed on this component's own tick interval. */
	UFUNCTION(BlueprintPure, Category = "Sensor")
	const FEnemySensorData& GetSensorData() const { return SensorData; }

	/** The actor being sensed, or null when nothing is detected. */
	UFUNCTION(BlueprintPure, Category = "Sensor")
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "Sensor")
	float GetDetectionRange() const { return DetectionRange; }

	/**
	 * World-space unit vector towards the target, flattened to the ground plane.
	 * Zero when nothing is detected. The brain never sees this: it only produces
	 * scalar drives, and the controller combines them with this direction.
	 */
	UFUNCTION(BlueprintPure, Category = "Sensor")
	FVector GetTargetDirection() const { return TargetDirection; }

	/** Ground-plane distance to the target in cm. Zero when nothing is detected. */
	UFUNCTION(BlueprintPure, Category = "Sensor")
	float GetTargetDistance() const { return TargetDistance; }

	/** The projectile that produced the highest threat this update, for debug drawing. */
	UFUNCTION(BlueprintPure, Category = "Sensor")
	AActor* GetMostThreateningProjectile() const { return MostThreateningProjectile.Get(); }

	/** Where that projectile is predicted to pass closest to us. Meaningless when threat is 0. */
	UFUNCTION(BlueprintPure, Category = "Sensor")
	FVector GetPredictedClosestApproachPoint() const { return PredictedClosestApproachPoint; }

	UFUNCTION(BlueprintPure, Category = "Sensor")
	float GetHighestProjectileThreat() const { return HighestProjectileThreat; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Copies detection range and pain sensitivity out of an archetype profile. */
	void ApplyProfile(const UEnemyAIProfile* Profile);

protected:
	virtual void BeginPlay() override;

	void UpdateSensorData(float DeltaTime);
	void UpdateTargetSenses(const FVector& SelfLocation, const FVector& Forward, const FVector& Right);
	void UpdateProjectileSenses(const FVector& SelfLocation, const FVector& SelfVelocity,
		const FVector& Forward, const FVector& Right);

	void UpdateSelfSenses(float DeltaTime);

	/** Finds the actor to treat as a target and checks range plus line of sight. */
	AActor* AcquireTarget(const FVector& SelfLocation) const;

	bool HasLineOfSightTo(const AActor* Target) const;

	UFUNCTION()
	void HandleOwnerDamaged(float Damage, AActor* DamageInstigator, const FVector& HitDirection);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor", meta = (ClampMin = "100.0"))
	float DetectionRange = 2000.0f;

	/** Eye height above the actor origin, used for line-of-sight traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor")
	float EyeHeight = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor")
	bool bRequireLineOfSight = true;

	/** Relative speed that maps to a fully saturated closing/opening reading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor", meta = (ClampMin = "1.0"))
	float MaxClosingSpeed = 800.0f;

	/** Damage in one hit that produces a full Pain spike. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor", meta = (ClampMin = "1.0"))
	float PainReferenceDamage = 25.0f;

	/** Seconds for a Pain spike to fall by half. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor", meta = (ClampMin = "0.05"))
	float PainHalfLife = 0.6f;

	/** Projectiles further away than this are skipped before any maths runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor|Projectile", meta = (ClampMin = "100.0"))
	float ProjectileSenseRange = 3000.0f;

	/** An impact this many seconds away reads as zero threat; zero seconds reads as full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor|Projectile", meta = (ClampMin = "0.05"))
	float ProjectileThreatMaxTime = 1.5f;

	/** Predicted miss distance at which a projectile stops being worth dodging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor|Projectile", meta = (ClampMin = "10.0"))
	float ProjectileMissRadius = 250.0f;

	/** Sensor refresh period. Matches the brain's 20Hz step so no reading is stale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor", meta = (ClampMin = "0.01"))
	float SensorInterval = 0.05f;

private:
	FEnemySensorData SensorData;

	TWeakObjectPtr<AActor> CurrentTarget;

	UPROPERTY(Transient)
	TObjectPtr<UHealthComponent> OwnerHealth;

	FVector TargetDirection = FVector::ZeroVector;
	float TargetDistance = 0.0f;

	TWeakObjectPtr<AActor> MostThreateningProjectile;
	FVector PredictedClosestApproachPoint = FVector::ZeroVector;
	float HighestProjectileThreat = 0.0f;

	/** Target position from the previous update, used to derive its velocity. */
	FVector PreviousTargetLocation = FVector::ZeroVector;
	bool bHasPreviousTargetLocation = false;
};
