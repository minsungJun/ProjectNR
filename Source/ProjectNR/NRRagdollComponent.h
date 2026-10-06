// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NRRagdollComponent.generated.h"

class USkeletalMeshComponent;

/**
 * Turns the character into a ragdoll when its UHealthComponent reports death.
 *
 * The sample has a full ragdoll system (BP_MovementMode_Ragdoll, injury states, getting up),
 * but it lives on a separate pawn and is built around recovering. Death needs none of that:
 * the mesh switches to physics on the spot and falls, pushed away from whoever landed the
 * killing blow.
 *
 * The capsule and the movement component are left alone, so the pawn - and the camera that
 * follows it - stays where it died while the body falls beside it. The mesh ignores the Pawn
 * channel so the body does not collide with its own capsule.
 */
UCLASS(ClassGroup = (NR), meta = (BlueprintSpawnableComponent))
class PROJECTNR_API UNRRagdollComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNRRagdollComponent();

	/** Starts the ragdoll now. Direction is the push, already including any upward part. */
	UFUNCTION(BlueprintCallable, Category = "Ragdoll")
	void StartRagdoll(const FVector& PushDirection);

	UFUNCTION(BlueprintPure, Category = "Ragdoll")
	bool IsRagdolling() const { return bRagdolling; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleDeath(AActor* Killer);

	/** Speed, in cm/s, given to the pelvis at the moment of death. 0 just collapses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll", meta = (ClampMin = "0.0"))
	float DeathPushSpeed = 350.0f;

	/** Upward share of the push, so the body tips rather than sliding along the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DeathPushUp = 0.3f;

	/** Bone the push is applied to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	FName PushBone = TEXT("pelvis");

private:
	/** The skeletal mesh that has a physics asset; a visual-only mesh cannot ragdoll. */
	USkeletalMeshComponent* FindRagdollMesh() const;

	bool bRagdolling = false;
};
