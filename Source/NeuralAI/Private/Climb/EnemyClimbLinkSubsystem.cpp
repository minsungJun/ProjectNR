// Neural Enemy AI prototype module.

#include "Climb/EnemyClimbLinkSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Climb/EnemyClimbNavTypes.h"
#include "Climb/EnemyClimbSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/NavLinkProxy.h"
#include "NavAreas/NavArea_Default.h"
#include "NeuralAI.h"

UNavArea_Climb::UNavArea_Climb()
{
	DefaultCost = 1.5f;
	DrawColor = FColor(255, 140, 0);
}

UNavFilter_EnemyNoClimb::UNavFilter_EnemyNoClimb()
{
	FNavigationFilterArea& Climb = Areas.AddDefaulted_GetRef();
	Climb.AreaClass = UNavArea_Climb::StaticClass();
	Climb.bIsExcluded = true;
}

bool UEnemyClimbLinkSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UEnemyClimbLinkSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!GetDefault<UEnemyClimbSettings>()->bBuildClimbLinks)
	{
		return;
	}
	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UEnemyClimbLinkSubsystem::HandleLevelAdded);
	LevelRemovedHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UEnemyClimbLinkSubsystem::HandleLevelRemoved);
	for (ULevel* Level : InWorld.GetLevels())
	{
		if (Level && Level->bIsVisible)
		{
			BuildLinks(Level);
		}
	}
}

void UEnemyClimbLinkSubsystem::Deinitialize()
{
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedHandle);
	Super::Deinitialize();
}

void UEnemyClimbLinkSubsystem::HandleLevelAdded(ULevel* Level, UWorld* World)
{
	if (World == GetWorld())
	{
		BuildLinks(Level);
	}
}

void UEnemyClimbLinkSubsystem::HandleLevelRemoved(ULevel* Level, UWorld* World)
{
	if (World != GetWorld())
	{
		return;
	}
	// Null means every level is going.
	Links.RemoveAll([Level](const FClimbLink& Link) { return !Level || !Link.Level.IsValid() || Link.Level.Get() == Level; });
	BuiltLevels.RemoveAll([Level](const TWeakObjectPtr<ULevel>& Built) { return !Level || !Built.IsValid() || Built.Get() == Level; });
}

bool UEnemyClimbLinkSubsystem::HasLinkNear(const FEnemyClimbLedge& Ledge) const
{
	const float Spacing = GetDefault<UEnemyClimbSettings>()->LinkSpacing * 0.5f;
	return Links.ContainsByPredicate([&Ledge, Spacing](const FClimbLink& Link)
	{
		return FVector::Dist2D(Link.Ledge.BaseCenter, Ledge.BaseCenter) < Spacing
			&& FMath::Abs(Link.Ledge.TopZ - Ledge.TopZ) < 50.0f;
	});
}

int32 UEnemyClimbLinkSubsystem::BuildLinks(ULevel* Level)
{
	UWorld* World = GetWorld();
	if (!World || !Level || BuiltLevels.Contains(Level))
	{
		return 0;
	}
	BuiltLevels.Add(Level);

	// Probe as a crawler would: its capsule, its floors.
	const UEnemyClimbSettings& Settings = *GetDefault<UEnemyClimbSettings>();
	const AEnemyCharacter* Crawler = GetDefault<AEnemyCharacter>();
	const FEnemyClimbProbe::FBody Body = FEnemyClimbProbe::GetBody(Crawler);
	const float WalkableFloorZ = Crawler->GetCharacterMovement() ? Crawler->GetCharacterMovement()->GetWalkableFloorZ() : 0.71f;
	const double StartTime = FPlatformTime::Seconds();

	int32 Probes = 0;
	// A copy: the link holders go into this same list.
	const TArray<TObjectPtr<AActor>> Actors = Level->Actors;
	for (AActor* Actor : Actors)
	{
		if (!Actor || Actor->IsA<APawn>())
		{
			continue;
		}
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			// Fixed level geometry only: doors and other movers would leave links hanging.
			if (!Mesh->GetStaticMesh() || Mesh->Mobility != EComponentMobility::Static || !Mesh->IsCollisionEnabled()
				|| Mesh->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
			{
				continue;
			}
			const FBox Local = Mesh->GetStaticMesh()->GetBoundingBox();
			const FTransform& Transform = Mesh->GetComponentTransform();
			const FBox Bounds = Local.TransformBy(Transform);
			if (Bounds.GetSize().Z < Settings.MinClimbHeight - FEnemyClimbProbe::HeightTolerance)
			{
				continue;
			}

			// Each upright side of the box, sampled along its length clear of the corners, probed
			// straight at from just outside it, feet at the bottom of the box.
			const FVector Center = Local.GetCenter();
			const FVector Extent = Local.GetExtent();
			const FVector Scale = Transform.GetScale3D().GetAbs();
			for (int32 Side = 0; Side < 4; ++Side)
			{
				const bool bAlongX = Side < 2;
				const float Sign = Side % 2 == 0 ? 1.0f : -1.0f;
				const FVector LocalNormal = bAlongX ? FVector(Sign, 0.0f, 0.0f) : FVector(0.0f, Sign, 0.0f);
				const FVector LocalAxis = bAlongX ? FVector(0.0f, 1.0f, 0.0f) : FVector(1.0f, 0.0f, 0.0f);
				const FVector Normal = Transform.TransformVectorNoScale(LocalNormal);
				if (!FEnemyClimbProbe::IsUpright(Normal, Settings.MaxWallNormalZ))
				{
					continue;
				}
				const FVector Out = Normal.GetSafeNormal2D();
				const float HalfLength = bAlongX ? Extent.Y * Scale.Y : Extent.X * Scale.X;
				const float Usable = HalfLength - Body.Radius - 10.0f;
				const int32 Count = Usable > 0.0f ? FMath::FloorToInt32(2.0f * Usable / Settings.LinkSpacing) + 1 : 1;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const float Along = Count > 1 ? -Usable + 2.0f * Usable * Index / (Count - 1) : 0.0f;
					const FVector LocalFace = Center + LocalNormal * (bAlongX ? Extent.X : Extent.Y) + LocalAxis * (Along / (bAlongX ? Scale.Y : Scale.X));
					const FVector Face = Transform.TransformPosition(LocalFace);
					const FVector Start(Face.X + Out.X * (Body.Radius + Settings.WallGap + 30.0f), Face.Y + Out.Y * (Body.Radius + Settings.WallGap + 30.0f),
						Bounds.Min.Z + Body.HalfHeight + 5.0f);

					FEnemyClimbLedge Ledge;
					++Probes;
					if (FEnemyClimbProbe::ProbeFrom(World, Body, Start, -Out, Settings, WalkableFloorZ, nullptr, Ledge) != EEnemyClimbProbeResult::Ok
						|| HasLinkNear(Ledge))
					{
						continue;
					}
					FClimbLink& Link = Links.AddDefaulted_GetRef();
					Link.Level = Level;
					Link.Ledge = Ledge;
					Link.Start = FVector(Ledge.BaseCenter.X, Ledge.BaseCenter.Y, Ledge.FloorZ + 5.0f);
					Link.End = FVector(Ledge.StandCenter.X, Ledge.StandCenter.Y, Ledge.TopZ + 5.0f);

					SpawnLinkHolder(World, Level, Link.Start, Link.End);
				}
			}
		}
	}

	const int32 Found = Links.FilterByPredicate([Level](const FClimbLink& Link) { return Link.Level.Get() == Level; }).Num();
	UE_LOG(LogNeuralAI, Log, TEXT("Climb links: %d climb(s) in %s from %d probes, %.1f ms"), Found, *GetNameSafe(Level->GetOuter()),
		Probes, (FPlatformTime::Seconds() - StartTime) * 1000.0);
	return Found;
}

void UEnemyClimbLinkSubsystem::SpawnLinkHolder(UWorld* World, ULevel* Level, const FVector& Start, const FVector& End)
{
	// A holder per climb, standing at its foot, in the level, so the links go when the room does.
	// (One holder for a whole level, its links spread over the map, put only a handful of them in
	// the navmesh.)
	FActorSpawnParameters Params;
	Params.OverrideLevel = Level;
	Params.bDeferConstruction = true;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform At(Start);
	ANavLinkProxy* Holder = World->SpawnActor<ANavLinkProxy>(ANavLinkProxy::StaticClass(), At, Params);
	if (!Holder)
	{
		return;
	}
	const FVector Top = End - Start;
	Holder->PointLinks.Reset();
	FNavigationLink& Up = Holder->PointLinks.Emplace_GetRef(FVector::ZeroVector, Top);
	Up.Direction = ENavLinkDirection::LeftToRight;
	Up.SetAreaClass(UNavArea_Climb::StaticClass());
	FNavigationLink& Down = Holder->PointLinks.Emplace_GetRef(Top, FVector::ZeroVector);
	Down.Direction = ENavLinkDirection::LeftToRight;
	Down.SetAreaClass(UNavArea_Default::StaticClass());
	Holder->FinishSpawning(At);
}

bool UEnemyClimbLinkSubsystem::IsSpotTaken(const FVector& Base, const AActor* Asker, float Radius) const
{
	return Climbers.ContainsByPredicate([&Base, Asker, Radius](const FClimber& Climber)
	{
		return Climber.Actor.IsValid() && Climber.Actor.Get() != Asker && FVector::Dist2D(Climber.Base, Base) < Radius * 2.0f + 10.0f;
	});
}

bool UEnemyClimbLinkSubsystem::FindFreeSpot(const ACharacter* Climber, const FEnemyClimbLedge& Wanted, FEnemyClimbLedge& OutLedge) const
{
	const FEnemyClimbProbe::FBody Body = FEnemyClimbProbe::GetBody(Climber);
	if (!IsSpotTaken(Wanted.BaseCenter, Climber, Body.Radius))
	{
		OutLedge = Wanted;
		return true;
	}

	// Along the wall, nearest first, alternating sides, a body's width and a little apart, each
	// spot probed straight at the wall as if standing just out from it.
	const UEnemyClimbSettings& Settings = *GetDefault<UEnemyClimbSettings>();
	const FVector Along = FVector::CrossProduct(Wanted.WallNormal, FVector::UpVector).GetSafeNormal();
	const float Step = Body.Radius * 2.0f + 20.0f;
	const UCharacterMovementComponent* Movement = Climber ? Climber->GetCharacterMovement() : nullptr;
	for (int32 Index = 1; Index <= Settings.ClimbSpotSearchSteps; ++Index)
	{
		for (const float Side : { 1.0f, -1.0f })
		{
			const FVector Start = Wanted.BaseCenter + Along * (Side * Index * Step) + Wanted.WallNormal * 30.0f;
			FEnemyClimbLedge Spot;
			if (FEnemyClimbProbe::ProbeFrom(GetWorld(), Body, Start, -Wanted.WallNormal, Settings,
					Movement ? Movement->GetWalkableFloorZ() : 0.71f, Climber, Spot) == EEnemyClimbProbeResult::Ok
				&& FMath::Abs(Spot.TopZ - Wanted.TopZ) < 50.0f && !IsSpotTaken(Spot.BaseCenter, Climber, Body.Radius))
			{
				OutLedge = Spot;
				return true;
			}
		}
	}
	return false;
}

void UEnemyClimbLinkSubsystem::RegisterClimber(AActor* Climber, const FEnemyClimbLedge& Ledge)
{
	UnregisterClimber(Climber);
	Climbers.Add({ Climber, Ledge.BaseCenter });
}

void UEnemyClimbLinkSubsystem::UnregisterClimber(const AActor* Climber)
{
	Climbers.RemoveAll([Climber](const FClimber& Entry) { return !Entry.Actor.IsValid() || Entry.Actor.Get() == Climber; });
}

bool UEnemyClimbLinkSubsystem::FindLedge(const FVector& Start, const FVector& End, FEnemyClimbLedge& OutLedge, float Tolerance) const
{
	float Best = TNumericLimits<float>::Max();
	for (const FClimbLink& Link : Links)
	{
		const float StartGap = FVector::Dist2D(Link.Start, Start);
		const float EndGap = FVector::Dist2D(Link.End, End);
		if (StartGap <= Tolerance && EndGap <= Tolerance && FMath::Abs(Link.End.Z - End.Z) <= 100.0f && StartGap + EndGap < Best)
		{
			Best = StartGap + EndGap;
			OutLedge = Link.Ledge;
		}
	}
	return Best < TNumericLimits<float>::Max();
}

TArray<FEnemyClimbLedge> UEnemyClimbLinkSubsystem::GetLedges() const
{
	TArray<FEnemyClimbLedge> Ledges;
	for (const FClimbLink& Link : Links)
	{
		Ledges.Add(Link.Ledge);
	}
	return Ledges;
}
