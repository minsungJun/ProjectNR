// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "NRInteractable.generated.h"

class APawn;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UNRInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Something the player can use with the interact key: a chest now, later shops, levers, NPCs.
 *
 * UNRInteractionComponent on the player finds the nearest one in range whose CanInteract is
 * true, the HUD shows its prompt above GetInteractLocation, and the key calls Interact. The
 * actor needs query collision against WorldStatic or WorldDynamic to be found.
 */
class PROJECTNR_API INRInteractable
{
	GENERATED_BODY()

public:
	/** False hides the prompt and ignores the key, e.g. a chest that is already open. */
	virtual bool CanInteract(const APawn* User) const = 0;

	virtual void Interact(APawn* User) = 0;

	/** Short verb shown after the key, e.g. "OPEN". */
	virtual FText GetInteractPrompt() const = 0;

	/** World point the prompt is drawn at and range is measured to. */
	virtual FVector GetInteractLocation() const = 0;
};
