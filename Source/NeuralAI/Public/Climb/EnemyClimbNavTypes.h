// Neural Enemy AI prototype module.

#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "EnemyClimbNavTypes.generated.h"

/**
 * Navmesh area of the links up a wall (UEnemyClimbLinkSubsystem). A link costs its length times
 * DefaultCost, so with 1.5 a 4 m climb weighs about as much as 6 m of floor: a path takes the wall
 * when walking round to stairs or a ramp is longer than that, and the stairs when they are shorter.
 */
UCLASS(Config = Engine)
class NEURALAI_API UNavArea_Climb : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_Climb();
};

/**
 * Navmesh area of the links down from a climbable top to the foot of the wall. Plain cost: the
 * way down is no slower than walking. Enemies that climb go down the wall where it is
 * ClimbDownMinHeight or more; the rest (and every enemy where it is lower) walk off the edge.
 */
UCLASS(Config = Engine)
class NEURALAI_API UNavArea_ClimbDown : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_ClimbDown();
};

/** Paths for enemies that do not climb (shooters): every climb link left out. */
UCLASS()
class NEURALAI_API UNavFilter_EnemyNoClimb : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UNavFilter_EnemyNoClimb();
};
