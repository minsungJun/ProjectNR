// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EnemyClimbSettings.generated.h"

/**
 * How enemies climb walls, crates and ledges, Left 4 Dead style: walk to the foot of the wall,
 * go straight up its face, pull over the edge, walk on. Project Settings > Game > Enemy Climb.
 * @see UEnemyClimbComponent, FEnemyClimbProbe
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Enemy Climb"))
class NEURALAI_API UEnemyClimbSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** Lower than this, in cm from the feet to the top, is a step: walked, not climbed. */
	UPROPERTY(Config, EditAnywhere, Category = "Reach", meta = (ClampMin = "0.0"))
	float MinClimbHeight = 100.0f;

	/** Higher than this, in cm, is not climbed. */
	UPROPERTY(Config, EditAnywhere, Category = "Reach", meta = (ClampMin = "50.0"))
	float MaxClimbHeight = 600.0f;

	/** How far, in cm, the probe looks for a wall. */
	UPROPERTY(Config, EditAnywhere, Category = "Reach", meta = (ClampMin = "50.0"))
	float WallSearchDistance = 1500.0f;

	/** A surface counts as a wall while its normal points up by no more than this (0 is upright). */
	UPROPERTY(Config, EditAnywhere, Category = "Reach", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float MaxWallNormalZ = 0.3f;

	/** Gap, in cm, between the body and the wall face on the way up. */
	UPROPERTY(Config, EditAnywhere, Category = "Climb", meta = (ClampMin = "0.0"))
	float WallGap = 2.0f;

	/** How far past the edge, in cm, the enemy stands once on top: its radius plus this. */
	UPROPERTY(Config, EditAnywhere, Category = "Climb", meta = (ClampMin = "0.0"))
	float TopInset = 20.0f;

	/** How far above the top, in cm, the feet rise before pulling over the edge. */
	UPROPERTY(Config, EditAnywhere, Category = "Climb", meta = (ClampMin = "0.0"))
	float EdgeClearance = 10.0f;

	/** cm/s up the wall. At 350 a 4 m wall takes a little over a second. */
	UPROPERTY(Config, EditAnywhere, Category = "Climb", meta = (ClampMin = "10.0"))
	float ClimbSpeed = 350.0f;

	/** Seconds to turn to the wall and settle against it at the foot. */
	UPROPERTY(Config, EditAnywhere, Category = "Climb", meta = (ClampMin = "0.0"))
	float AlignTime = 0.15f;

	/** Seconds to pull over the edge and stand up on top. */
	UPROPERTY(Config, EditAnywhere, Category = "Climb", meta = (ClampMin = "0.01"))
	float MantleTime = 0.35f;

	/** Within this, in cm, of the foot of the wall the walk there is over. */
	UPROPERTY(Config, EditAnywhere, Category = "Climb", meta = (ClampMin = "5.0"))
	float BaseArrivalRadius = 30.0f;

	/** Seconds without getting anywhere - walking to the wall, going up, over the edge - before giving up and dropping. */
	UPROPERTY(Config, EditAnywhere, Category = "Climb", meta = (ClampMin = "0.1"))
	float StuckTime = 2.0f;

	/**
	 * Put the walls that can be climbed into the navmesh as links, so paths go up them when that is
	 * the shorter way (UEnemyClimbLinkSubsystem). Read when play starts.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Links")
	bool bBuildClimbLinks = true;

	/** Distance, in cm, between the spots probed along each side of a block or wall. */
	UPROPERTY(Config, EditAnywhere, Category = "Links", meta = (ClampMin = "50.0"))
	float LinkSpacing = 200.0f;

	/** How near, in cm along the floor, an enemy following a path comes to the foot of a climb link before it starts climbing. */
	UPROPERTY(Config, EditAnywhere, Category = "Links", meta = (ClampMin = "20.0"))
	float LinkTriggerDistance = 150.0f;

	/** Draw what the probe found and the climb under way. Same as NeuralAI.Climb.Debug 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bDrawDebug = false;
};
