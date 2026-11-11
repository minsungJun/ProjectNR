// A top-down mask of the grid run's outdoor walls, for the landscape material. The landscape's
// own height and collision cannot change at run time, so the generated layout is drawn into a
// texture instead: R raises the ground (World Position Offset, visual only) on the outside of
// the outdoor walls, where the player never walks, and G blends a dirt layer along both sides
// of those walls. Indoor modules hold the mask at zero so nothing climbs into a building.

#pragma once

#include "CoreMinimal.h"
#include "NRWallMask.generated.h"

class ANRMapGenerator;
class UTextureRenderTarget2D;
class UWorld;

/** How the outdoor wall mask is shaped. Distances in cm. */
USTRUCT(BlueprintType)
struct FNRWallMaskSettings
{
	GENERATED_BODY()

	/** Off: the landscape stays flat and unblended. */
	UPROPERTY(EditAnywhere, Category = "Wall Mask")
	bool bEnabled = true;

	/** Pixels per side of the mask. */
	UPROPERTY(EditAnywhere, Category = "Wall Mask", meta = (ClampMin = "64", ClampMax = "4096"))
	int32 Resolution = 1024;

	/** How far past a room's outline its outer wall face is. */
	UPROPERTY(EditAnywhere, Category = "Wall Mask", meta = (ClampMin = "0.0"))
	float RoomWallPad = 30.0f;

	/** Half width out to the outer wall faces of an outdoor corridor or hall arm (walls at +-325, 50 thick). */
	UPROPERTY(EditAnywhere, Category = "Wall Mask", meta = (ClampMin = "0.0"))
	float PathHalfWidth = 350.0f;

	/** Half width of an indoor corridor (kept flat). */
	UPROPERTY(EditAnywhere, Category = "Wall Mask", meta = (ClampMin = "0.0"))
	float IndoorCorridorHalfWidth = 400.0f;

	/** Outside the walls the ground rises over this distance... */
	UPROPERTY(EditAnywhere, Category = "Wall Mask", meta = (ClampMin = "1.0"))
	float RiseWidth = 150.0f;

	/** ...stays up to here... */
	UPROPERTY(EditAnywhere, Category = "Wall Mask", meta = (ClampMin = "0.0"))
	float HoldEnd = 500.0f;

	/** ...and is back down at this distance from the wall. */
	UPROPERTY(EditAnywhere, Category = "Wall Mask", meta = (ClampMin = "1.0"))
	float FadeEnd = 1400.0f;

	/** Most the ground rises, in cm. */
	UPROPERTY(EditAnywhere, Category = "Wall Mask", meta = (ClampMin = "0.0"))
	float RaiseHeight = 120.0f;

	/** Dirt reaches this far inside the walls. */
	UPROPERTY(EditAnywhere, Category = "Wall Mask", meta = (ClampMin = "1.0"))
	float DirtInside = 250.0f;

	/** Near an indoor module the raise and dirt fade out over this distance. */
	UPROPERTY(EditAnywhere, Category = "Wall Mask", meta = (ClampMin = "1.0"))
	float IndoorClear = 300.0f;
};

/** The mask of one layout, kept on the CPU so ground cover can be lifted by the same amount. */
class PROJECTNR_API FNRWallMask
{
public:
	/** Draws the mask of the generator's current layout; empty when there are no outdoor modules. */
	void Build(const ANRMapGenerator& Generator, const FNRWallMaskSettings& Settings);

	void Reset();

	bool IsEmpty() const { return Pixels.Num() == 0; }

	/** World XY square the mask covers. */
	FBox2D GetArea() const { return FBox2D(Min, Min + FVector2D(Extent)); }

	/** Full raise height, in cm. */
	double GetRaiseHeight() const { return Height; }

	/** How far the ground is raised at a world XY point, in cm. */
	double RaiseAt(const FVector2D& At) const;

	/** Copies the mask into Target (sampled by the landscape material) and tells every landscape in
	 *  the world where it lies (game worlds only). */
	void Upload(UWorld* World, UTextureRenderTarget2D* Target) const;

	/** Writes the mask as a PNG (R raise, G dirt). */
	bool SavePng(const FString& Path) const;

	/** Landscape material parameters (M_NR_AutoLandscape_GridRun, group "NR Wall Mask"). */
	static const FName AreaParameter;   // (MinX, MinY, 1 / Size, On)
	static const FName HeightParameter; // RaiseHeight in cm

private:
	TArray<FColor> Pixels;
	int32 Size = 0;
	FVector2D Min = FVector2D::ZeroVector;
	double Extent = 0.0;
	double Height = 0.0;
};
