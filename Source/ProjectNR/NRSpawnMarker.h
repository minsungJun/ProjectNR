// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NRSpawnMarker.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;

/**
 * The warning where an enemy is about to appear: a disc on the floor that swells over the
 * telegraph time, with a thin pillar of the same colour above it so it is seen from any angle.
 * Spawned by UNRSpawnSubsystem, gone by itself.
 */
UCLASS()
class PROJECTNR_API ANRSpawnMarker : public AActor
{
	GENERATED_BODY()

public:
	ANRSpawnMarker();

	/** Shows the marker in Color for Seconds. Call right after spawning. */
	void Show(const FLinearColor& Color, float Seconds);

	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Marker")
	TObjectPtr<UStaticMeshComponent> Disc;

	UPROPERTY(VisibleAnywhere, Category = "Marker")
	TObjectPtr<UStaticMeshComponent> Pillar;

	UPROPERTY(VisibleAnywhere, Category = "Marker")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> TintMaterial;

private:
	float Duration = 0.8f;
	float Elapsed = 0.0f;
};
