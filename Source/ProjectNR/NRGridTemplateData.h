// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NRGridTypes.h"
#include "NRGridTemplateData.generated.h"

class UWorld;

/**
 * A hand-made room level the map generator may place, and what it is allowed to stand for.
 *
 * The level follows the module spec (see ANRMapGenerator): floor top at Z = 0, origin at the
 * floor's centre, Size square, and for every doorway in PortMask a marker NRPort.<dir> on its
 * outer face and a blocker NRBlock.<dir> filling it, in the level's own frame. Doorways the
 * layout does not use stay blocked, so a template with more doorways than a room needs is fine.
 */
UCLASS(BlueprintType)
class PROJECTNR_API UNRRoomTemplateData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Template")
	TSoftObjectPtr<UWorld> Level;

	/** Room types this level can be. A type no template lists is a configuration error, never
	 *  silently filled by some other room. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Template")
	TArray<ENRGridRoomType> RoomTypes;

	/** Doorways the level has, in its own frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Template", meta = (Bitmask, BitmaskEnum = "/Script/ProjectNR.ENRGridDirection"))
	int32 PortMask = 0b1111;

	/** Quarter turns the level may be placed at: bit 0 for 0 degrees, bit 1 for 90, and so on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Template", meta = (Bitmask))
	int32 AllowedRotations = 0b1111;

	/** Relative chance against the other templates that fit the same room. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Template", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Outer size, in cm. Must match the map's room size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room Template", meta = (ClampMin = "100.0"))
	float Size = 2400.0f;
};

/** A straight corridor level: runs along its local X with ends NRPort.A at -Length/2 and NRPort.B at +Length/2. */
UCLASS(BlueprintType)
class PROJECTNR_API UNRCorridorTemplateData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Corridor Template")
	TSoftObjectPtr<UWorld> Level;

	/** End to end, in cm. Sets the gap between rooms. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Corridor Template", meta = (ClampMin = "0.0"))
	float Length = 800.0f;
};
