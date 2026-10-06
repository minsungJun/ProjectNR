// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NRDoor.generated.h"

class UStaticMeshComponent;

/**
 * A door between rooms. Rooms open and close it; the player never does.
 *
 * Closed it blocks everything, opened it slides out of the way (into the floor by default)
 * and stops colliding. A door between two rooms is listed on both: the room behind opens it
 * when cleared, the room ahead closes it once the player is inside.
 *
 * The default mesh is the engine cube scaled to a 3 x 0.25 x 3 m slab so the door shows up
 * the moment it is placed; scale the actor or swap the mesh to fit the doorway.
 */
UCLASS()
class PROJECTNR_API ANRDoor : public AActor
{
	GENERATED_BODY()

public:
	ANRDoor();

	UFUNCTION(BlueprintCallable, Category = "Door")
	void Open();

	UFUNCTION(BlueprintCallable, Category = "Door")
	void Close();

	/** True once opening has started, until closing starts. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsOpen() const { return bWantOpen; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	/** State at the start of play. Doors between rooms start closed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	bool bStartOpen = false;

	/** Where the mesh moves to when open, relative to where it sits closed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FVector OpenOffset = FVector(0.0f, 0.0f, -310.0f);

	/** Seconds from fully closed to fully open (and back). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door", meta = (ClampMin = "0.0"))
	float MoveTime = 0.6f;

private:
	void ApplyAlpha();
	void SetBlocking(bool bBlocking);

	FVector ClosedLocation = FVector::ZeroVector;

	/** 0 closed, 1 open. */
	float Alpha = 0.0f;
	bool bWantOpen = false;
};
