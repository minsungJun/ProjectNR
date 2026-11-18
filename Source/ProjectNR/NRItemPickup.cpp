// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRItemPickup.h"
#include "NRMetaProgression.h"

#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NRConsumableComponent.h"
#include "NRLootSettings.h"
#include "NRRunSubsystem.h"
#include "NRWeaponPartComponent.h"
#include "NRWeaponPartData.h"
#include "NRWeaponComponent.h"
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
	AmmoMesh = CubeFinder.Object;
	MoneyMesh = CylinderFinder.Object;
	PartMesh = CubeFinder.Object;
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

void ANRItemPickup::SetPart(UNRWeaponPartData* InPart, ENRWeaponPartLevel InLevel)
{
	ItemType = ENRItemType::WeaponPart;
	Count = 1;
	PartData = InPart;
	PartLevel = InLevel;
	ApplyLook();
}

FString ANRItemPickup::Describe() const
{
	if (ItemType == ENRItemType::WeaponPart)
	{
		return FString::Printf(TEXT("PART %s %s"), *GetNameSafe(PartData), *UNRWeaponPartData::GetLevelText(PartLevel).ToString());
	}
	return FString::Printf(TEXT("%d %s"), Count, NRItems::GetDisplayName(ItemType));
}

void ANRItemPickup::ApplyLook()
{
	FLinearColor Color = PotionColor;
	UStaticMesh* Shape = PotionMesh;
	FVector Scale = PotionScale;
	FRotator Rotation = FRotator::ZeroRotator;
	switch (ItemType)
	{
	case ENRItemType::Shield:
		// A cube stood on its corner reads as a diamond, which tells it apart from the potion.
		Color = ShieldColor;
		Shape = ShieldMesh;
		Scale = ShieldScale;
		Rotation = FRotator(45.0f, 0.0f, 45.0f);
		break;
	case ENRItemType::Ammo:
		Color = AmmoColor;
		Shape = AmmoMesh;
		Scale = AmmoScale;
		break;
	case ENRItemType::Money:
		// A flat cylinder on its edge: a coin, face on as it spins.
		Color = MoneyColor;
		Shape = MoneyMesh;
		Scale = MoneyScale;
		Rotation = FRotator(0.0f, 0.0f, 90.0f);
		break;
	case ENRItemType::WeaponPart:
		// A flat chip tilted towards the camera; its colour says the level.
		Color = PartLevel == ENRWeaponPartLevel::III ? PartLevelIIIColor
			: PartLevel == ENRWeaponPartLevel::II ? PartLevelIIColor : PartLevelIColor;
		Shape = PartMesh;
		Scale = PartScale;
		Rotation = FRotator(0.0f, 0.0f, 25.0f);
		break;
	default:
		break;
	}

	Mesh->SetStaticMesh(Shape);
	Mesh->SetRelativeScale3D(Scale);
	Mesh->SetRelativeRotation(Rotation);
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

	bool bTaken = false;
	switch (ItemType)
	{
	case ENRItemType::Ammo:
		// Into the guns' reserves; left lying while every one is full.
		if (UNRWeaponComponent* Weapons = Pawn->FindComponentByClass<UNRWeaponComponent>())
		{
			bTaken = Weapons->AddReserveMagazines(Count * GetDefault<UNRLootSettings>()->AmmoBoxMagazines) > 0;
		}
		break;
	case ENRItemType::Money:
		if (UNRRunSubsystem* Run = GetWorld() ? GetWorld()->GetSubsystem<UNRRunSubsystem>() : nullptr)
		{
			Run->AddMoney(FMath::Max(FMath::RoundToInt32(Count * UNRMetaProgressionSubsystem::GetMultiplier(this, ENRMetaStat::GoldGain)), Count));
			bTaken = true;
		}
		break;
	case ENRItemType::WeaponPart:
		// Into the parts inventory, unfitted: the player places it in a safe area.
		if (UNRWeaponPartComponent* Parts = Pawn->FindComponentByClass<UNRWeaponPartComponent>())
		{
			bTaken = PartData && Parts->AddPart(PartData, PartLevel).IsValid();
		}
		break;
	default:
		if (UNRConsumableComponent* Consumables = Pawn->FindComponentByClass<UNRConsumableComponent>())
		{
			bTaken = Consumables->AddItem(ItemType, Count);
		}
		break;
	}
	if (!bTaken)
	{
		return false;
	}

	bCollected = true;
	UE_LOG(LogNRPickup, Log, TEXT("%s picked up %s"), *GetNameSafe(Pawn), *Describe());
	Destroy();
	return true;
}
