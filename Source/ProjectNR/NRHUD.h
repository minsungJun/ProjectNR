// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "NRHUD.generated.h"

class UNRWeaponComponent;

/**
 * Canvas crosshair.
 *
 * Deliberately a thin renderer: every value it draws with comes from
 * UNRWeaponComponent's getters, and nothing is decided here. Replacing this with a UMG
 * widget means writing the widget and clearing HUDClass - the spread, the hostile test
 * and the visibility rule all stay where they are and the widget binds to the same
 * getters.
 *
 * Draws at the canvas centre, which is the same screen point UNRWeaponComponent traces
 * through, so the crosshair and the shots agree by construction rather than by tuning.
 */
UCLASS()
class PROJECTNR_API ANRHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

protected:
	/** Gap between the centre and the inner end of each line, before spread. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0.0"))
	float BaseGap = 8.0f;

	/** Pixels the gap grows by at full spread. @see UNRWeaponComponent::GetCrosshairSpread */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0.0"))
	float MaxSpreadPixels = 26.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "1.0"))
	float LineLength = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "1.0"))
	float LineThickness = 2.0f;

	/** Drawn when the aim point is on nothing in particular. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor NeutralColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.75f);

	/** Drawn when the aim point is on something that can take damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor HostileColor = FLinearColor(1.0f, 0.25f, 0.2f, 0.9f);

	/** A single pixel at the exact centre. Useful while checking that shots converge. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Crosshair")
	bool bDrawCenterDot = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Label", meta = (ClampMin = "0.1"))
	float WeaponLabelScale = 1.5f;

	/** Pixels between the equipped-weapon label and the bottom of the screen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Label", meta = (ClampMin = "0.0"))
	float WeaponLabelBottomMargin = 40.0f;

private:
	/** The weapon on the currently possessed pawn, or null. */
	UNRWeaponComponent* FindWeapon() const;
};
