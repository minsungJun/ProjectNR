// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NRSpawnPoint.generated.h"

class AEnemyCharacter;
class UArrowComponent;
class UCapsuleComponent;

/** What may come out of a spawn point. */
UENUM(BlueprintType)
enum class ENRSpawnKind : uint8
{
	/** A close-in enemy (crawler). */
	Melee,
	/** An enemy that shoots from afar. */
	Ranged,
	/** Either; the encounter rules decide, within the room's ranged cap. */
	Any,
	/** The boss room's boss. Always used, outside the share of points picked; skipped while no boss class is set. */
	Boss
};

/**
 * A place a room's enemy may appear when the player walks in.
 *
 * Put several in a room's level, more than the room should hold: on entry the spawn subsystem
 * picks a share of them (UNREncounterData) and spawns a matching enemy on each, facing where the
 * arrow points. The capsule shows how much room an enemy needs; neither it nor the arrow is seen
 * in game. @see UNRSpawnSubsystem
 */
UCLASS()
class PROJECTNR_API ANRSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ANRSpawnPoint();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	ENRSpawnKind Kind = ENRSpawnKind::Melee;

	/** Spawn exactly this class here instead of the one the encounter rules give for Kind. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	TSubclassOf<AEnemyCharacter> EnemyClassOverride;

	/** Relative chance of being picked among the room's points. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0.01"))
	float Weight = 1.0f;

	/** Always used, before the rest are picked. Boss points always are. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	bool bRequired = false;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Tints the arrow and capsule by Kind, so a room's points read at a glance in the editor. */
	void UpdateEditorLook();

	UPROPERTY(VisibleAnywhere, Category = "Spawn")
	TObjectPtr<UCapsuleComponent> Shape;

	UPROPERTY(VisibleAnywhere, Category = "Spawn")
	TObjectPtr<UArrowComponent> Arrow;
};
