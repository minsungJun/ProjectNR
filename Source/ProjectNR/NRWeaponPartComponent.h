// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NRWeaponPartTypes.h"
#include "NRWeaponPartComponent.generated.h"

struct FNRWeaponDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNROnWeaponPartInventoryChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNROnWeaponPartLoadoutChanged, ENRWeaponId, WeaponId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNROnWeaponRuntimeStatsChanged, ENRWeaponId, WeaponId, const FNRWeaponRuntimeStats&, Stats);

/**
 * The parts the player owns and where each one is fitted.
 *
 * Owns the rules, not the UI: every check (slot type, weapon compatibility, one slot per
 * part, whether editing is allowed right now) is made here, so a menu, a debug command or a
 * future reward screen all go through the same door.
 *
 * Runtime stats are recomputed only when a loadout or a fitted part's level changes, never
 * per frame. UNRWeaponComponent and UNRMeleeComponent read them through
 * UNRWeaponComponent::GetRuntimeStats.
 *
 * Lives on the pawn for now, so a respawned pawn starts over. Moving it to the player state
 * is the step that makes parts survive a death.
 */
UCLASS(ClassGroup = (NR), meta = (BlueprintSpawnableComponent))
class PROJECTNR_API UNRWeaponPartComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNRWeaponPartComponent();

	virtual void InitializeComponent() override;

	/** Adds a new instance to the inventory, unequipped. Returns its id. */
	UFUNCTION(BlueprintCallable, Category = "Weapon Parts")
	FGuid AddPart(UNRWeaponPartData* PartData, ENRWeaponPartLevel Level);

	/**
	 * Fits a part into a slot. A part already fitted elsewhere moves - to another slot or
	 * another weapon - and whatever held the target slot goes back to the inventory.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon Parts")
	ENRWeaponPartEquipResult EquipPart(FGuid InstanceId, ENRWeaponId WeaponId, int32 SlotIndex);

	/** The same checks as EquipPart, without changing anything. */
	UFUNCTION(BlueprintPure, Category = "Weapon Parts")
	ENRWeaponPartEquipResult CanEquipPart(FGuid InstanceId, ENRWeaponId WeaponId, int32 SlotIndex) const;

	/** Empties one slot. False if it was already empty or editing is locked. */
	UFUNCTION(BlueprintCallable, Category = "Weapon Parts")
	bool UnequipSlot(ENRWeaponId WeaponId, int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "Weapon Parts")
	bool UnequipPart(FGuid InstanceId);

	UFUNCTION(BlueprintCallable, Category = "Weapon Parts")
	bool SetPartLevel(FGuid InstanceId, ENRWeaponPartLevel NewLevel);

	/**
	 * Whether loadouts may change right now. Parts are rearranged only in a Safe Phase; until
	 * the run structure exists, bPrototypeAllowLoadoutEditing stands in for it.
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon Parts")
	bool CanEditLoadout() const { return bPrototypeAllowLoadoutEditing || bInSafePhase; }

	/** Hook for the future Safe Phase to open and close editing. */
	UFUNCTION(BlueprintCallable, Category = "Weapon Parts")
	void SetInSafePhase(bool bSafe) { bInSafePhase = bSafe; }

	bool IsInSafePhase() const { return bInSafePhase; }

	/** Turns the prototype "edit anywhere" bypass on or off. A run that decides safe areas
	 *  itself turns it off for its own duration. */
	void SetPrototypeAllowLoadoutEditing(bool bAllow) { bPrototypeAllowLoadoutEditing = bAllow; }

	const TArray<FNRWeaponPartInstance>& GetInventory() const { return Inventory; }

	const FNRWeaponPartInstance* FindPart(const FGuid& InstanceId) const;

	/** Where the part is fitted. False if it sits in the inventory. */
	UFUNCTION(BlueprintPure, Category = "Weapon Parts")
	bool FindEquippedSlot(FGuid InstanceId, ENRWeaponId& OutWeaponId, int32& OutSlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "Weapon Parts")
	bool IsEquipped(FGuid InstanceId) const;

	/** Parts held but not fitted to a weapon. */
	UFUNCTION(BlueprintPure, Category = "Weapon Part")
	int32 GetStoredCount() const;

	/** Most parts that can be held unfitted (permanent growth raises it). AddPart refuses past it. */
	UFUNCTION(BlueprintPure, Category = "Weapon Part")
	int32 GetStorageCapacity() const;

	/** The part in a slot, or an invalid guid for an empty one. */
	UFUNCTION(BlueprintPure, Category = "Weapon Parts")
	FGuid GetEquippedPart(ENRWeaponId WeaponId, int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "Weapon Parts")
	FNRWeaponRuntimeStats GetRuntimeStats(ENRWeaponId WeaponId) const;

	/** The formulas in FNRWeaponRuntimeStats, applied to one weapon and the parts fitted to it. */
	static FNRWeaponRuntimeStats ComputeRuntimeStats(const FNRWeaponDefinition& Weapon, const TArray<const FNRWeaponPartInstance*>& Parts);

	static FText DescribeEquipResult(ENRWeaponPartEquipResult Result);

	UPROPERTY(BlueprintAssignable, Category = "Weapon Parts")
	FNROnWeaponPartInventoryChanged OnInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "Weapon Parts")
	FNROnWeaponPartLoadoutChanged OnLoadoutChanged;

	UPROPERTY(BlueprintAssignable, Category = "Weapon Parts")
	FNROnWeaponRuntimeStatsChanged OnRuntimeStatsChanged;

protected:
	/** Handed out when play starts. Defaults to every test part at levels I, II and III. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Parts")
	TArray<FNRStartingWeaponPart> StartingParts;

	/** Allows editing outside a Safe Phase. Prototype only: turn off once Safe Phases exist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Parts")
	bool bPrototypeAllowLoadoutEditing = true;

private:
	FNRWeaponPartInstance* FindPartMutable(const FGuid& InstanceId);
	FNRWeaponPartLoadout& GetLoadoutMutable(ENRWeaponId WeaponId);

	void RecalculateStats(ENRWeaponId WeaponId);
	void RecalculateAllStats();

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Weapon Parts")
	TArray<FNRWeaponPartInstance> Inventory;

	/** One per ENRWeaponId, indexed by it. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Weapon Parts")
	TArray<FNRWeaponPartLoadout> Loadouts;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Weapon Parts")
	TArray<FNRWeaponRuntimeStats> RuntimeStats;

	bool bInSafePhase = false;
};
