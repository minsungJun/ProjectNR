// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NRItemTypes.h"
#include "NRItemPickup.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * An item lying in the world. The player collects it by walking into it and the pickup
 * disappears: potions and shields go into the player's UNRConsumableComponent, an ammo box into
 * its guns' reserves (UNRWeaponComponent - left lying while they are all full), gold into the
 * run (UNRRunSubsystem).
 *
 * Only player-controlled pawns collect, so enemies walk over drops without taking them.
 * Without art yet, it draws an engine basic shape tinted per item type, slowly spinning and
 * bobbing, with a small light so it reads on a light floor.
 */
UCLASS()
class PROJECTNR_API ANRItemPickup : public AActor
{
	GENERATED_BODY()

public:
	ANRItemPickup();

	/** Sets what this pickup gives and updates its look. Call right after spawning. */
	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void SetItem(ENRItemType InItemType, int32 InCount);

	UFUNCTION(BlueprintPure, Category = "Pickup")
	ENRItemType GetItemType() const { return ItemType; }

	UFUNCTION(BlueprintPure, Category = "Pickup")
	int32 GetCount() const { return Count; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Gives the item to Collector if it can carry it. True when collected. */
	bool TryCollect(AActor* Collector);

	void ApplyLook();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	ENRItemType ItemType = ENRItemType::Potion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (ClampMin = "1"))
	int32 Count = 1;

	/** Seconds before it disappears on its own. 0 keeps it until collected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup", meta = (ClampMin = "0.0"))
	float Lifetime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	TObjectPtr<UStaticMesh> PotionMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	FVector PotionScale = FVector(0.22f, 0.22f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	FLinearColor PotionColor = FLinearColor(0.25f, 1.0f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	TObjectPtr<UStaticMesh> ShieldMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	FVector ShieldScale = FVector(0.28f, 0.28f, 0.28f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	FLinearColor ShieldColor = FLinearColor(0.35f, 0.7f, 1.0f);

	/** An ammo box: a squat block. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	TObjectPtr<UStaticMesh> AmmoMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	FVector AmmoScale = FVector(0.32f, 0.22f, 0.18f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	FLinearColor AmmoColor = FLinearColor(0.85f, 0.45f, 0.1f);

	/** Gold: a coin stood on its edge, spinning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	TObjectPtr<UStaticMesh> MoneyMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	FVector MoneyScale = FVector(0.24f, 0.24f, 0.04f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	FLinearColor MoneyColor = FLinearColor(1.0f, 0.8f, 0.15f);

	/** Tinted through its "Color" parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	TObjectPtr<UMaterialInterface> TintMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	float SpinDegreesPerSecond = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	float BobHeight = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Look")
	float BobPeriod = 1.6f;

private:
	/** Mesh height at rest, which the bob moves around. */
	float MeshBaseZ = 0.0f;
	bool bCollected = false;
};
