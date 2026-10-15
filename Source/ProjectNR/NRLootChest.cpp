// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRLootChest.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NRLootSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRChest, Log, All);

namespace NRChestShape
{
	// Outer size in cm: depth along X (front is +X), width along Y, body height. The engine
	// cube is 100 cm and centred, which the scales and offsets below are worked from.
	constexpr float Depth = 60.0f;
	constexpr float Width = 90.0f;
	constexpr float BodyHeight = 50.0f;
	constexpr float LidHeight = 12.0f;
}

ANRLootChest::ANRLootChest()
{
	PrimaryActorTick.bCanEverTick = true;
	// Only the lid animation needs it, and that starts when the chest is opened.
	PrimaryActorTick.bStartWithTickEnabled = false;

	using namespace NRChestShape;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	TintMaterial = MaterialFinder.Object;

	// The actor's origin sits on the floor, so the body is lifted by half its height. It is the
	// only solid part: it blocks the player and is what the interaction scan finds.
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetStaticMesh(CubeFinder.Object);
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, BodyHeight * 0.5f));
	Body->SetRelativeScale3D(FVector(Depth, Width, BodyHeight) / 100.0f);
	Body->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	LidHinge = CreateDefaultSubobject<USceneComponent>(TEXT("LidHinge"));
	LidHinge->SetupAttachment(Root);
	LidHinge->SetRelativeLocation(FVector(-Depth * 0.5f, 0.0f, BodyHeight));

	Lid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lid"));
	Lid->SetupAttachment(LidHinge);
	Lid->SetStaticMesh(CubeFinder.Object);
	Lid->SetRelativeLocation(FVector(Depth * 0.5f, 0.0f, LidHeight * 0.5f));
	Lid->SetRelativeScale3D(FVector(Depth + 2.0f, Width + 2.0f, LidHeight) / 100.0f);
	Lid->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Hangs off the lid's front edge, so it swings up with it.
	Latch = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Latch"));
	Latch->SetupAttachment(LidHinge);
	Latch->SetStaticMesh(CubeFinder.Object);
	Latch->SetRelativeLocation(FVector(Depth + 2.0f, 0.0f, 0.0f));
	Latch->SetRelativeScale3D(FVector(4.0f, 14.0f, 18.0f) / 100.0f);
	Latch->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Root);
	Glow->SetRelativeLocation(FVector(Depth + 30.0f, 0.0f, BodyHeight + 30.0f));
	Glow->SetIntensity(700.0f);
	Glow->SetAttenuationRadius(250.0f);
	Glow->SetCastShadows(false);

	Prompt = NSLOCTEXT("NRChest", "Open", "OPEN");

	FNRLootEntry Potion;
	Potion.ItemType = ENRItemType::Potion;
	LootTable.Add(Potion);
	FNRLootEntry Shield;
	Shield.ItemType = ENRItemType::Shield;
	LootTable.Add(Shield);
	FNRLootEntry AmmoBox;
	AmmoBox.ItemType = ENRItemType::Ammo;
	LootTable.Add(AmmoBox);
	FNRLootEntry Gold;
	Gold.ItemType = ENRItemType::Money;
	Gold.Count = 15;
	LootTable.Add(Gold);
}

void ANRLootChest::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLook();
}

void ANRLootChest::ApplyLook()
{
	auto Tint = [this](UStaticMeshComponent* Mesh, const FLinearColor& Color)
	{
		if (Mesh && TintMaterial)
		{
			if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(0, TintMaterial))
			{
				Material->SetVectorParameterValue(TEXT("Color"), Color);
			}
		}
	};
	Tint(Body, BodyColor);
	Tint(Lid, LidColor);
	Tint(Latch, TrimColor);
	Glow->SetLightColor(TrimColor);
	Glow->SetVisibility(!bOpened);
}

FVector ANRLootChest::GetInteractLocation() const
{
	// Above the lid, where the prompt reads clearly.
	return GetActorLocation() + FVector(0.0f, 0.0f, NRChestShape::BodyHeight + 45.0f);
}

void ANRLootChest::Interact(APawn* User)
{
	if (bOpened)
	{
		return;
	}

	bOpened = true;
	OpenStartTime = GetWorld()->GetTimeSeconds();
	Glow->SetVisibility(false);
	SetActorTickEnabled(true);
	UE_LOG(LogNRChest, Log, TEXT("%s opened by %s"), *GetName(), *GetNameSafe(User));

	if (ItemDelay > 0.0f)
	{
		GetWorldTimerManager().SetTimer(ItemTimer, this, &ANRLootChest::SpawnItems, ItemDelay, false);
	}
	else
	{
		SpawnItems();
	}
}

void ANRLootChest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Eased so the lid flicks up and settles.
	const float Alpha = FMath::Clamp((GetWorld()->GetTimeSeconds() - OpenStartTime) / OpenTime, 0.0f, 1.0f);
	const float Eased = 1.0f - FMath::Square(1.0f - Alpha);
	LidHinge->SetRelativeRotation(FRotator(OpenAngle * Eased, 0.0f, 0.0f));

	if (Alpha >= 1.0f)
	{
		SetActorTickEnabled(false);
	}
}

void ANRLootChest::SpawnItems()
{
	UNRLootSubsystem* Loot = GetWorld() ? GetWorld()->GetSubsystem<UNRLootSubsystem>() : nullptr;
	if (!Loot)
	{
		return;
	}

	// In a row across the front, centred on the chest.
	const FVector Front = GetActorLocation() + GetActorForwardVector().GetSafeNormal2D() * ItemDistance;
	const FVector Side = GetActorRightVector().GetSafeNormal2D();
	for (int32 Index = 0; Index < ItemCount; ++Index)
	{
		const FNRLootEntry* Entry = NRItems::PickEntry(LootTable);
		if (!Entry)
		{
			break;
		}

		const float Offset = (Index - (ItemCount - 1) * 0.5f) * ItemSpacing;
		Loot->SpawnPickup(Entry->ItemType, Entry->Count, Front + Side * Offset + FVector(0.0f, 0.0f, 60.0f));
	}
}

// NR.Chest.Spawn: a chest 150 cm in front of the player, facing them.
namespace NRChestCommands
{
	void Spawn(const TArray<FString>& Args, UWorld* World)
	{
		const APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		if (!Pawn)
		{
			return;
		}

		const FVector Forward = Pawn->GetActorForwardVector().GetSafeNormal2D();
		FVector Location = Pawn->GetActorLocation() + Forward * 150.0f;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Location, Location - FVector(0.0f, 0.0f, 500.0f), ECC_WorldStatic))
		{
			Location = Hit.ImpactPoint;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const ANRLootChest* Chest = World->SpawnActor<ANRLootChest>(Location, (-Forward).Rotation(), Params);
		UE_LOG(LogNRChest, Log, TEXT("Spawned %s at %s"), *GetNameSafe(Chest), *Location.ToCompactString());
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRChestSpawnCommand(TEXT("NR.Chest.Spawn"),
	TEXT("Spawn a loot chest in front of the player, facing them."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRChestCommands::Spawn));
