// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRLootSubsystem.h"
#include "NRMetaProgression.h"

#include "Characters/EnemyCharacter.h"
#include "CollisionQueryParams.h"
#include "Components/HealthComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NRItemPickup.h"
#include "NRLootSettings.h"
#include "NRLootTable.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRLoot, Log, All);

bool UNRLootSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game and PIE worlds only: nothing dies in an editor world.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UNRLootSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	AnyDamagedHandle = UHealthComponent::OnAnyDamaged.AddUObject(this, &UNRLootSubsystem::HandleAnyDamaged);
}

void UNRLootSubsystem::Deinitialize()
{
	UHealthComponent::OnAnyDamaged.Remove(AnyDamagedHandle);
	AnyDamagedHandle.Reset();
	Super::Deinitialize();
}

void UNRLootSubsystem::HandleAnyDamaged(UHealthComponent* Health, float Damage)
{
	if (!Health || Health->GetWorld() != GetWorld())
	{
		return;
	}

	// OnAnyDamaged fires before the dead flag latches: no health left means this was the kill.
	AActor* Owner = Health->GetOwner();
	if (!Owner || !Owner->IsA<AEnemyCharacter>() || Health->GetCurrentHealth() > 0.0f)
	{
		return;
	}

	// Every roll of the enemy's table, spread a little round the body.
	const UNRLootTable* Table = GetDefault<UNRLootSettings>()->FindEnemyLoot(Owner);
	if (!Table)
	{
		UE_LOG(LogNRLoot, Warning, TEXT("%s has no loot table (Project Settings > NR Loot)"), *Owner->GetName());
		return;
	}
	TArray<FNRLootDrop> Drops;
	Table->Roll(Drops, UNRMetaProgressionSubsystem::GetMultiplier(this, ENRMetaStat::ItemDropRate));
	const FVector Where = Owner->GetActorLocation();
	for (const FNRLootDrop& Drop : Drops)
	{
		SpawnDropNear(Drop, Where);
	}
}

ANRItemPickup* UNRLootSubsystem::SpawnDropNear(const FNRLootDrop& Drop, const FVector& Location)
{
	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Distance = FMath::FRandRange(30.0f, 70.0f);
	return SpawnDrop(Drop, Location + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Distance);
}

ANRItemPickup* UNRLootSubsystem::SpawnPickup(ENRItemType ItemType, int32 Count, const FVector& Location)
{
	FNRLootDrop Drop;
	Drop.ItemType = ItemType;
	Drop.Count = Count;
	return SpawnDrop(Drop, Location);
}

ANRItemPickup* UNRLootSubsystem::SpawnDrop(const FNRLootDrop& Drop, const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Onto the floor under the point. Pawns are not world objects, so the body it dropped from
	// is skipped; other pickups are, and their triggers are skipped by hand so drops do not stack.
	FVector SpawnLocation = Location;
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NRLootFloor), false);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	World->LineTraceMultiByObjectType(Hits, Location + FVector(0.0f, 0.0f, 50.0f), Location - FVector(0.0f, 0.0f, 500.0f), Objects, Params);
	for (const FHitResult& Hit : Hits)
	{
		if (!Cast<ANRItemPickup>(Hit.GetActor()))
		{
			SpawnLocation = Hit.ImpactPoint + FVector(0.0f, 0.0f, 40.0f);
			break;
		}
	}

	UClass* PickupClass = GetDefault<UNRLootSettings>()->PickupClass.LoadSynchronous();
	if (!PickupClass)
	{
		PickupClass = ANRItemPickup::StaticClass();
	}

	// Deferred so the item is set before BeginPlay, which may collect it at once.
	ANRItemPickup* Pickup = World->SpawnActorDeferred<ANRItemPickup>(PickupClass,
		FTransform(FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f), SpawnLocation), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pickup)
	{
		return nullptr;
	}
	if (Drop.ItemType == ENRItemType::WeaponPart)
	{
		Pickup->SetPart(Drop.Part, Drop.Level);
	}
	else
	{
		Pickup->SetItem(Drop.ItemType, Drop.Count);
	}
	Pickup->FinishSpawning(FTransform(Pickup->GetActorRotation(), SpawnLocation));

	UE_LOG(LogNRLoot, Log, TEXT("Dropped %s at %s"), *Pickup->Describe(), *SpawnLocation.ToCompactString());
	return Pickup;
}

// NR.Loot.Drop [potion|shield|ammo|gold|part] [count|I|II|III]: drop an item 250 cm in front of the player.
namespace NRLootCommands
{
	void Drop(const TArray<FString>& Args, UWorld* World)
	{
		UNRLootSubsystem* Loot = World ? World->GetSubsystem<UNRLootSubsystem>() : nullptr;
		const APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		if (!Loot || !Pawn)
		{
			UE_LOG(LogNRLoot, Warning, TEXT("NR.Loot.Drop needs a game world with a player pawn"));
			return;
		}

		ENRItemType Type = ENRItemType::Potion;
		if (Args.Num() > 0)
		{
			Type = Args[0].Equals(TEXT("shield"), ESearchCase::IgnoreCase) ? ENRItemType::Shield
				: Args[0].Equals(TEXT("ammo"), ESearchCase::IgnoreCase) ? ENRItemType::Ammo
				: Args[0].Equals(TEXT("gold"), ESearchCase::IgnoreCase) || Args[0].Equals(TEXT("money"), ESearchCase::IgnoreCase) ? ENRItemType::Money
				: ENRItemType::Potion;
		}
		const FVector Forward = Pawn->GetActorForwardVector().GetSafeNormal2D();
		const FVector At = Pawn->GetActorLocation() + Forward * 250.0f;
		if (Args.Num() > 0 && Args[0].Equals(TEXT("part"), ESearchCase::IgnoreCase))
		{
			// A random part; the level from the second argument, else by the default weights.
			FNRLootDrop Drop;
			Drop.ItemType = ENRItemType::WeaponPart;
			Drop.Part = UNRLootTable::PickPart({});
			const FString Level = Args.Num() > 1 ? Args[1] : FString();
			Drop.Level = Level.Equals(TEXT("III"), ESearchCase::IgnoreCase) ? ENRWeaponPartLevel::III
				: Level.Equals(TEXT("II"), ESearchCase::IgnoreCase) ? ENRWeaponPartLevel::II
				: Level.Equals(TEXT("I"), ESearchCase::IgnoreCase) ? ENRWeaponPartLevel::I
				: NRItems::RollPartLevel(FNRLootEntry());
			if (Drop.Part)
			{
				Loot->SpawnDrop(Drop, At);
			}
			return;
		}
		const int32 Count = Args.Num() > 1 ? FMath::Max(FCString::Atoi(*Args[1]), 1) : 1;
		Loot->SpawnPickup(Type, Count, At);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRLootDropCommand(TEXT("NR.Loot.Drop"),
	TEXT("NR.Loot.Drop [potion|shield|ammo|gold|part] [count|I|II|III]: drop an item in front of the player."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRLootCommands::Drop));
