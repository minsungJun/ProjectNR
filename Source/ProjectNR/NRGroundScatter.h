// Ground and ground cover for the grid run map: one flat ground plane under the whole grid, and
// grass, ferns and stones scattered over it as instanced meshes. The rooms, hall cells and
// corridors move with every generated map, so whenever the map generator settles on a new layout
// the cover is scattered again without the places those modules stand on (plus a margin), and
// nothing pokes through their floors. Where a module's floor is hidden in game so the landscape
// shows (Outdoor rooms, open corridors, the outside of Transition rooms), cover is kept away from
// the walls only.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NRWallMask.h"
#include "NRGroundScatter.generated.h"

class ANRMapGenerator;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/** One kind of ground cover: which meshes, how thick, how big. */
USTRUCT(BlueprintType)
struct FNRScatterLayer
{
	GENERATED_BODY()

	/** Picked at random for each instance. */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	TArray<TObjectPtr<UStaticMesh>> Meshes;

	/** Instances per square metre of ground, before the modules are cleared. */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "0.0"))
	float PerSquareMetre = 0.1f;

	/** Uniform scale, picked between X and Y. */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	FVector2D ScaleRange = FVector2D(0.8, 1.2);

	/** How far below the ground the instance's origin goes, in cm, picked between X and Y. Stones
	 *  with a centred origin sink into the ground this way. */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	FVector2D SinkRange = FVector2D(0.0, 0.0);

	/** Most degrees of random pitch and roll. Yaw is always random. */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxTilt = 0.0f;

	/** Instances fade out from here to the end distance, in cm; 0 keeps them always. */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "0.0"))
	float CullStart = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "0.0"))
	float CullEnd = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Scatter")
	bool bCastShadow = true;

	/** Stand on the landscape (traced down at each point) instead of the flat ground. Points with
	 *  no landscape under them, or steeper than MaxSlope, are skipped. For trees on the hills. */
	UPROPERTY(EditAnywhere, Category = "Scatter|Terrain")
	bool bFollowTerrain = false;

	/** Steepest slope a terrain instance stands on, in degrees. */
	UPROPERTY(EditAnywhere, Category = "Scatter|Terrain", meta = (ClampMin = "0.0", ClampMax = "90.0", EditCondition = "bFollowTerrain"))
	float MaxSlope = 35.0f;

	/** Half size of this layer's area around the actor, in cm; 0 uses the actor's HalfExtent. */
	UPROPERTY(EditAnywhere, Category = "Scatter|Terrain")
	FVector2D AreaHalfExtent = FVector2D::ZeroVector;

	/** Kept clear around every module on top of ClearMargin, in cm (a tree's crown). */
	UPROPERTY(EditAnywhere, Category = "Scatter|Terrain", meta = (ClampMin = "0.0"))
	float ExtraClearMargin = 0.0f;

	/** Leave this layer out of the open floors too (Outdoor rooms, open corridors): trees. */
	UPROPERTY(EditAnywhere, Category = "Scatter|Terrain")
	bool bSkipOpenFloors = false;
};

/** Trees planted on the raised ground outside the outdoor walls (the wall mask's berm). */
USTRUCT(BlueprintType)
struct FNRBermTrees
{
	GENERATED_BODY()

	/** Picked at random for each tree; none plants no trees. */
	UPROPERTY(EditAnywhere, Category = "Berm Trees")
	TArray<TObjectPtr<UStaticMesh>> Meshes;

	/** Distance to the nearest other tree, in cm, picked between X and Y for each tree. */
	UPROPERTY(EditAnywhere, Category = "Berm Trees")
	FVector2D Spacing = FVector2D(100.0, 250.0);

	/** Only where the ground is raised at least this share of the full height (0-1): the top of the berm. */
	UPROPERTY(EditAnywhere, Category = "Berm Trees", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinRaise = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Berm Trees")
	FVector2D ScaleRange = FVector2D(0.8, 1.3);

	/** How far below the ground the trunk's origin goes, in cm. */
	UPROPERTY(EditAnywhere, Category = "Berm Trees")
	FVector2D SinkRange = FVector2D(10.0, 30.0);

	UPROPERTY(EditAnywhere, Category = "Berm Trees", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxTilt = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Berm Trees")
	bool bCastShadow = true;
};

UCLASS()
class PROJECTNR_API ANRGroundScatter : public AActor
{
	GENERATED_BODY()

public:
	ANRGroundScatter();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Scatters again over the whole area (editor button). */
	UFUNCTION(CallInEditor, Category = "Scatter")
	void Rescatter();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Half size of the ground and the scattered area around the actor, in cm. */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "100.0"))
	FVector2D HalfExtent = FVector2D(15000.0, 15000.0);

	/** The ground's top, in cm above the actor. Below the modules' floors (Z = 0) so they cover it. */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	float GroundTop = -2.0f;

	UPROPERTY(EditAnywhere, Category = "Scatter")
	TObjectPtr<UMaterialInterface> GroundMaterial;

	UPROPERTY(EditAnywhere, Category = "Scatter")
	TArray<FNRScatterLayer> CoverLayers;

	/** Same seed, same cover (before clearing). */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	int32 Seed = 1;

	/** Mix each generated map's seed into Seed, so the cover is laid out anew for every map. */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	bool bRandomPerMap = true;

	/** Kept clear around every room, hall cell and corridor, in cm, so that cover beside a wall
	 *  does not grow through it. */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "0.0"))
	float ClearMargin = 150.0f;

	/** Width cleared along a corridor, in cm (the corridor is 8 m wide). */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "0.0"))
	float CorridorClearWidth = 800.0f;

	/**
	 * Cover is kept where a module's floor is hidden in game and the landscape shows: inside
	 * Outdoor rooms, along corridors with no Indoor end (and into Outdoor hall cells), and on the
	 * outside half of Transition rooms. Off: every module is cleared whole.
	 */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	bool bCoverOpenFloors = true;

	/** How far inside an Outdoor room's outline cover stops, in cm (its walls stand at the edge). */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "0.0", EditCondition = "bCoverOpenFloors"))
	float OpenRoomInset = 150.0f;

	/** Width of the strip kept along an open corridor or Outdoor hall arm, in cm (6 m between its walls). */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "0.0", EditCondition = "bCoverOpenFloors"))
	float OpenCorridorWidth = 400.0f;

	/**
	 * Depth of a Transition room's outside part, in cm from its edge opposite the Indoor corridor
	 * (Lvl_GR_T_Room_Combat: partition 920 cm in, less its wall and a margin).
	 */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "0.0", EditCondition = "bCoverOpenFloors"))
	float TransitionOutsideDepth = 790.0f;

	/** Decal actors with this tag are dirt paths: once the map is ready, every layer is cleared
	 *  from them (the modules bring their own decals, so they are found after loading). */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	FName DirtTag = TEXT("NRDirt");

	/** Part of a dirt decal's half size that is cleared, from its centre; the rest is its soft edge,
	 *  where cover may still grow. */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DirtClearFraction = 0.75f;

	/** Share of the cover that still grows on dirt (grass tufts, ferns, stones). Layers that skip
	 *  open floors (trees) never do. */
	UPROPERTY(EditAnywhere, Category = "Scatter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DirtCoverKeep = 0.2f;

	/** Actors with this tag are raised ground (mounds along outdoor walls): once the map is ready,
	 *  the flat layers stand on their surface instead of the ground, inside the modules too. */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	FName RaisedGroundTag = TEXT("NRGround");

	/** The map generator whose modules are cleared. Found in the level when empty. */
	UPROPERTY(EditAnywhere, Category = "Scatter")
	TObjectPtr<ANRMapGenerator> Generator;

	UPROPERTY(VisibleAnywhere, Category = "Scatter")
	TObjectPtr<UStaticMeshComponent> Ground;

	/** The outdoor wall mask drawn for every layout (NRWallMask.h): the landscape material raises
	 *  the ground outside the outdoor walls and blends dirt along them; cover there is lifted too. */
	UPROPERTY(EditAnywhere, Category = "Scatter|Wall Mask")
	FNRWallMaskSettings WallMask;

	/** Where the mask goes; the landscape material samples it. */
	UPROPERTY(EditAnywhere, Category = "Scatter|Wall Mask")
	TObjectPtr<UTextureRenderTarget2D> WallMaskTarget;

	/** Trees on the raised ground outside the outdoor walls. */
	UPROPERTY(EditAnywhere, Category = "Scatter|Wall Mask")
	FNRBermTrees BermTrees;

public:
	const FNRWallMask& GetWallMask() const { return Mask; }

protected:

private:
	/** Rebuilds the instances, leaving out points inside Clear but not inside Keep, and every point
	 *  inside Bare (world XY boxes). */
	void Scatter(const TArray<FBox2D>& Clear, const TArray<FBox2D>& Keep = TArray<FBox2D>(),
		const TArray<FBox2D>& Bare = TArray<FBox2D>());

	/** The cleared middle of every dirt decal in the world. */
	TArray<FBox2D> DirtBoxes() const;

	/** XY bounds of every actor with Tag. */
	TArray<FBox2D> TaggedBoxes(FName Tag) const;

	/** The top of raised ground at At, false when there is none or it does not stand above the ground. */
	bool RaisedHeight(const FVector2D& At, double& OutHeight) const;

	/** The mask of the current layout. */
	FNRWallMask Mask;

	/** Rebuilds and uploads the mask for the generator's current layout. */
	void BuildWallMask();

	/** Seed the cover is drawn with: Seed, mixed with the map's seed when bRandomPerMap. */
	uint32 CoverSeed() const;

	/** Adds the berm trees of the current mask to Batches; returns how many. */
	int32 PlantBermTrees(TMap<UHierarchicalInstancedStaticMeshComponent*, TArray<FTransform>>& Batches);

	/** Raised ground of the current scatter (set only while scattering for a ready map). */
	TArray<FBox2D> RaisedBoxes;

	/** Whether the dirt of the current layout's loaded modules has been cleared yet. */
	bool bDirtCleared = false;

	/** The boxes of the last scatter, kept for NR.Grid.ScatterDebug. */
	TArray<FBox2D> DebugClear;
	TArray<FBox2D> DebugKeep;
	TArray<FBox2D> DebugBare;
	TArray<FBox2D> DebugRaised;

	/** Draws the last scatter's boxes for a little longer than the layout check interval. */
	void DrawDebugAreas() const;

	/** Scatters for the generator's current layout. */
	void ScatterForLayout();

	/** The boxes the generator's current layout stands on; empty with no layout. */
	TArray<FBox2D> ModuleBoxes() const;

	/** The parts of those boxes whose floor is hidden and get cover after all (bCoverOpenFloors). */
	TArray<FBox2D> OpenFloorBoxes() const;

	/** Re-scatters when the generator's layout has changed. */
	void CheckLayout();

	/** Seed, attempt, rooms and corridor count of the generator's layout; 0 without a generator. */
	uint32 CurrentLayoutKey() const;

	/** The landscape's height at At, false when there is none or it is steeper than MinNormalZ allows. */
	bool TerrainHeight(const FVector2D& At, double MinNormalZ, double& OutHeight) const;

	/** One instanced component per mesh, made on demand. */
	UHierarchicalInstancedStaticMeshComponent* ComponentFor(UStaticMesh* Mesh, const FNRScatterLayer& Layer);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Instances;

	/** What the cover was last cleared for: seed, rooms and corridors of the layout. */
	uint32 LayoutKey = 0;

	FTimerHandle LayoutTimer;
};
