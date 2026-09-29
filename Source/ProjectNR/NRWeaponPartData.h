// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NRWeaponPartTypes.h"
#include "NRWeaponPartData.generated.h"

class UTexture2D;

/**
 * One kind of part: what it changes, how much at each level, and where it fits.
 *
 * Every balance number lives here, not in code, so tuning is an asset edit.
 */
UCLASS(BlueprintType)
class PROJECTNR_API UNRWeaponPartData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Shown in the tooltip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Part")
	FText DisplayName;

	/** Two to four letters drawn on the icon while there is no icon texture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Part")
	FText ShortLabel;

	/** Optional. The icon falls back to ShortLabel when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Part")
	TObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Part")
	ENRWeaponPartStat Stat = ENRWeaponPartStat::Damage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Part")
	ENRWeaponPartSlotType SlotType = ENRWeaponPartSlotType::Common;

	/** Weapons this part may be fitted to. Magazine parts leave Melee out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Part", meta = (Bitmask, BitmaskEnum = "/Script/ProjectNR.ENRWeaponId"))
	int32 CompatibleWeapons = 0b111;

	/** Effect at level I, II and III, in the stat's own unit (see ENRWeaponPartStat). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Part|Levels")
	float ValueLevelI = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Part|Levels")
	float ValueLevelII = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Part|Levels")
	float ValueLevelIII = 0.3f;

	UFUNCTION(BlueprintPure, Category = "Weapon Part")
	float GetValue(ENRWeaponPartLevel Level) const;

	UFUNCTION(BlueprintPure, Category = "Weapon Part")
	bool IsCompatibleWith(ENRWeaponId Weapon) const;

	/** "+20% damage" style text for tooltips. */
	FText DescribeEffect(ENRWeaponPartLevel Level) const;

	static FText GetLevelText(ENRWeaponPartLevel Level);

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
