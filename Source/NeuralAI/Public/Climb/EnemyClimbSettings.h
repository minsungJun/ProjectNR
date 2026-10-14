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

	/**
	 * Coming down: drops lower than this, in cm, are walked off (a fall); from this high up the
	 * enemy climbs down the wall instead - over the edge backwards, down the face, onto the floor.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Climb", meta = (ClampMin = "0.0"))
	float ClimbDownMinHeight = 250.0f;

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

	/**
	 * Blocks are probed for links only when they never move (Mobility Static) - or when they block
	 * one of these collision channels, which marks them as level blocks made to be climbed over
	 * whatever their mobility: LevelBlock_Traversable is Movable but blocks "Traversable".
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Links")
	TArray<FName> MovableClimbChannels = { TEXT("Traversable") };

	/**
	 * An actor or component with this tag is never climbed, link or no link: for walls that pass
	 * every test but are not meant to be stood on (the tall corridor walls by a doorway, say).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Links")
	FName NoClimbTag = TEXT("NoClimb");

	/** Distance, in cm, between the spots probed along each side of a block or wall. */
	UPROPERTY(Config, EditAnywhere, Category = "Links", meta = (ClampMin = "50.0"))
	float LinkSpacing = 200.0f;

	/** How near, in cm along the floor, an enemy following a path comes to the foot of a climb link before it starts climbing. */
	UPROPERTY(Config, EditAnywhere, Category = "Links", meta = (ClampMin = "20.0"))
	float LinkTriggerDistance = 150.0f;

	/** The target counts as up high once its feet are this many cm above the enemy's. */
	UPROPERTY(Config, EditAnywhere, Category = "On Demand", meta = (ClampMin = "0.0"))
	float ElevatedTargetHeight = 150.0f;

	/**
	 * Climbing where there is no link, to a target up high that can be walked to: only when the
	 * way up the wall is shorter than this share of the walk. Keeps it from climbing for a hair's gain.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "On Demand", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float ShortcutGain = 0.8f;

	/** In weighing a way up a wall, each cm climbed counts as this many cm walked (as UNavArea_Climb's cost). */
	UPROPERTY(Config, EditAnywhere, Category = "On Demand", meta = (ClampMin = "1.0"))
	float ClimbCostFactor = 1.5f;

	/** Seconds between one enemy's looks for a wall to climb where its path does not reach. */
	UPROPERTY(Config, EditAnywhere, Category = "On Demand", meta = (ClampMin = "0.1"))
	float OnDemandInterval = 1.0f;

	/** Seconds a wall given up on is left out of the looks. */
	UPROPERTY(Config, EditAnywhere, Category = "On Demand", meta = (ClampMin = "0.0"))
	float FailedWallCooldown = 5.0f;

	/**
	 * When someone else is already climbing the spot an enemy picked, how many spots (a body's
	 * width apart) to try along the wall each way before waiting its turn.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Crowd", meta = (ClampMin = "0", ClampMax = "10"))
	int32 ClimbSpotSearchSteps = 4;

	/** Seconds between an enemy's looks for a free spot while every one near it is taken. */
	UPROPERTY(Config, EditAnywhere, Category = "Crowd", meta = (ClampMin = "0.05"))
	float ClimbSpotRetryInterval = 0.25f;

	/** Draw what the probe found and the climb under way. Same as NeuralAI.Climb.Debug 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bDrawDebug = false;
};
