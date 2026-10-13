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
 *
 * Allies alert each other here too, for the same reason: it is world knowledge. A sensor that
 * sees the target directly tells every sensor within AlertRadius; those treat the target as
 * sensed for AlertDuration even out of range or sight, so a whole group reacts when one member
 * spots the player. Only direct sightings are passed on - an alerted sensor does not relay
 * its alert - so it spreads to the neighbours of whoever actually saw, not across the map.
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

	/** An ally saw Target. Sense it for AlertDuration regardless of range or sight. */
	void ReceiveAlert(AActor* Target, const UEnemySensorComponent* From);

	/** True while the current target comes from an ally's alert rather than our own eyes. */
	UFUNCTION(BlueprintPure, Category = "Sensor")
	bool IsSensingByAlert() const { return bSensingByAlert; }

	/**
	 * A dormant enemy senses no target and takes no alerts, so its brain stays idle - how a
	 * room keeps its enemies waiting until the player walks in. Projectile and pain senses
	 * keep working; waking is the room's decision, not the sensor's.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sensor")
	void SetDormant(bool bInDormant);

	UFUNCTION(BlueprintPure, Category = "Sensor")
	bool IsDormant() const { return bDormant; }

	/**
	 * Shooter just fired along a line passing near us - the moment of the shot, not its flight,
	 * so a fast round can still be dodged. Called by whoever traced the shot (the game, not this
	 * module). For ShotFlinchDuration the enemy reads an incoming-shot threat from the side the
	 * line passes (LinePoint is the line's closest point to us), and the brain dodges away from
	 * it. A flinch already under way, or one within ShotFlinchCooldown, is not restarted.
	 * Ignored while dormant or dead.
	 */
	void NotifyShotAt(AActor* Shooter, const FVector& LinePoint);

	/**
	 * A shot by Shooter was heard. Like an ally's alert, the shooter is sensed for AlertDuration
	 * without being seen. Who hears it is the caller's decision (the room it was fired in).
	 * Ignored while dormant or dead. True when this is news (no live alert was running).
	 */
	bool HearShot(AActor* Shooter);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Tells every other sensor within AlertRadius about Target. */
	void BroadcastAlert(AActor* Target) const;

	void UpdateSensorData(float DeltaTime);
	void UpdateTargetSenses(const FVector& SelfLocation, const FVector& Forward, const FVector& Right);
	void UpdateProjectileSenses(const FVector& SelfLocation, const FVector& SelfVelocity,
		const FVector& Forward, const FVector& Right);

	void UpdateSelfSenses(float DeltaTime);

	/** A shot fired our way: the flinch threat, written over the projectile threats it outweighs. */
	void UpdateShotSense(float DeltaTime);

	/** Finds the actor to treat as a target and checks range plus line of sight. */
	AActor* AcquireTarget(const FVector& SelfLocation) const;

	bool HasLineOfSightTo(const AActor* Target) const;

	UFUNCTION()
	void HandleOwnerDamaged(float Damage, AActor* DamageInstigator, const FVector& HitDirection);

	/** Raised from 30 m for 36 m rooms, whose diagonal is about 51 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor", meta = (ClampMin = "100.0"))
	float DetectionRange = 4500.0f;

	/** Allies within this distance of a direct sighting are alerted. 0 disables alerting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor|Alert", meta = (ClampMin = "0.0"))
	float AlertRadius = 2500.0f;

	/** Seconds an alert keeps the target sensed without seeing it. Refreshed by new alerts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor|Alert", meta = (ClampMin = "0.1"))
	float AlertDuration = 8.0f;

	/**
	 * Farthest an alerted enemy will follow a target it has not seen itself. Also the distance
	 * the TargetNear reading is scaled against while alerted - against DetectionRange a target
	 * outside it would read as 0 and give the brain no urge to close in at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor|Alert", meta = (ClampMin = "100.0"))
	float AlertChaseRange = 6000.0f;

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

	/** Threat read during a shot flinch: high enough to launch a dodge, below a shot about to land. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor|Shot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ShotFlinchThreat = 0.8f;

	/** Seconds one flinch keeps the threat up. Short: a jink, not a retreat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor|Shot", meta = (ClampMin = "0.05"))
	float ShotFlinchDuration = 0.3f;

	/** Seconds after a flinch before another shot can start one. With ShotFlinchDuration this
	 *  makes the shot dodge cycle 0.6 s, matching the controller's DodgeCooldown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor|Shot", meta = (ClampMin = "0.0"))
	float ShotFlinchCooldown = 0.3f;

	/** A shot line passing closer to us than this is dead centre: the flinch goes the way we are
	 *  already moving, or either way, instead of away from the line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor|Shot", meta = (ClampMin = "0.0"))
	float ShotCentreTolerance = 30.0f;

	/** Sensor refresh period. Matches the brain's 20Hz step so no reading is stale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor", meta = (ClampMin = "0.01"))
	float SensorInterval = 0.05f;

	/**
	 * Starts dormant, sensing nothing until whoever owns the enemy wakes it. For enemies in
	 * levels that are loaded at run time: they begin play the moment their level loads, before
	 * anything has decided whether their room is in play.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sensor")
	bool bStartDormant = false;

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

	/** Whether last update saw the target directly; a new sighting is what triggers an alert. */
	bool bSawTargetDirectly = false;
	bool bSensingByAlert = false;
	bool bDormant = false;

	TWeakObjectPtr<AActor> AlertTarget;
	float AlertExpireTime = -1.0f;

	/** The flinch under way: seconds left, which threat side it lights (+1 right, -1 left), and
	 *  when the next may start. */
	float ShotFlinchRemaining = 0.0f;
	int32 ShotFlinchSide = 0;
	float ShotFlinchReadyTime = 0.0f;

	/** Every live sensor, so alerts reach allies without an overlap query per sighting. */
	static TArray<TWeakObjectPtr<UEnemySensorComponent>> ActiveSensors;
};
