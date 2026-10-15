// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"

class ACharacter;
class AActor;
class UEnemyClimbSettings;
class UPrimitiveComponent;
class UWorld;

/** Why a wall can or cannot be climbed. */
enum class EEnemyClimbProbeResult : uint8
{
	Ok,
	/** Nothing within reach that way. */
	NoWall,
	/** Hit a slope or a floor, not a wall. */
	NotUpright,
	/** No floor at the foot of the wall to start from. */
	NoFloor,
	/** No top within MaxClimbHeight: the wall goes on, or the top is under something. */
	NoTop,
	/** The top is a step, walked rather than climbed. */
	TooLow,
	TooHigh,
	/** The top is too steep to stand on. */
	TopNotWalkable,
	/** Not enough room to stand on top. */
	NoRoomOnTop,
	/** Something sticks out of the wall on the way up, or over the edge. */
	Blocked,
	/** The wall is tagged not to be climbed (NoClimbTag). */
	Excluded
};

/**
 * A wall to climb and the points of the climb, as capsule centres: stand at the foot (Base), go
 * straight up to just over the top (Edge), pull in over the edge to stand on top (Stand).
 */
struct NEURALAI_API FEnemyClimbLedge
{
	TWeakObjectPtr<AActor> Wall;

	/** Where the probe met the wall face. */
	FVector WallPoint = FVector::ZeroVector;

	/** Out of the wall, flat. */
	FVector WallNormal = FVector::ForwardVector;

	float FloorZ = 0.0f;
	float TopZ = 0.0f;

	FVector BaseCenter = FVector::ZeroVector;
	FVector EdgeCenter = FVector::ZeroVector;
	FVector StandCenter = FVector::ZeroVector;

	float GetHeight() const { return TopZ - FloorZ; }

	/** Distance along the floor from From to the foot of the wall. */
	float GetDistanceTo(const FVector& From) const { return FVector::Dist2D(From, BaseCenter); }
};

/**
 * Finds walls an enemy can climb, Left 4 Dead style: an upright face, a top it can stand on
 * between MinClimbHeight and MaxClimbHeight up, and nothing in the way up or over the edge.
 *
 * The geometry (where the climb points go, which heights count) is pure and tested by
 * NeuralAI.Climb.Probe; the rest traces the world.
 */
struct NEURALAI_API FEnemyClimbProbe
{
	/** The climbing body: capsule radius and half height. */
	struct FBody
	{
		float Radius = 42.0f;
		float HalfHeight = 60.0f;
	};

	static const TCHAR* ToString(EEnemyClimbProbeResult Result);

	/** A surface with this normal is a wall: it leans up by no more than MaxNormalZ. */
	static bool IsUpright(const FVector& Normal, float MaxNormalZ);

	/** The hit actor or component carries the settings' NoClimbTag. */
	static bool IsExcluded(const AActor* Actor, const UPrimitiveComponent* Component);

	/**
	 * Leeway on the height limits, in cm: a floor a hair below zero must not put a 6 m wall over a
	 * 6 m limit (Lvl_ClimbAutoTest's floor top is at -0.5).
	 */
	static constexpr float HeightTolerance = 2.0f;

	/** Ok, TooLow or TooHigh for a climb of Height cm, give or take HeightTolerance. */
	static EEnemyClimbProbeResult CheckHeight(float Height, float MinHeight, float MaxHeight);

	/** Capsule centre standing at the foot of the wall, Gap cm off its face. */
	static FVector GetBaseCenter(const FVector& WallPoint, const FVector& WallNormal, float FloorZ, const FBody& Body, float Gap);

	/** Capsule centre at the top of the climb: over the foot, feet Clearance cm above the top. */
	static FVector GetEdgeCenter(const FVector& BaseCenter, float TopZ, const FBody& Body, float Clearance);

	/** Capsule centre standing on top, its near side Inset cm in from the edge. */
	static FVector GetStandCenter(const FVector& WallPoint, const FVector& WallNormal, float TopZ, const FBody& Body, float Inset);

	/**
	 * Where the body is Alpha (0..1) of the way over the edge from Edge to Stand: in along the
	 * floor first, down onto the top only once past the edge, so it does not catch the corner.
	 */
	static FVector GetMantlePoint(const FVector& Edge, const FVector& Stand, float Alpha);

	/** Probes for a wall straight along Direction (flattened) from the character. */
	static EEnemyClimbProbeResult ProbeDirection(const UWorld* World, const ACharacter* Character, const FVector& Direction,
		const UEnemyClimbSettings& Settings, FEnemyClimbLedge& OutLedge);

	/**
	 * The same for a body of the given size whose capsule centre is at Start, standing on floors
	 * no steeper than WalkableFloorZ, ignoring Ignore: for probing where no enemy stands yet.
	 */
	static EEnemyClimbProbeResult ProbeFrom(const UWorld* World, const FBody& Body, const FVector& Start, const FVector& Direction,
		const UEnemyClimbSettings& Settings, float WalkableFloorZ, const AActor* Ignore, FEnemyClimbLedge& OutLedge);

	/** Probes Directions ways round the character and gives the climbable wall with the shortest walk to its foot. */
	static EEnemyClimbProbeResult FindNearest(const UWorld* World, const ACharacter* Character, const UEnemyClimbSettings& Settings,
		int32 Directions, FEnemyClimbLedge& OutLedge);

	static FBody GetBody(const ACharacter* Character);

	/** Wall point, foot, edge and stand, joined; green when climbable, red otherwise. */
	static void DrawDebug(const UWorld* World, const FEnemyClimbLedge& Ledge, EEnemyClimbProbeResult Result, float Duration);
};
