// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NRInteractable.h"
#include "NRItemTypes.h"
#include "NRLootChest.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * A chest the player opens with the interact key. The lid swings up and the items roll out in
 * front of it as pickups, which the player then walks over to collect - the same pickups
 * enemies drop, spawned through UNRLootSubsystem.
 *
 * Opens once. Place it in the level and set LootTable and ItemCount per chest. The front is
 * the actor's +X: face that towards where the player will come from.
 *
 * Built from engine basic shapes until there is art; a Blueprint child can swap the meshes.
 */
UCLASS()
class PROJECTNR_API ANRLootChest : public AActor, public INRInteractable
{
	GENERATED_BODY()

public:
	ANRLootChest();

	virtual bool CanInteract(const APawn* User) const override { return !bOpened; }
	virtual void Interact(APawn* User) override;
	virtual FText GetInteractPrompt() const override { return Prompt; }
	virtual FVector GetInteractLocation() const override;

	UFUNCTION(BlueprintPure, Category = "Chest")
	bool IsOpened() const { return bOpened; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

	void SpawnItems();
	void ApplyLook();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> Body;

	/** The lid turns about this, at the top of the back edge. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<USceneComponent> LidHinge;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> Lid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> Latch;

	/** Lit while closed so the chest stands out; off once opened. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
	TObjectPtr<UPointLightComponent> Glow;

	/** Each item is picked from here by weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Loot")
	TArray<FNRLootEntry> LootTable;

	/** How many picks from LootTable one chest gives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Loot", meta = (ClampMin = "0"))
	int32 ItemCount = 2;

	/** How far in front of the chest the items land, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Loot", meta = (ClampMin = "0.0"))
	float ItemDistance = 110.0f;

	/** Side-to-side gap between items, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Loot", meta = (ClampMin = "0.0"))
	float ItemSpacing = 70.0f;

	/** Seconds after the key press before the items appear, so they come out as the lid lifts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Loot", meta = (ClampMin = "0.0"))
	float ItemDelay = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest")
	FText Prompt;

	/** Seconds for the lid to swing open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Look", meta = (ClampMin = "0.01"))
	float OpenTime = 0.4f;

	/** How far the lid swings back, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Look")
	float OpenAngle = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Look")
	FLinearColor BodyColor = FLinearColor(0.12f, 0.05f, 0.015f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Look")
	FLinearColor LidColor = FLinearColor(0.18f, 0.08f, 0.02f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chest|Look")
	FLinearColor TrimColor = FLinearColor(1.0f, 0.75f, 0.2f);

	/** Tinted through its "Color" parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Look")
	TObjectPtr<UMaterialInterface> TintMaterial;

private:
	bool bOpened = false;
	float OpenStartTime = 0.0f;
	FTimerHandle ItemTimer;
};
