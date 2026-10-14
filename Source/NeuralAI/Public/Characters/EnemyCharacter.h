// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EnemyCharacter.generated.h"

class UHealthComponent;
class UEnemySensorComponent;
class UNeuralBrainComponent;
class UEnemyAIDebugComponent;
class UEnemyClimbComponent;
class UStaticMeshComponent;
class UStateTree;
class UEnemyAIProfile;

/**
 * The Crawler: an insect-like enemy driven by a neural brain.
 *
 * This class stays deliberately thin. It owns components and exposes the physical
 * actions (move, attack, dodge) but decides nothing: the sensor gathers world state,
 * the brain produces drives, and the AI controller turns drives into these calls.
 */
UCLASS()
class NEURALAI_API AEnemyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AEnemyCharacter();

	UFUNCTION(BlueprintPure, Category = "Enemy")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Enemy")
	UEnemySensorComponent* GetSensorComponent() const { return SensorComponent; }

	/** Named GetNeuralBrain, not GetBrainComponent: AAIController already owns that name
	 *  for the behaviour-tree brain, and confusing the two would be easy. */
	UFUNCTION(BlueprintPure, Category = "Enemy")
	UNeuralBrainComponent* GetNeuralBrain() const { return NeuralBrain; }

	/**
	 * Damages anything with health inside a forward arc. Purely mechanical: the decision
	 * that an attack should happen, and whether it is allowed to, belongs to the controller.
	 * Returns the number of actors hit (a ranged enemy, whose shot lands later, returns 1 for
	 * a shot fired).
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy")
	virtual int32 PerformAttack();

	UFUNCTION(BlueprintPure, Category = "Enemy")
	float GetAttackRange() const { return AttackRange; }

	/** Seconds between attacks, or 0 to leave it to the controller's AttackCooldown. */
	UFUNCTION(BlueprintPure, Category = "Enemy")
	float GetAttackInterval() const { return AttackInterval; }

	/** Which ring of positional slots this enemy stands in: 0 close in, 1 the ranged ring. */
	virtual int32 GetPreferredSlotRing() const { return 0; }

	/** Whether its attacks take turns through the attack tokens. Close-in strikes do. */
	virtual bool UsesAttackTokens() const { return true; }

	UFUNCTION(BlueprintPure, Category = "Enemy")
	UEnemyClimbComponent* GetClimbComponent() const { return ClimbComponent; }

	/** Whether it goes up walls to reach its target. Crawlers do; shooters keep to the floor. */
	virtual bool CanClimb() const { return true; }

	/**
	 * Lays the body mesh against the wall: 0 standing as usual, 1 belly to the wall in front,
	 * head up. Driven by the climb component; the crawler has no animation, so this is the climb.
	 */
	void SetClimbPose(float Alpha);

	/**
	 * State tree that gates this enemy's behaviour, or null to run the controller's own
	 * pipeline directly.
	 *
	 * It lives on the character rather than the controller because the controller is
	 * spawned at runtime and never appears in the level, so there is nothing to assign
	 * an asset to. The controller reads this on possession and hands it to its own
	 * StateTreeAIComponent, which is where the schema requires it to run.
	 */
	UFUNCTION(BlueprintPure, Category = "Enemy")
	UStateTree* GetCombatStateTree() const { return CombatStateTree; }

	/** Archetype numbers for this enemy, or null to use the class defaults. */
	UFUNCTION(BlueprintPure, Category = "Enemy")
	const UEnemyAIProfile* GetAIProfile() const { return AIProfile; }

	/**
	 * Adopts another enemy's profile and state tree.
	 *
	 * Used when spawning a crowd from a hand-placed template, so the copies are the same
	 * archetype rather than bare class defaults. Only meaningful before BeginPlay, which
	 * is where the profile gets pushed into the components.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy")
	void CopyArchetypeFrom(const AEnemyCharacter* Other);

	UFUNCTION(BlueprintPure, Category = "Enemy")
	bool IsDead() const;

	/**
	 * Briefly disables acting without killing the pawn.
	 *
	 * Exists so the StateTree has a real third state to switch on. Nothing inflicts a
	 * stun automatically yet; NeuralAI.StunEnemy drives it for testing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy")
	void ApplyStun(float Duration);

	UFUNCTION(BlueprintPure, Category = "Enemy")
	bool IsStunned() const { return bStunned; }

protected:
	virtual void BeginPlay() override;

	void ClearStun();

	/** Repairs component pointers left null by hot reload reinstancing. */
	void ResolveComponentReferences();

	/** Stops movement, drops collision, and lets the body lie there. */
	UFUNCTION()
	void HandleDeath(AActor* Killer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UEnemySensorComponent> SensorComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNeuralBrainComponent> NeuralBrain;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UEnemyAIDebugComponent> DebugComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UEnemyClimbComponent> ClimbComponent;

	/** Body mesh, relative to the capsule, while on a wall: tipped head up, belly to the wall just in front. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Climb")
	FRotator ClimbBodyRotation = FRotator(90.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Climb")
	FVector ClimbBodyLocation = FVector(17.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy", meta = (ClampMin = "1.0"))
	float MovementSpeed = 400.0f;

	/**
	 * Tuning numbers shared by the whole archetype.
	 *
	 * Values here win over the per-property defaults on this actor and its components,
	 * so one asset retunes every crawler at once. Leave empty to keep the class defaults.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Profile")
	TObjectPtr<UEnemyAIProfile> AIProfile;

	/** Assign ST_TestMonsterST here. Leave empty to run without a state tree. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|StateTree")
	TObjectPtr<UStateTree> CombatStateTree;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Attack", meta = (ClampMin = "0.0"))
	float AttackDamage = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Attack", meta = (ClampMin = "10.0"))
	float AttackRange = 220.0f;

	/** Half-angle of the damage arc in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Attack", meta = (ClampMin = "5.0", ClampMax = "180.0"))
	float AttackHalfAngle = 60.0f;

	/** Seconds between attacks. 0 uses the controller's AttackCooldown (1.5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Attack", meta = (ClampMin = "0.0"))
	float AttackInterval = 0.0f;

private:
	bool bStunned = false;

	/** The body mesh's own place on the capsule, to come back to after a climb. */
	FTransform BodyRestTransform;
	FTimerHandle StunTimer;
};
