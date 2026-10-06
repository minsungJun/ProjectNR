// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRItemPickup.h"

#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NRConsumableComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRPickup, Log, All);

ANRItemPickup::ANRItemPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// Pawns only: the player walks in, nothing else needs to know it is there.
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(80.0f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	RootComponent = Trigger;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(false);
	Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, 10.0f));

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Mesh);
	Glow->SetIntensity(1500.0f);
	Glow->SetAttenuationRadius(180.0f);
	Glow->SetCastShadows(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PotionMesh = CylinderFinder.Object;
	ShieldMesh = CubeFinder.Object;
	TintMaterial = MaterialFinder.Object;
}

void ANRItemPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLook();
}

void ANRItemPickup::BeginPlay()
{
	Super::BeginPlay();

	MeshBaseZ = Mesh->GetRelativeLocation().Z;
	if (Lifetime > 0.0f)
	{
		SetLifeSpan(Lifetime);
	}

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ANRItemPickup::HandleBeginOverlap);

	// Dropped right where the player stands: no begin-overlap will come, so check once now.
	TArray<AActor*> Overlapping;
	Trigger->GetOverlappingActors(Overlapping, APawn::StaticClass());
	for (AActor* Actor : Overlapping)
	{
		if (TryCollect(Actor))
		{
			break;
		}
	}
}

void ANRItemPickup::SetItem(ENRItemType InItemType, int32 InCount)
{
	ItemType = InItemType;
	Count = FMath::Max(InCount, 1);
	ApplyLook();
}

void ANRItemPickup::ApplyLook()
{
	const bool bShield = ItemType == ENRItemType::Shield;
	const FLinearColor Color = bShield ? ShieldColor : PotionColor;

	Mesh->SetStaticMesh(bShield ? ShieldMesh : PotionMesh);
	Mesh->SetRelativeScale3D(bShield ? ShieldScale : PotionScale);
	// A cube stood on its corner reads as a diamond, which tells it apart from the potion.
	Mesh->SetRelativeRotation(bShield ? FRotator(45.0f, 0.0f, 45.0f) : FRotator::ZeroRotator);
	Glow->SetLightColor(Color);

	if (TintMaterial)
	{
		UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0, TintMaterial);
		if (Material)
		{
			Material->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}

void ANRItemPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Mesh->AddWorldRotation(FRotator(0.0f, SpinDegreesPerSecond * DeltaSeconds, 0.0f));
	if (BobPeriod > 0.0f)
	{
		FVector Location = Mesh->GetRelativeLocation();
		Location.Z = MeshBaseZ + BobHeight * FMath::Sin(GetGameTimeSinceCreation() * UE_TWO_PI / BobPeriod);
		Mesh->SetRelativeLocation(Location);
	}
}

void ANRItemPickup::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TryCollect(OtherActor);
}

bool ANRItemPickup::TryCollect(AActor* Collector)
{
	const APawn* Pawn = Cast<APawn>(Collector);
	if (bCollected || !Pawn || !Pawn->IsPlayerControlled())
	{
		return false;
	}

	UNRConsumableComponent* Consumables = Pawn->FindComponentByClass<UNRConsumableComponent>();
	if (!Consumables || !Consumables->AddItem(ItemType, Count))
	{
		return false;
	}

	bCollected = true;
	UE_LOG(LogNRPickup, Log, TEXT("%s picked up %d %s"), *GetNameSafe(Pawn), Count, NRItems::GetDisplayName(ItemType));
	Destroy();
	return true;
}
