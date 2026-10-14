// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRSpawnMarker.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ANRSpawnMarker::ANRSpawnMarker()
{
	PrimaryActorTick.bCanEverTick = true;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	Disc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Disc"));
	Disc->SetupAttachment(GetRootComponent());
	Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Disc->SetCastShadow(false);
	Disc->SetStaticMesh(CylinderFinder.Object);

	// BasicShapes/Cylinder is 100 cm across and tall, pivot in the middle: 8 cm wide, 160 cm high.
	Pillar = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pillar"));
	Pillar->SetupAttachment(GetRootComponent());
	Pillar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pillar->SetCastShadow(false);
	Pillar->SetStaticMesh(CylinderFinder.Object);
	Pillar->SetRelativeScale3D(FVector(0.08f, 0.08f, 1.6f));
	Pillar->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	// The basic meshes' own material has no "Color" to tint; this one does.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	TintMaterial = MaterialFinder.Object;

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(GetRootComponent());
	Glow->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	Glow->SetAttenuationRadius(300.0f);
	Glow->SetCastShadows(false);
	Glow->SetIntensity(0.0f);
}

void ANRSpawnMarker::Show(const FLinearColor& Color, float Seconds)
{
	Duration = FMath::Max(Seconds, 0.05f);
	Elapsed = 0.0f;
	if (TintMaterial)
	{
		for (UStaticMeshComponent* Mesh : { Disc.Get(), Pillar.Get() })
		{
			if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0, TintMaterial))
			{
				Material->SetVectorParameterValue(TEXT("Color"), Color);
			}
		}
	}
	Glow->SetLightColor(Color);
	SetLifeSpan(Duration + 0.05f);
	Tick(0.0f);
}

void ANRSpawnMarker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	// Grows from a dot to an enemy's footprint, brightening as the moment comes.
	const float Alpha = FMath::Clamp(Elapsed / Duration, 0.0f, 1.0f);
	const float Width = FMath::Lerp(0.3f, 1.0f, Alpha);
	// BasicShapes/Cylinder is 100 cm across and tall: a thin disc on the floor.
	Disc->SetRelativeScale3D(FVector(Width, Width, 0.03f));
	Glow->SetIntensity(FMath::Lerp(5.0f, 40.0f, Alpha));
}
