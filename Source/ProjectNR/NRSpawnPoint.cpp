// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRSpawnPoint.h"

#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"

namespace
{
	FColor KindColor(ENRSpawnKind Kind)
	{
		switch (Kind)
		{
		case ENRSpawnKind::Ranged: return FColor(150, 60, 220);
		case ENRSpawnKind::Any: return FColor(240, 200, 40);
		case ENRSpawnKind::Boss: return FColor(230, 30, 30);
		default: return FColor(60, 200, 90);
		}
	}
}

ANRSpawnPoint::ANRSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	// About a crawler's size, standing on the floor: the actor sits at the capsule's middle,
	// where a spawned enemy's own capsule will be.
	Shape = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Shape"));
	Shape->InitCapsuleSize(42.0f, 60.0f);
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetHiddenInGame(true);
	Shape->bDrawOnlyIfSelected = false;
	SetRootComponent(Shape);

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(Shape);
	Arrow->ArrowSize = 1.5f;
	Arrow->SetHiddenInGame(true);

	SetCanBeDamaged(false);
	UpdateEditorLook();
}

void ANRSpawnPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateEditorLook();
}

#if WITH_EDITOR
void ANRSpawnPoint::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateEditorLook();
}
#endif

void ANRSpawnPoint::UpdateEditorLook()
{
	const FColor Color = KindColor(Kind);
	if (Shape)
	{
		Shape->ShapeColor = Color;
		Shape->MarkRenderStateDirty();
	}
	if (Arrow)
	{
		Arrow->SetArrowColor(Color);
	}
}
