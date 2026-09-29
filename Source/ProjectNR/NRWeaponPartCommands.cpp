// Copyright Epic Games, Inc. All Rights Reserved.

// Console commands for testing weapon parts without the menu, and for driving a smoke test
// where no mouse or keyboard can reach the game (automation, MCP).
//
//   NR.Loadout                              open / close the loadout menu
//   NR.Parts.List                           inventory, loadouts and runtime stats
//   NR.Parts.Equip <inventory#> <weapon> <slot>
//   NR.Parts.Unequip <weapon> <slot>
//   NR.Weapon.Equip <1|2|3>
//   NR.Weapon.Fire                          one shot with the weapon in hand (a light attack for melee)
//   NR.Weapon.Reload
//   NR.Aim <None|Shoulder|Precision>        aim mode without the right mouse button
//
// <weapon> is Rifle, Shotgun or Melee. Slot 0 is the exclusive slot, 1..5 the common ones.

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "NRAimComponent.h"
#include "NRHUD.h"
#include "NRMeleeComponent.h"
#include "NRWeaponComponent.h"
#include "NRWeaponPartComponent.h"
#include "NRWeaponPartData.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRPartCommands, Log, All);

namespace NRPartCommands
{
	APlayerController* GetPlayer(UWorld* World)
	{
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	APawn* GetPawn(UWorld* World)
	{
		const APlayerController* PC = GetPlayer(World);
		return PC ? PC->GetPawn() : nullptr;
	}

	template <typename T>
	T* GetComponent(UWorld* World)
	{
		APawn* Pawn = GetPawn(World);
		T* Component = Pawn ? Pawn->FindComponentByClass<T>() : nullptr;
		if (!Component)
		{
			UE_LOG(LogNRPartCommands, Warning, TEXT("The possessed pawn has no %s"), *T::StaticClass()->GetName());
		}
		return Component;
	}

	bool ParseWeapon(const FString& Text, ENRWeaponId& Out)
	{
		const int64 Value = StaticEnum<ENRWeaponId>()->GetValueByNameString(Text);
		if (Value == INDEX_NONE)
		{
			UE_LOG(LogNRPartCommands, Warning, TEXT("Unknown weapon '%s' (Rifle, Shotgun, Melee)"), *Text);
			return false;
		}
		Out = static_cast<ENRWeaponId>(Value);
		return true;
	}

	void List(const TArray<FString>& Args, UWorld* World)
	{
		const UNRWeaponPartComponent* Parts = GetComponent<UNRWeaponPartComponent>(World);
		if (!Parts)
		{
			return;
		}

		const TArray<FNRWeaponPartInstance>& Inventory = Parts->GetInventory();
		for (int32 Index = 0; Index < Inventory.Num(); ++Index)
		{
			const FNRWeaponPartInstance& Part = Inventory[Index];
			ENRWeaponId Weapon;
			int32 Slot;
			const bool bFitted = Parts->FindEquippedSlot(Part.InstanceId, Weapon, Slot);
			UE_LOG(LogNRPartCommands, Log, TEXT("  #%02d %s %s  %s%s"), Index,
				*GetNameSafe(Part.PartData), *UNRWeaponPartData::GetLevelText(Part.Level).ToString(),
				Part.PartData ? *Part.PartData->DescribeEffect(Part.Level).ToString() : TEXT(""),
				bFitted ? *FString::Printf(TEXT("  [on %s slot %d]"), *UEnum::GetValueAsString(Weapon), Slot) : TEXT(""));
		}

		for (ENRWeaponId Id : { ENRWeaponId::Rifle, ENRWeaponId::Shotgun, ENRWeaponId::Melee })
		{
			const FNRWeaponRuntimeStats Stats = Parts->GetRuntimeStats(Id);
			UE_LOG(LogNRPartCommands, Log, TEXT("  %s: dmg x%.2f, mag %d, speed x%.2f, crit %.0f%% x%.2f"),
				*UEnum::GetValueAsString(Id), Stats.DamageMultiplier, Stats.MaxMagazine, Stats.AttackSpeedMultiplier,
				Stats.CriticalChance * 100.0f, Stats.CriticalMultiplier);
		}
	}

	void Equip(const TArray<FString>& Args, UWorld* World)
	{
		UNRWeaponPartComponent* Parts = GetComponent<UNRWeaponPartComponent>(World);
		ENRWeaponId Weapon;
		if (!Parts || Args.Num() < 3 || !ParseWeapon(Args[1], Weapon))
		{
			UE_LOG(LogNRPartCommands, Warning, TEXT("NR.Parts.Equip <inventory#> <Rifle|Shotgun|Melee> <slot>"));
			return;
		}

		const int32 Index = FCString::Atoi(*Args[0]);
		if (!Parts->GetInventory().IsValidIndex(Index))
		{
			UE_LOG(LogNRPartCommands, Warning, TEXT("No inventory part #%d"), Index);
			return;
		}

		const ENRWeaponPartEquipResult Result = Parts->EquipPart(Parts->GetInventory()[Index].InstanceId, Weapon, FCString::Atoi(*Args[2]));
		UE_LOG(LogNRPartCommands, Log, TEXT("Equip #%d -> %s slot %s: %s"), Index, *Args[1], *Args[2],
			*UNRWeaponPartComponent::DescribeEquipResult(Result).ToString());
	}

	void Unequip(const TArray<FString>& Args, UWorld* World)
	{
		UNRWeaponPartComponent* Parts = GetComponent<UNRWeaponPartComponent>(World);
		ENRWeaponId Weapon;
		if (!Parts || Args.Num() < 2 || !ParseWeapon(Args[0], Weapon))
		{
			UE_LOG(LogNRPartCommands, Warning, TEXT("NR.Parts.Unequip <Rifle|Shotgun|Melee> <slot>"));
			return;
		}
		const bool bDone = Parts->UnequipSlot(Weapon, FCString::Atoi(*Args[1]));
		UE_LOG(LogNRPartCommands, Log, TEXT("Unequip %s slot %s: %s"), *Args[0], *Args[1], bDone ? TEXT("done") : TEXT("nothing to remove"));
	}

	void EquipWeapon(const TArray<FString>& Args, UWorld* World)
	{
		if (UNRWeaponComponent* Weapons = GetComponent<UNRWeaponComponent>(World))
		{
			Weapons->EquipWeapon(Args.Num() > 0 ? FCString::Atoi(*Args[0]) - 1 : 0);
		}
	}

	void Fire(const TArray<FString>& Args, UWorld* World)
	{
		UNRWeaponComponent* Weapons = GetComponent<UNRWeaponComponent>(World);
		if (!Weapons)
		{
			return;
		}
		if (Weapons->IsMeleeEquipped())
		{
			if (UNRMeleeComponent* Melee = GetComponent<UNRMeleeComponent>(World))
			{
				Melee->LightAttack();
			}
			return;
		}
		const int32 Spawned = Weapons->Fire();
		UE_LOG(LogNRPartCommands, Log, TEXT("Fire: %d projectile(s), ammo %d/%d%s"), Spawned,
			Weapons->GetCurrentAmmo(), Weapons->GetMaxAmmo(), Weapons->IsReloading() ? TEXT(" (reloading)") : TEXT(""));
	}

	void Reload(const TArray<FString>& Args, UWorld* World)
	{
		if (UNRWeaponComponent* Weapons = GetComponent<UNRWeaponComponent>(World))
		{
			UE_LOG(LogNRPartCommands, Log, TEXT("Reload: %s"), Weapons->Reload() ? TEXT("started") : TEXT("refused"));
		}
	}

	void Aim(const TArray<FString>& Args, UWorld* World)
	{
		UNRAimComponent* AimComponent = GetComponent<UNRAimComponent>(World);
		if (!AimComponent)
		{
			return;
		}
		const int64 Value = Args.Num() > 0 ? StaticEnum<ENRAimMode>()->GetValueByNameString(Args[0]) : INDEX_NONE;
		if (Value == INDEX_NONE)
		{
			UE_LOG(LogNRPartCommands, Warning, TEXT("NR.Aim <None|Shoulder|Precision>"));
			return;
		}
		AimComponent->SetAimMode(static_cast<ENRAimMode>(Value));
		UE_LOG(LogNRPartCommands, Log, TEXT("Aim: %s"), *UEnum::GetValueAsString(AimComponent->GetAimMode()));
	}

	void ToggleMenu(const TArray<FString>& Args, UWorld* World)
	{
		const APlayerController* PC = GetPlayer(World);
		if (ANRHUD* HUD = PC ? Cast<ANRHUD>(PC->GetHUD()) : nullptr)
		{
			HUD->ToggleLoadoutMenu();
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRLoadoutCommand(TEXT("NR.Loadout"),
	TEXT("Open or close the weapon part loadout menu."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRPartCommands::ToggleMenu));

static FAutoConsoleCommandWithWorldAndArgs GNRPartsListCommand(TEXT("NR.Parts.List"),
	TEXT("Log owned parts, loadouts and runtime stats."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRPartCommands::List));

static FAutoConsoleCommandWithWorldAndArgs GNRPartsEquipCommand(TEXT("NR.Parts.Equip"),
	TEXT("NR.Parts.Equip <inventory#> <Rifle|Shotgun|Melee> <slot>: fit a part (slot 0 exclusive, 1-5 common)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRPartCommands::Equip));

static FAutoConsoleCommandWithWorldAndArgs GNRPartsUnequipCommand(TEXT("NR.Parts.Unequip"),
	TEXT("NR.Parts.Unequip <Rifle|Shotgun|Melee> <slot>: empty a slot."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRPartCommands::Unequip));

static FAutoConsoleCommandWithWorldAndArgs GNRWeaponEquipCommand(TEXT("NR.Weapon.Equip"),
	TEXT("NR.Weapon.Equip <1|2|3>: switch weapon."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRPartCommands::EquipWeapon));

static FAutoConsoleCommandWithWorldAndArgs GNRWeaponFireCommand(TEXT("NR.Weapon.Fire"),
	TEXT("Fire the weapon in hand once (a light attack for melee)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRPartCommands::Fire));

static FAutoConsoleCommandWithWorldAndArgs GNRWeaponReloadCommand(TEXT("NR.Weapon.Reload"),
	TEXT("Reload the weapon in hand."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRPartCommands::Reload));

static FAutoConsoleCommandWithWorldAndArgs GNRAimCommand(TEXT("NR.Aim"),
	TEXT("NR.Aim <None|Shoulder|Precision>: set the aim mode as the right mouse button would."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRPartCommands::Aim));
