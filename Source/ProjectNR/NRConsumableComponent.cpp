// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRConsumableComponent.h"

#include "Components/HealthComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRConsumable, Log, All);

UNRConsumableComponent::UNRConsumableComponent()
{
	// Everything happens on a key press.
	PrimaryComponentTick.bCanEverTick = false;
}

void UNRConsumableComponent::BeginPlay()
{
	Super::BeginPlay();

	PotionCount = StartingPotions;
	ShieldCount = StartingShields;

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		BindInput();
		Pawn->ReceiveRestartedDelegate.AddDynamic(this, &UNRConsumableComponent::OnPawnRestarted);
	}
}

void UNRConsumableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &UNRConsumableComponent::OnPawnRestarted);
	}

	Super::EndPlay(EndPlayReason);
}

void UNRConsumableComponent::OnPawnRestarted(APawn* Pawn)
{
	BindInput();
}

void UNRConsumableComponent::BindInput()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (bInputBound || !Pawn || !Pawn->InputComponent)
	{
		return;
	}

	// Bound directly, like the weapon and roll keys, so adding the component is the whole setup.
	if (PotionKey.IsValid())
	{
		Pawn->InputComponent->BindKey(PotionKey, IE_Pressed, this, &UNRConsumableComponent::OnPotionInput);
	}
	if (ShieldKey.IsValid())
	{
		Pawn->InputComponent->BindKey(ShieldKey, IE_Pressed, this, &UNRConsumableComponent::OnShieldInput);
	}
	bInputBound = true;
}

UHealthComponent* UNRConsumableComponent::FindHealth() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UHealthComponent>() : nullptr;
}

bool UNRConsumableComponent::UsePotion()
{
	UHealthComponent* Health = FindHealth();
	if (!Health || Health->IsDead() || PotionCount <= 0)
	{
		return false;
	}

	const float Restored = Health->Heal(Health->GetMaxHealth() * PotionHealFraction);
	if (Restored <= 0.0f)
	{
		UE_LOG(LogNRConsumable, Log, TEXT("Potion refused: health is full"));
		return false;
	}

	--PotionCount;
	UE_LOG(LogNRConsumable, Log, TEXT("Potion healed %.0f (%.0f/%.0f), %d left"),
		Restored, Health->GetCurrentHealth(), Health->GetMaxHealth(), PotionCount);
	return true;
}

bool UNRConsumableComponent::UseShield()
{
	UHealthComponent* Health = FindHealth();
	if (!Health || Health->IsDead() || ShieldCount <= 0)
	{
		return false;
	}

	const float Added = Health->AddShield(ShieldAmount);
	if (Added <= 0.0f)
	{
		UE_LOG(LogNRConsumable, Log, TEXT("Shield refused: shield is full"));
		return false;
	}

	--ShieldCount;
	UE_LOG(LogNRConsumable, Log, TEXT("Shield added %.0f (%.0f/%.0f), %d left"),
		Added, Health->GetCurrentShield(), Health->GetMaxShield(), ShieldCount);
	return true;
}

bool UNRConsumableComponent::AddItem(ENRItemType ItemType, int32 Count)
{
	if (Count <= 0)
	{
		return false;
	}

	switch (ItemType)
	{
	case ENRItemType::Shield:
		AddCharges(0, Count);
		break;
	case ENRItemType::Potion:
		AddCharges(Count, 0);
		break;
	default:
		// Ammo and money are not consumables: the pickup hands them elsewhere.
		return false;
	}
	UE_LOG(LogNRConsumable, Log, TEXT("Got %d %s: %d potions, %d shields"),
		Count, NRItems::GetDisplayName(ItemType), PotionCount, ShieldCount);
	return true;
}

void UNRConsumableComponent::AddCharges(int32 Potions, int32 Shields)
{
	PotionCount += FMath::Max(Potions, 0);
	ShieldCount += FMath::Max(Shields, 0);
}

// NR.Item.Potion / NR.Item.Shield / NR.Item.Refill, and NR.Player.Damage [amount] to have
// something to heal or soak.
namespace NRConsumableCommands
{
	APawn* GetPlayerPawn(UWorld* World)
	{
		return World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	}

	UNRConsumableComponent* GetConsumables(UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		UNRConsumableComponent* Consumables = Pawn ? Pawn->FindComponentByClass<UNRConsumableComponent>() : nullptr;
		if (!Consumables)
		{
			UE_LOG(LogNRConsumable, Warning, TEXT("The player pawn has no UNRConsumableComponent"));
		}
		return Consumables;
	}

	void Potion(const TArray<FString>& Args, UWorld* World)
	{
		if (UNRConsumableComponent* Consumables = GetConsumables(World))
		{
			Consumables->UsePotion();
		}
	}

	void Shield(const TArray<FString>& Args, UWorld* World)
	{
		if (UNRConsumableComponent* Consumables = GetConsumables(World))
		{
			Consumables->UseShield();
		}
	}

	void Refill(const TArray<FString>& Args, UWorld* World)
	{
		if (UNRConsumableComponent* Consumables = GetConsumables(World))
		{
			const int32 Count = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 3;
			Consumables->AddCharges(Count, Count);
			UE_LOG(LogNRConsumable, Log, TEXT("Charges: %d potions, %d shields"),
				Consumables->GetPotionCount(), Consumables->GetShieldCount());
		}
	}

	void Damage(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		UHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
		if (!Health)
		{
			return;
		}

		const float Amount = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 30.0f;
		Health->ApplyDamage(Amount, nullptr, -Pawn->GetActorForwardVector());
		UE_LOG(LogNRConsumable, Log, TEXT("Player took %.0f: health %.0f/%.0f, shield %.0f"),
			Amount, Health->GetCurrentHealth(), Health->GetMaxHealth(), Health->GetCurrentShield());
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRItemPotionCommand(TEXT("NR.Item.Potion"),
	TEXT("Use a healing potion, as the potion key does."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRConsumableCommands::Potion));

static FAutoConsoleCommandWithWorldAndArgs GNRItemShieldCommand(TEXT("NR.Item.Shield"),
	TEXT("Use a shield, as the shield key does."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRConsumableCommands::Shield));

static FAutoConsoleCommandWithWorldAndArgs GNRItemRefillCommand(TEXT("NR.Item.Refill"),
	TEXT("NR.Item.Refill [count]: add count (default 3) potions and shields."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRConsumableCommands::Refill));

static FAutoConsoleCommandWithWorldAndArgs GNRPlayerDamageCommand(TEXT("NR.Player.Damage"),
	TEXT("NR.Player.Damage [amount]: damage the player (default 30), shield first."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRConsumableCommands::Damage));
