// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRChestSpawnPoint.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"

ANRChestSpawnPoint::ANRChestSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	// The origin is on the floor, where the chest's own origin will be.
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// BP_NR_LootCrate_01's size at its 0.75 scale (front along +X): 104 deep, 155 wide, 80 high.
	Shape = CreateDefaultSubobject<UBoxComponent>(TEXT("Shape"));
	Shape->SetupAttachment(Root);
	Shape->InitBoxExtent(FVector(52.0f, 78.0f, 40.0f));
	Shape->SetRelativeLocation(FVector(0.0f, 0.0f, 40.0f));
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetCanEverAffectNavigation(false);
	Shape->SetHiddenInGame(true);
	Shape->ShapeColor = FColor(240, 170, 40);
	Shape->SetLineThickness(2.0f);

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(Root);
	Arrow->SetRelativeLocation(FVector(0.0f, 0.0f, 20.0f));
	Arrow->ArrowSize = 1.5f;
	Arrow->SetArrowColor(FColor(240, 170, 40));
	Arrow->SetHiddenInGame(true);

	SetCanBeDamaged(false);
}
