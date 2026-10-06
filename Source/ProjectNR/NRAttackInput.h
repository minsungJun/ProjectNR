// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

/**
 * Whether the attack button is being held right now, for hold-to-keep-attacking.
 *
 * Read from the player's key state every time rather than tracked from press and release
 * events: a release can be swallowed - by the loadout menu, by a weapon switch removing the
 * melee mapping context - and a tracked "held" flag would then stay set and keep attacking.
 * While the mouse cursor is showing a menu owns the mouse, so it never counts as held.
 */
namespace NRAttackInput
{
	inline bool IsAttackHeld(const AActor* Owner)
	{
		const APawn* Pawn = Cast<APawn>(Owner);
		const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
		return PC && !PC->ShouldShowMouseCursor() && PC->IsInputKeyDown(EKeys::LeftMouseButton);
	}
}
