// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "Climb/EnemyClimbProbe.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyClimbLinkSubsystem.generated.h"

class ULevel;

/**
 * Puts the walls enemies can climb into the navmesh, Left 4 Dead style, so a path takes a wall
 * when it is the shorter way and the controller climbs it where the path says.
 *
 * When play starts, and whenever a level is added (grid run rooms stream in), every static mesh
 * in the level that blocks pawns is probed along each of its sides, every LinkSpacing cm, the way
 * a crawler would (FEnemyClimbProbe). Each climbable spot gets two navmesh links, kept on a
 * transient ANavLinkProxy of its own in that level: up from the foot of the wall to the top (UNavArea_Climb,
 * so it costs more than walking and shooters' paths leave it out), and down the other way (plain
 * area: walking off the edge is all a drop takes). The navmesh is dynamic, so the links take
 * effect once the tiles around them rebuild.
 *
 * It also keeps track of who is climbing where, so two never go up the same spot: a climber that
 * finds its spot taken moves along the wall to the nearest free one (FindFreeSpot).
 */
UCLASS()
class NEURALAI_API UEnemyClimbLinkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Probes Level's geometry and adds its climb links, once per level. Returns the climbs found. */
	int32 BuildLinks(ULevel* Level);

	/**
	 * The climb a link up was made for: its foot (link start) within Tolerance of Start and its
	 * top (link end) within Tolerance of End, along the floor.
	 */
	bool FindLedge(const FVector& Start, const FVector& End, FEnemyClimbLedge& OutLedge, float Tolerance = 60.0f) const;

	/** Every climb with a link, for tests and debugging. */
	TArray<FEnemyClimbLedge> GetLedges() const;

	/**
	 * Wanted itself if no one else is climbing there, otherwise the nearest climbable spot along the
	 * same wall, to either side, a body's width apart, that is free and reaches the same top. False
	 * when there is none within ClimbSpotSearchSteps.
	 */
	bool FindFreeSpot(const ACharacter* Climber, const FEnemyClimbLedge& Wanted, FEnemyClimbLedge& OutLedge) const;

	/** Climber is on its way up Ledge: no one else may take that spot until it lets go. */
	void RegisterClimber(AActor* Climber, const FEnemyClimbLedge& Ledge);
	void UnregisterClimber(const AActor* Climber);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FClimbLink
	{
		TWeakObjectPtr<ULevel> Level;
		FEnemyClimbLedge Ledge;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
	};

	void HandleLevelAdded(ULevel* Level, UWorld* World);
	void HandleLevelRemoved(ULevel* Level, UWorld* World);

	/** The navmesh links of one climb, up and down, on a transient ANavLinkProxy in Level. */
	void SpawnLinkHolder(UWorld* World, ULevel* Level, const FVector& Start, const FVector& End);

	/** A link already starts within this of Ledge's foot (one climb per spot). */
	bool HasLinkNear(const FEnemyClimbLedge& Ledge) const;

	/** Someone other than Asker is climbing with its foot within a body's width of Base. */
	bool IsSpotTaken(const FVector& Base, const AActor* Asker, float Radius) const;

	struct FClimber
	{
		TWeakObjectPtr<AActor> Actor;
		FVector Base = FVector::ZeroVector;
	};
	TArray<FClimber> Climbers;

	TArray<FClimbLink> Links;
	TArray<TWeakObjectPtr<ULevel>> BuiltLevels;
	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelRemovedHandle;
};
