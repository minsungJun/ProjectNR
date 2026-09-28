// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRHUD.h"

#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"
#include "NRWeaponComponent.h"

UNRWeaponComponent* ANRHUD::FindWeapon() const
{
	const APawn* Pawn = GetOwningPawn();
	return Pawn ? Pawn->FindComponentByClass<UNRWeaponComponent>() : nullptr;
}

void ANRHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const UNRWeaponComponent* Weapon = FindWeapon();
	if (!Weapon)
	{
		return;
	}

	// Equipped weapon, bottom centre. Drawn even when the crosshair is hidden so a switch
	// made mid-vault is still confirmed on screen.
	const FString WeaponLabel = FString::Printf(TEXT("[%d] %s"),
		Weapon->GetCurrentWeaponIndex() + 1, *Weapon->GetCurrentWeaponName().ToString());
	float LabelWidth = 0.0f;
	float LabelHeight = 0.0f;
	GetTextSize(WeaponLabel, LabelWidth, LabelHeight, nullptr, WeaponLabelScale);
	DrawText(WeaponLabel, NeutralColor,
		(Canvas->SizeX - LabelWidth) * 0.5f, Canvas->SizeY - LabelHeight - WeaponLabelBottomMargin,
		nullptr, WeaponLabelScale);

	if (!Weapon->ShouldShowCrosshair())
	{
		return;
	}

	// Canvas centre is the same screen point the weapon traces through, so no offset or
	// calibration is needed to make the two agree.
	const float CenterX = Canvas->SizeX * 0.5f;
	const float CenterY = Canvas->SizeY * 0.5f;

	const float Gap = BaseGap + Weapon->GetCrosshairSpread() * MaxSpreadPixels;
	const FLinearColor Color = Weapon->IsAimingAtHostile() ? HostileColor : NeutralColor;

	const float Inner = Gap;
	const float Outer = Gap + LineLength;

	DrawLine(CenterX - Outer, CenterY, CenterX - Inner, CenterY, Color, LineThickness);
	DrawLine(CenterX + Inner, CenterY, CenterX + Outer, CenterY, Color, LineThickness);
	DrawLine(CenterX, CenterY - Outer, CenterX, CenterY - Inner, Color, LineThickness);
	DrawLine(CenterX, CenterY + Inner, CenterX, CenterY + Outer, Color, LineThickness);

	if (bDrawCenterDot)
	{
		DrawRect(Color, CenterX - 1.0f, CenterY - 1.0f, 2.0f, 2.0f);
	}
}
