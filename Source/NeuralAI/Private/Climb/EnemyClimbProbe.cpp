// Neural Enemy AI prototype module.

#include "Climb/EnemyClimbProbe.h"

#include "Climb/EnemyClimbSettings.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace EnemyClimbProbe
{
	/** Only level geometry counts: walls, crates, ledges. Pawns are neither climbed nor in the way of a probe. */
	FCollisionObjectQueryParams GeometryQuery()
	{
		FCollisionObjectQueryParams Query;
		Query.AddObjectTypesToQuery(ECC_WorldStatic);
		Query.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Query;
	}
}

const TCHAR* FEnemyClimbProbe::ToString(EEnemyClimbProbeResult Result)
{
	switch (Result)
	{
	case EEnemyClimbProbeResult::Ok:             return TEXT("ok");
	case EEnemyClimbProbeResult::NoWall:         return TEXT("no wall");
	case EEnemyClimbProbeResult::NotUpright:     return TEXT("not upright");
	case EEnemyClimbProbeResult::NoFloor:        return TEXT("no floor at the foot");
	case EEnemyClimbProbeResult::NoTop:          return TEXT("no top in reach");
	case EEnemyClimbProbeResult::TooLow:         return TEXT("too low");
	case EEnemyClimbProbeResult::TooHigh:        return TEXT("too high");
	case EEnemyClimbProbeResult::TopNotWalkable: return TEXT("top too steep");
	case EEnemyClimbProbeResult::NoRoomOnTop:    return TEXT("no room on top");
	case EEnemyClimbProbeResult::Blocked:        return TEXT("blocked on the way");
	}
	return TEXT("?");
}

bool FEnemyClimbProbe::IsUpright(const FVector& Normal, float MaxNormalZ)
{
	return FMath::Abs(Normal.Z) <= MaxNormalZ && !Normal.GetSafeNormal2D().IsNearlyZero();
}

EEnemyClimbProbeResult FEnemyClimbProbe::CheckHeight(float Height, float MinHeight, float MaxHeight)
{
	if (Height < MinHeight - HeightTolerance)
	{
		return EEnemyClimbProbeResult::TooLow;
	}
	return Height > MaxHeight + HeightTolerance ? EEnemyClimbProbeResult::TooHigh : EEnemyClimbProbeResult::Ok;
}

FVector FEnemyClimbProbe::GetBaseCenter(const FVector& WallPoint, const FVector& WallNormal, float FloorZ, const FBody& Body, float Gap)
{
	const FVector Out = WallNormal.GetSafeNormal2D();
	const FVector Base = WallPoint + Out * (Body.Radius + Gap);
	return FVector(Base.X, Base.Y, FloorZ + Body.HalfHeight + 2.0f);
}

FVector FEnemyClimbProbe::GetEdgeCenter(const FVector& BaseCenter, float TopZ, const FBody& Body, float Clearance)
{
	return FVector(BaseCenter.X, BaseCenter.Y, TopZ + Clearance + Body.HalfHeight);
}

FVector FEnemyClimbProbe::GetStandCenter(const FVector& WallPoint, const FVector& WallNormal, float TopZ, const FBody& Body, float Inset)
{
	const FVector In = -WallNormal.GetSafeNormal2D();
	const FVector Stand = WallPoint + In * (Body.Radius + Inset);
	return FVector(Stand.X, Stand.Y, TopZ + Body.HalfHeight + 2.0f);
}

FVector FEnemyClimbProbe::GetMantlePoint(const FVector& Edge, const FVector& Stand, float Alpha)
{
	const float A = FMath::Clamp(Alpha, 0.0f, 1.0f);
	const float Along = FMath::SmoothStep(0.0f, 1.0f, A);
	const float Down = FMath::Clamp((A - 0.5f) * 2.0f, 0.0f, 1.0f);
	return FVector(
		FMath::Lerp(Edge.X, Stand.X, Along),
		FMath::Lerp(Edge.Y, Stand.Y, Along),
		FMath::Lerp(Edge.Z, Stand.Z, Down));
}

FEnemyClimbProbe::FBody FEnemyClimbProbe::GetBody(const ACharacter* Character)
{
	FBody Body;
	if (const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr)
	{
		Body.Radius = Capsule->GetScaledCapsuleRadius();
		Body.HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	}
	return Body;
}

EEnemyClimbProbeResult FEnemyClimbProbe::ProbeDirection(const UWorld* World, const ACharacter* Character, const FVector& Direction,
	const UEnemyClimbSettings& Settings, FEnemyClimbLedge& OutLedge)
{
	if (!Character)
	{
		OutLedge = FEnemyClimbLedge();
		return EEnemyClimbProbeResult::NoWall;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	return ProbeFrom(World, GetBody(Character), Character->GetActorLocation(), Direction, Settings,
		Movement ? Movement->GetWalkableFloorZ() : 0.71f, Character, OutLedge);
}

EEnemyClimbProbeResult FEnemyClimbProbe::ProbeFrom(const UWorld* World, const FBody& Body, const FVector& Start, const FVector& Direction,
	const UEnemyClimbSettings& Settings, float WalkableFloorZ, const AActor* Ignore, FEnemyClimbLedge& OutLedge)
{
	OutLedge = FEnemyClimbLedge();
	const FVector Flat = Direction.GetSafeNormal2D();
	if (!World || Flat.IsNearlyZero())
	{
		return EEnemyClimbProbeResult::NoWall;
	}
	const FCollisionObjectQueryParams Geometry = EnemyClimbProbe::GeometryQuery();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyClimbProbe), false, Ignore);

	// The wall: straight out at body height, an upright face.
	FHitResult WallHit;
	if (!World->LineTraceSingleByObjectType(WallHit, Start, Start + Flat * Settings.WallSearchDistance, Geometry, Params))
	{
		return EEnemyClimbProbeResult::NoWall;
	}
	OutLedge.Wall = WallHit.GetActor();
	OutLedge.WallPoint = WallHit.ImpactPoint;
	if (!IsUpright(WallHit.ImpactNormal, Settings.MaxWallNormalZ))
	{
		return EEnemyClimbProbeResult::NotUpright;
	}
	OutLedge.WallNormal = WallHit.ImpactNormal.GetSafeNormal2D();

	// The floor at its foot.
	const FVector FootProbe = WallHit.ImpactPoint + OutLedge.WallNormal * (Body.Radius + Settings.WallGap);
	FHitResult FloorHit;
	if (!World->LineTraceSingleByObjectType(FloorHit, FVector(FootProbe.X, FootProbe.Y, Start.Z + 50.0f),
		FVector(FootProbe.X, FootProbe.Y, Start.Z - Body.HalfHeight - 300.0f), Geometry, Params))
	{
		return EEnemyClimbProbeResult::NoFloor;
	}
	OutLedge.FloorZ = FloorHit.ImpactPoint.Z;
	OutLedge.BaseCenter = GetBaseCenter(OutLedge.WallPoint, OutLedge.WallNormal, OutLedge.FloorZ, Body, Settings.WallGap);

	// The top: straight down onto where it would stand, from as high as it may climb and a body
	// over. Starting inside something means the wall goes on above that.
	const FVector StandProbe = OutLedge.WallPoint - OutLedge.WallNormal * (Body.Radius + Settings.TopInset);
	const FVector TopStart(StandProbe.X, StandProbe.Y, OutLedge.FloorZ + Settings.MaxClimbHeight + Body.HalfHeight * 2.0f + 50.0f);
	if (World->OverlapAnyTestByObjectType(TopStart, FQuat::Identity, Geometry, FCollisionShape::MakeSphere(5.0f), Params))
	{
		OutLedge.TopZ = TopStart.Z;
		return EEnemyClimbProbeResult::NoTop;
	}
	FHitResult TopHit;
	if (!World->LineTraceSingleByObjectType(TopHit, TopStart, FVector(StandProbe.X, StandProbe.Y, OutLedge.FloorZ + 1.0f), Geometry, Params))
	{
		return EEnemyClimbProbeResult::NoTop;
	}
	OutLedge.TopZ = TopHit.ImpactPoint.Z;
	OutLedge.EdgeCenter = GetEdgeCenter(OutLedge.BaseCenter, OutLedge.TopZ, Body, Settings.EdgeClearance);
	OutLedge.StandCenter = GetStandCenter(OutLedge.WallPoint, OutLedge.WallNormal, OutLedge.TopZ, Body, Settings.TopInset);

	const EEnemyClimbProbeResult HeightResult = CheckHeight(OutLedge.GetHeight(), Settings.MinClimbHeight, Settings.MaxClimbHeight);
	if (HeightResult != EEnemyClimbProbeResult::Ok)
	{
		return HeightResult;
	}
	if (TopHit.ImpactNormal.Z < WalkableFloorZ)
	{
		return EEnemyClimbProbeResult::TopNotWalkable;
	}

	// Room to stand on top, and a clear way up the face and in over the edge.
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Body.Radius, Body.HalfHeight);
	if (World->OverlapAnyTestByObjectType(OutLedge.StandCenter, FQuat::Identity, Geometry, Capsule, Params))
	{
		return EEnemyClimbProbeResult::NoRoomOnTop;
	}
	FHitResult PathHit;
	if (World->SweepSingleByObjectType(PathHit, OutLedge.BaseCenter, OutLedge.EdgeCenter, FQuat::Identity, Geometry, Capsule, Params)
		|| World->SweepSingleByObjectType(PathHit, OutLedge.EdgeCenter, OutLedge.StandCenter, FQuat::Identity, Geometry, Capsule, Params))
	{
		return EEnemyClimbProbeResult::Blocked;
	}
	return EEnemyClimbProbeResult::Ok;
}

EEnemyClimbProbeResult FEnemyClimbProbe::FindNearest(const UWorld* World, const ACharacter* Character, const UEnemyClimbSettings& Settings,
	int32 Directions, FEnemyClimbLedge& OutLedge)
{
	if (!Character)
	{
		return EEnemyClimbProbeResult::NoWall;
	}
	// The most telling failure, for the log when nothing is climbable: a wall that was there but
	// too high says more than the empty directions round it.
	EEnemyClimbProbeResult Best = EEnemyClimbProbeResult::NoWall;
	float BestDistance = TNumericLimits<float>::Max();
	const FVector From = Character->GetActorLocation();
	const int32 Count = FMath::Max(Directions, 1);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Yaw = FMath::DegreesToRadians(360.0f * Index / Count);
		FEnemyClimbLedge Ledge;
		const EEnemyClimbProbeResult Result = ProbeDirection(World, Character, FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0f), Settings, Ledge);
		if (Result == EEnemyClimbProbeResult::Ok)
		{
			if (Best != EEnemyClimbProbeResult::Ok || Ledge.GetDistanceTo(From) < BestDistance)
			{
				Best = Result;
				BestDistance = Ledge.GetDistanceTo(From);
				OutLedge = Ledge;
			}
		}
		else if (Best != EEnemyClimbProbeResult::Ok && Result > Best)
		{
			Best = Result;
			OutLedge = Ledge;
		}
	}
	return Best;
}

void FEnemyClimbProbe::DrawDebug(const UWorld* World, const FEnemyClimbLedge& Ledge, EEnemyClimbProbeResult Result, float Duration)
{
#if ENABLE_DRAW_DEBUG
	if (!World || Ledge.WallPoint.IsZero())
	{
		return;
	}
	const bool bOk = Result == EEnemyClimbProbeResult::Ok;
	const FColor Color = bOk ? FColor::Green : FColor::Red;
	DrawDebugSphere(World, Ledge.WallPoint, 12.0f, 8, Color, false, Duration, 0, 2.0f);
	DrawDebugLine(World, Ledge.WallPoint, Ledge.WallPoint + Ledge.WallNormal * 60.0f, FColor::Cyan, false, Duration, 0, 2.0f);
	if (Ledge.BaseCenter.IsZero())
	{
		return;
	}
	DrawDebugSphere(World, Ledge.BaseCenter, 15.0f, 8, Color, false, Duration, 0, 2.0f);
	if (!Ledge.EdgeCenter.IsZero())
	{
		DrawDebugLine(World, Ledge.BaseCenter, Ledge.EdgeCenter, Color, false, Duration, 0, 3.0f);
		DrawDebugLine(World, Ledge.EdgeCenter, Ledge.StandCenter, Color, false, Duration, 0, 3.0f);
		DrawDebugSphere(World, Ledge.StandCenter, 20.0f, 8, Color, false, Duration, 0, 2.0f);
	}
	DrawDebugString(World, Ledge.WallPoint + FVector(0.0f, 0.0f, 40.0f),
		FString::Printf(TEXT("%s %.0f cm"), FEnemyClimbProbe::ToString(Result), Ledge.GetHeight()), nullptr, Color, Duration);
#endif
}
