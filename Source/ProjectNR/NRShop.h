// Copyright Epic Games, Inc. All Rights Reserved.

// The shop: a box the player opens with the interact key to spend the run's gold.
//
// What it sells and for how much comes from a UNRShopData asset (class defaults when none is set):
// healing potions and shields (armour plates) at fixed prices, ammo for each gun - dearer for the
// gun the player fires most - and a few weapon parts, rolled once per shop, at higher levels than
// chests drop and at a price picked within a range per level. Wound medicine and grenades are on
// the design list but do not exist in the game yet, so they are not sold.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "NRInteractable.h"
#include "NRItemTypes.h"
#include "NRWeaponPartTypes.h"
#include "NRShop.generated.h"

class UMaterialInterface;
class UNRWeaponComponent;
class UNRWeaponPartData;
class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** One line on the shop's list. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRShopOffer
{
	GENERATED_BODY()

	/** Potion, Shield, Ammo or WeaponPart. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	ENRItemType ItemType = ENRItemType::Potion;

	/** Charges for a potion or shield, magazines for ammo, 1 for a part. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	float Amount = 1.0f;

	/** Ammo: which gun it is for. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	ENRWeaponId WeaponId = ENRWeaponId::Rifle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UNRWeaponPartData> Part;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	ENRWeaponPartLevel PartLevel = ENRWeaponPartLevel::I;

	/** Gold. Ammo prices are worked out again whenever the list is read. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	int32 Price = 0;

	/** Parts are one of a kind: gone once bought. The rest never run out. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	bool bSoldOut = false;
};

/** Why a purchase did or did not go through. */
UENUM(BlueprintType)
enum class ENRShopResult : uint8
{
	Bought,
	NotEnoughGold,
	SoldOut,
	/** The player cannot carry it, e.g. that gun's reserve is full. Nothing is charged. */
	CannotCarry,
	Invalid
};

/** A price range in gold. */
USTRUCT(BlueprintType)
struct PROJECTNR_API FNRShopPriceRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop", meta = (ClampMin = "0"))
	int32 Min = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop", meta = (ClampMin = "0"))
	int32 Max = 0;
};

/** What a shop sells and its prices. Edit a copy, or leave a shop's ShopData empty for these defaults. */
UCLASS(BlueprintType)
class PROJECTNR_API UNRShopData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UNRShopData();

	/** Healing potion (회복약): fixed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumables", meta = (ClampMin = "0"))
	int32 PotionPrice = 15;

	/** Shield / armour plate (방탄판): fixed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumables", meta = (ClampMin = "0"))
	int32 ShieldPrice = 20;

	/** Magazines' worth of ammo one purchase gives. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0.1"))
	float AmmoMagazinesPerBuy = 2.0f;

	/** Ammo price when the guns are used equally (or not at all). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 AmmoBasePrice = 10;

	/** Most an ammo price rises, as a share of the base: reached when one gun does all the firing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AmmoUsageMarkup = 0.3f;

	/** Parts on sale in one shop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts", meta = (ClampMin = "0", ClampMax = "8"))
	int32 PartOffers = 3;

	/** Parts that may come up. Empty: every weapon part asset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts")
	TArray<TObjectPtr<UNRWeaponPartData>> PartPool;

	/** Level I, II, III weights for a part on sale (chests drop 70 / 25 / 5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts", meta = (ClampMin = "0.0"))
	float PartLevelIWeight = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts", meta = (ClampMin = "0.0"))
	float PartLevelIIWeight = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts", meta = (ClampMin = "0.0"))
	float PartLevelIIIWeight = 15.0f;

	/** Price range of a part, per level (I, II, III). The price is picked once, when the shop stocks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts", EditFixedSize)
	TArray<FNRShopPriceRange> PartPrice;
};

/** The pricing rules on their own, so they can be tested without a world (ProjectNR.Shop.Pricing). */
namespace NRShop
{
	/**
	 * Ammo price for a gun from how much it has been fired. Fired holds every gun's magazines fired;
	 * Index is the one priced. The price rises only for a gun used more than an even share: one gun
	 * of two doing all the firing pays Base * (1 + Markup), an even split pays Base.
	 */
	PROJECTNR_API int32 AmmoPrice(int32 Base, float Markup, const TArray<float>& Fired, int32 Index);

	/** A part's level by the three weights; level I when they are all 0. */
	PROJECTNR_API ENRWeaponPartLevel RollPartLevel(const UNRShopData& Data, FRandomStream& Stream);

	/** A price within the range for Level (Min when the range is empty or backwards). */
	PROJECTNR_API int32 RollPartPrice(const UNRShopData& Data, ENRWeaponPartLevel Level, FRandomStream& Stream);

	/** "HEALING POTION x1", "RIFLE AMMO x2", "Damage II": the line's name for the list and logs. */
	PROJECTNR_API FText DescribeOffer(const FNRShopOffer& Offer);
}

/**
 * The shop, as a box for now: a tinted cube with a sign. The interact key opens the shop screen
 * (ANRHUD); buying takes gold from the run (UNRRunSubsystem) and puts the item straight on the
 * player. The parts on sale are rolled the first time the shop is opened (or on BeginPlay when
 * bStockOnBeginPlay), so every shop has its own.
 */
UCLASS()
class PROJECTNR_API ANRShop : public AActor, public INRInteractable
{
	GENERATED_BODY()

public:
	ANRShop();

	// INRInteractable
	virtual bool CanInteract(const APawn* User) const override { return true; }
	virtual void Interact(APawn* User) override;
	virtual FText GetInteractPrompt() const override { return Prompt; }
	virtual FVector GetInteractLocation() const override;

	/** The list as the player sees it now: ammo priced for the player's current use of each gun. */
	TArray<FNRShopOffer> GetOffers(const APawn* Buyer) const;

	/** Buys line Index of GetOffers for Buyer. Charges only when the item was taken. */
	UFUNCTION(BlueprintCallable, Category = "Shop")
	ENRShopResult Buy(APawn* Buyer, int32 Index);

	/** Rolls the parts on sale again (console and tests). */
	void Restock();

	const UNRShopData& GetData() const;

	static FText DescribeResult(ENRShopResult Result);

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	void StockIfNeeded();

	/** Prices and what is sold. Empty: UNRShopData's class defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UNRShopData> ShopData;

	/** Roll the parts when play begins rather than when first opened. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	bool bStockOnBeginPlay = false;

	/** Fixed seed for the parts on sale; 0 picks one at random. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	int32 Seed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	FText Prompt;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UTextRenderComponent> Sign;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Look")
	FLinearColor BodyColor = FLinearColor(0.95f, 0.6f, 0.1f);

	UPROPERTY()
	TObjectPtr<UMaterialInterface> TintMaterial;

	/** The parts on sale. The other lines (potion, shield, each gun's ammo) are made by GetOffers. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Shop")
	TArray<FNRShopOffer> PartStock;

	bool bStocked = false;
};
