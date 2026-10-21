// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NRGridTypes.h"
#include "NRMapGenerationConfig.h"
#include "NRMapGenerator.generated.h"

class ANRMapGenerator;
class ANRRoom;
class ULevelStreamingDynamic;
class UWorld;
class UNRCorridorTemplateData;
struct FNRCorridorTemplateDesc;
struct FNRRoomTemplateDesc;

/** Where the generator is in building a map. Separate from the run's own won/lost state. */
UENUM(BlueprintType)
enum class ENRMapGenState : uint8
{
	Idle,
	/** Building the connection graph and choosing templates. */
	Generating,
	/** Level instances requested, waiting for every one to load and show. */
	Loading,
	/** Modules set up; waiting for navigation to cover the whole map. */
	Initializing,
	Ready,
	Failed
};

/** Where the layout to build comes from. */
UENUM(BlueprintType)
enum class ENRMapLayoutSource : uint8
{
	/** Generated from Seed with the Config's settings and templates. */
	Generated,
	/** FixedLayout, all rooms from RoomLevel (stage A test). */
	Fixed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNRMapReady, ANRMapGenerator*, Generator);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNRMapFailed, ANRMapGenerator*, Generator, const FString&, Reason);

/**
 * Builds a run map out of hand-made room and corridor levels, loaded as runtime level
 * instances (ULevelStreamingDynamic) at grid positions. docs/CLAUDE_GRID_LEVEL_INSTANCE_PLAN.md.
 *
 * StartMap: Generating -> Loading -> Initializing -> Ready (or Failed).
 *  - Generating: FNRGridLayoutGenerator builds the graph for the seed, then picks a room
 *    template and rotation for every room. Every random choice happens here, before anything
 *    loads, so load order cannot change the map.
 *  - Loading: every room and corridor is requested at once. Requests are held in a UPROPERTY
 *    array and tagged with a generation number; anything from an earlier load is ignored.
 *  - Initializing, once every module is loaded and shown: doorways facing a connection have
 *    their blocker (NRBlock.<dir>) hidden and collision turned off; every connection's room
 *    ports (NRPort.<dir>) and corridor ends (NRPort.A/B) must meet within PortTolerance and
 *    face each other. Then navigation must find a whole path from the start room to the boss
 *    room. No fixed delay: it is polled, with a timeout.
 *  - Rooms (stage D): every room level holds one ANRRoom (bWaitForGenerator), doors tagged
 *    NRDoor.<dir> in its doorways and its enemies. The generator hands each room its id, type,
 *    boss flag, the doors on joined doorways (opened) and the enemies of that level only; the
 *    doors on blocked doorways are hidden, the blocker already walls them. A Combat, Elite or
 *    Boss room without an enemy, or a Start or Safe room with one, fails the map.
 *  - Ready: the player is moved to the start room (NRSpawn marker, or its centre) and gets
 *    input back; the rooms are registered with UNRRunSubsystem, which starts the run and
 *    takes over the loadout rule; only then do the rooms open their gates. OnMapReady fires.
 *    While loading, the player's input is off (they wait on the persistent map's pad).
 *  - Any failure (a missing level, a timeout, a port off spec, no template for a room) unloads
 *    everything this load requested and fires OnMapFailed with the reason and seed. A partial
 *    map is never played.
 *
 * Every module is found through its own GetLoadedLevel(), never by searching the world, so two
 * copies of the same room never get mixed up.
 *
 * Module spec: rooms are RoomSize square with the floor's top at Z = 0 and the origin at the
 * floor's centre; doorways are centred on each side. Corridors run along their local X with
 * ends at +-CorridorLength/2. Cells are RoomSize + CorridorLength apart.
 *
 * Environments (docs/CLAUDE_GRID_ENVIRONMENT_PLAN.md): every room and hall cell is Indoor, Outdoor
 * or Transition by the settings' EnvironmentMode, and loads a template of its environment. With
 * the Config's CorridorTemplates, each connection loads the corridor chosen for its two rooms'
 * environments, turned so its NRPort.A end meets the room of its EndpointA environment.
 */
UCLASS()
class PROJECTNR_API ANRMapGenerator : public AActor
{
	GENERATED_BODY()

public:
	ANRMapGenerator();

	/**
	 * Builds a map: generates (or takes FixedLayout), then loads it. Drops whatever was loaded
	 * before. Seed 0 picks a random one. Returns false when it fails before loading starts;
	 * otherwise watch GetState or OnMapReady / OnMapFailed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Map Generation")
	bool StartMap(int32 InSeed = 0);

	/**
	 * StartMap with the connection shape and the environment mode chosen here rather than by the
	 * Config: the two choices a run start screen offers. A restart of the run keeps them.
	 */
	UFUNCTION(BlueprintCallable, Category = "Map Generation")
	bool StartMapWithOptions(int32 InSeed, ENRGridLayoutShape InShape, ENRMapEnvironmentMode InEnvironmentMode);

	/** StartMap with only the environment mode chosen here; the shape stays the Config's. */
	UFUNCTION(BlueprintCallable, Category = "Map Generation")
	bool StartMapWithEnvironment(int32 InSeed, ENRMapEnvironmentMode InEnvironmentMode);

	/** StartMap with the shape and environment mode replaced where set, the Config's where not.
	 *  StartMap uses neither. */
	bool StartMapWithChoices(int32 InSeed, TOptional<ENRGridLayoutShape> InShape, TOptional<ENRMapEnvironmentMode> InEnvironmentMode);

	/** Unloads and removes every module this generator loaded. */
	UFUNCTION(BlueprintCallable, Category = "Map Generation")
	void UnloadLayout();

	UFUNCTION(BlueprintPure, Category = "Map Generation")
	ENRMapGenState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Map Generation")
	const FString& GetLastError() const { return LastError; }

	/** The layout being built or built, rooms with their templates. */
	const FNRGeneratedLayout& GetActiveLayout() const { return ActiveLayout; }

	/** World position of a cell's centre, on the floor. */
	FVector GetCellCenter(const FIntPoint& Cell) const;

	/** World position of a room's centre (the middle of its footprint), on the floor. */
	FVector GetRoomCenter(const FNRGeneratedRoom& Room) const;

	/** Template descriptions of the Config's rooms, index for index. Empty without a Config. */
	TArray<FNRRoomTemplateDesc> DescribeTemplates() const;

	/** The Config's corridor templates, index for index (a missing one has weight 0). Empty
	 *  without any: every link then uses the single fallback corridor. */
	TArray<FNRCorridorTemplateDesc> DescribeCorridors() const;

	/** Logs every module, the doorway and floor checks, navigation and the placement signature. */
	void LogStatus() const;

	/**
	 * Placement of every loaded module as text: rooms by id with template, turn, position,
	 * opened doorways and where their port markers ended up; corridors by index. Two loads of
	 * the same seed must give the same text, whatever order their levels arrived in.
	 */
	FString BuildSignature() const;

	/**
	 * Generates a layout only, for debugging (nothing is loaded). Uses Config and its templates,
	 * or InlineSettings and one-cell rooms without one; the shape and environment mode chosen with
	 * StartMapWithOptions apply, and ShapeOverride / EnvironmentOverride replace them when set.
	 */
	bool GenerateLayout(int32 InSeed, FString& OutError, TOptional<ENRGridLayoutShape> ShapeOverride = {},
		TOptional<ENRMapEnvironmentMode> EnvironmentOverride = {});

	/** Debug view of a layout: cells, corridors (main path thicker), room number and type. */
	void DrawLayout(const FNRGeneratedLayout& Layout, float Duration) const;

	const FNRGeneratedLayout& GetGeneratedLayout() const { return GeneratedLayout; }

	/** Settings in use: the Config asset's, or InlineSettings. */
	const FNRMapGenSettings& GetSettings() const;

	/** GetSettings with the shape and environment mode chosen for this map, if any. */
	FNRMapGenSettings GetEffectiveSettings() const;

	/** Debug: request modules in a shuffled order on the next StartMap. */
	void SetShuffleLoadOrder(bool bShuffle) { bShuffleLoadOrder = bShuffle; }

	/** Debug: give this room a level that does not exist on the next StartMap (INDEX_NONE for none). */
	void SetDebugMissingRoom(int32 RoomId) { DebugMissingRoomId = RoomId; }

	/** Debug: remove this room's enemies before it is set up on the next StartMap, as if its
	 *  template had none (INDEX_NONE for none). */
	void SetDebugEmptyRoom(int32 RoomId) { DebugEmptyRoomId = RoomId; }

	UPROPERTY(BlueprintAssignable, Category = "Map Generation")
	FOnNRMapReady OnMapReady;

	UPROPERTY(BlueprintAssignable, Category = "Map Generation")
	FOnNRMapFailed OnMapFailed;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation")
	ENRMapLayoutSource LayoutSource = ENRMapLayoutSource::Generated;

	/** Seed for a generated map. 0 picks a new one every play. The level option GridSeed=N
	 *  (a same-seed restart) overrides it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation")
	int32 Seed = 0;

	/** Generation settings and templates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation")
	TObjectPtr<UNRMapGenerationConfig> Config;

	/** Settings when there is no Config. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation")
	FNRMapGenSettings InlineSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation")
	bool bStartOnBeginPlay = true;

	/** Moves the player to the start room once the modules are set up; the map is ready only
	 *  after the player has arrived there. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation")
	bool bMovePlayerToStartOnReady = true;

	/** Starts a run over the generated rooms once the map is ready (stage D). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation")
	bool bStartRunOnReady = true;

	/** Stage A layout, used when LayoutSource is Fixed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Fixed")
	FNRGeneratedLayout FixedLayout;

	/** Room level for FixedLayout, and for any room without a template. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Fixed")
	TSoftObjectPtr<UWorld> RoomLevel;

	/** Corridor level when the Config has no corridor template. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Fixed")
	TSoftObjectPtr<UWorld> CorridorLevel;

	/** Outer size of one cell of a room, in cm. Every room template must match it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Spec", meta = (ClampMin = "100.0"))
	float RoomSize = 3600.0f;

	/** Corridor length when the Config has no corridor template, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Spec", meta = (ClampMin = "0.0"))
	float CorridorLength = 800.0f;

	/** How far apart matching port markers may be, in cm, before the map is refused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Spec", meta = (ClampMin = "0.0"))
	float PortTolerance = 1.0f;

	/** Loads in a blocking pass instead of in the background. Off: the map loads asynchronously. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Loading")
	bool bBlockOnLoad = false;

	/** Seconds to wait for every module to load and show. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Loading", meta = (ClampMin = "1.0"))
	float LoadTimeout = 30.0f;

	/** Seconds to wait for navigation to join the start room to the boss room. A large grid
	 *  rebuilt in a world that already held a map can take 20-25 s in the editor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Loading", meta = (ClampMin = "1.0"))
	float NavigationTimeout = 60.0f;

	/** How high above the floor DrawLayout draws, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map Generation|Debug")
	float DebugDrawHeight = 600.0f;

	/** The last layout GenerateLayout produced (graph only). */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Map Generation|Debug")
	FNRGeneratedLayout GeneratedLayout;

	/** The layout being built or built. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Map Generation|Debug")
	FNRGeneratedLayout ActiveLayout;

private:
	/** One loaded level instance. Parallel to Streamings. */
	struct FModule
	{
		bool bCorridor = false;
		/** Room id, or connection index for a corridor. */
		int32 Id = INDEX_NONE;
		FTransform Transform;
		FString LevelPath;
	};

	UFUNCTION()
	void HandleModuleShown();

	void CheckLoadTimeout();
	void InitializeModules();
	void PollNavigation();
	void BecomeReady();
	void Fail(const FString& Reason);

	/** Loads every module of ActiveLayout. */
	bool LoadActiveLayout(FString& OutError);

	ULevelStreamingDynamic* LoadModule(const TSoftObjectPtr<UWorld>& Level, const FTransform& Transform, const FString& Name);

	TSoftObjectPtr<UWorld> GetRoomLevel(const FNRGeneratedRoom& Room) const;
	/** The corridor template a connection chose, or null for the fallback corridor. */
	const UNRCorridorTemplateData* GetCorridorTemplate(const FNRGeneratedConnection& Connection) const;
	TSoftObjectPtr<UWorld> GetCorridorLevel(const FNRGeneratedConnection& Connection) const;
	float GetCorridorLength() const;

	/** Actors of a loaded module with the given tag. */
	TArray<AActor*> FindTagged(int32 ModuleIndex, FName Tag) const;
	int32 FindRoomModule(int32 RoomId) const;
	int32 FindCorridorModule(int32 ConnectionIndex) const;

	FTransform GetRoomTransform(const FNRGeneratedRoom& Room) const;
	FTransform GetCorridorTransform(const FNRGeneratedConnection& Connection) const;

	/** Whether navigation joins the start room to the boss room (or the first two rooms). */
	bool HasNavigationPath(float* OutLength = nullptr, bool* OutBuilding = nullptr) const;

	void MovePlayerToStart() const;

	/** Hands every room level its room setup; adds problems to Errors. */
	void InitializeRooms(TArray<FString>& Errors);

	/** Takes the player's input away while the map loads, and gives it back. */
	void HoldPlayer(bool bHold);

	/** Rooms set up for the current map, for the run and for unloading. */
	TArray<TWeakObjectPtr<ANRRoom>> GeneratedRooms;
	bool bPlayerHeld = false;

	ENRMapGenState State = ENRMapGenState::Idle;
	FString LastError;

	/** Bumped on every load and unload, so a callback from an earlier load is ignored. */
	int32 Generation = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ULevelStreamingDynamic>> Streamings;

	TArray<FModule> Modules;

	FTimerHandle TimeoutTimer;
	FTimerHandle NavigationTimer;
	double LoadStartTime = 0.0;
	double NavigationStartTime = 0.0;

	bool bShuffleLoadOrder = false;
	int32 DebugMissingRoomId = INDEX_NONE;
	int32 DebugEmptyRoomId = INDEX_NONE;

	/** Shape and environment mode chosen for this map instead of the Config's (StartMapWithChoices,
	 *  the GridShape / GridEnv level options, NR.Grid.Start). */
	TOptional<ENRGridLayoutShape> ShapeChoice;
	TOptional<ENRMapEnvironmentMode> EnvironmentChoice;
};
