// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EnemyVisualSettings.generated.h"

class UAnimSequence;
class USkeletalMesh;

/** One enemy archetype's model and how it sits on the capsule. */
USTRUCT(BlueprintType)
struct NEURALAI_API FEnemyVisual
{
	GENERATED_BODY()

	/** Shown instead of the placeholder cube. Empty, or missing from this checkout, keeps the cube. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** Where the model's origin goes, in cm from the capsule centre. The capsule bottom is -60. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FVector Offset = FVector(0.0f, 0.0f, -60.0f);

	/** Turns the model so its head points along the enemy's forward (+X). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual", meta = (ClampMin = "0.01"))
	float Scale = 1.0f;

	/** Looped while it moves, faster the faster it goes. Empty: the model holds its pose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UAnimSequence> WalkAnimation;

	/** cm/s the walk animation covers at play rate 1. The play rate is the enemy's speed over this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual", meta = (ClampMin = "1.0"))
	float WalkAnimationSpeed = 85.0f;

	/** Highest play rate, so a fast enemy scuttles rather than blurs. Above it the feet slide. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual", meta = (ClampMin = "0.1"))
	float MaxWalkPlayRate = 3.0f;

	/**
	 * Played once on every attack, then back to walking. The hit lands as the attack starts, so the
	 * strike belongs near its beginning. Empty: no attack motion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UAnimSequence> AttackAnimation;

	/** Played once when it takes a hit it survives, over whatever it was doing. Empty: no flinch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UAnimSequence> HitAnimation;

	/** Played once when it dies; the body then keeps the last pose. Empty: it freezes where it stood. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UAnimSequence> DeathAnimation;
};

/**
 * The models enemies wear. Project Settings > Game > Enemy Visuals.
 *
 * Read when an enemy is built, so it covers every way one comes into the world (the grid run's
 * spawner, test commands, placed actors). The models live in Content, which this repository does
 * not track: without them an enemy keeps its placeholder cube.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Enemy Visuals"))
class NEURALAI_API UEnemyVisualSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** The melee enemy (AEnemyCharacter). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Enemies")
	FEnemyVisual Crawler;

	/** The ranged enemy (ARangedEnemyCharacter). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Enemies")
	FEnemyVisual Spitter;
};
