// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AITestPlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UInputAction;
class UInputMappingContext;
class UHealthComponent;
class AAITestProjectile;
struct FInputActionValue;

/**
 * Minimal player used to exercise the enemy AI: move, look, and fire a projectile.
 *
 * Intentionally has no weapon, inventory or ability system. Input follows the same
 * Enhanced Input pattern as the project's existing character so it behaves identically
 * on this engine version.
 */
UCLASS()
class NEURALAI_API AAITestPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AAITestPlayerCharacter();

	/** Spawns a projectile from the muzzle along the current aim direction. */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	AAITestProjectile* FireProjectile();

	UFUNCTION(BlueprintPure, Category = "Combat")
	FVector GetAimDirection() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	FVector GetMuzzleLocation() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** void-returning wrapper so the key binding matches FInputActionHandlerSignature. */
	void HandleFirePressed();

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** Stand-in body so the pawn is visible without pulling in a skeletal mesh or anim blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TSubclassOf<AAITestProjectile> ProjectileClass;

	/** Muzzle offset in character space: forward, right, up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FVector MuzzleOffset = FVector(80.0f, 0.0f, 40.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat", meta = (ClampMin = "0.0"))
	float FireCooldown = 0.25f;

	/** Base walk speed in cm/s. The NeuralAI.PlayerSpeed console variable overrides it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "1.0"))
	float MovementSpeed = 900.0f;

private:
	float LastFireTime = -1000.0f;
};
