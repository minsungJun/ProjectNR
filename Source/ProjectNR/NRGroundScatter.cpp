#include "NRGroundScatter.h"

#include "NRMapGenerator.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/DecalComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DecalActor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRGroundScatter, Log, All);

static FAutoConsoleCommandWithWorldAndArgs NRWallMaskSaveCommand(
	TEXT("NR.Grid.WallMaskSave"),
	TEXT("Writes the outdoor wall mask of the play world as Saved/NRWallMask.png (red: raised ground, green: dirt)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		for (TActorIterator<ANRGroundScatter> It(World); It; ++It)
		{
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NRWallMask.png"));
			const bool bSaved = It->GetWallMask().SavePng(Path);
			UE_LOG(LogNRGroundScatter, Log, TEXT("Wall mask %s %s"), bSaved ? TEXT("saved to") : TEXT("is empty, not saved:"), *Path);
			return;
		}
		UE_LOG(LogNRGroundScatter, Warning, TEXT("No ground scatter in this world"));
	}));

static TAutoConsoleVariable<int32> CVarNRScatterDebug(
	TEXT("NR.Grid.ScatterDebug"), 0,
	TEXT("1 draws where the ground scatter clears its cover: the module outlines, each layer's cleared area, ")
	TEXT("the open floors where cover is kept, the dirt and the mounds."));

ANRGroundScatter::ANRGroundScatter()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Ground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ground"));
	Ground->SetupAttachment(Root);
	Ground->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Ground->SetStaticMesh(Cube.Object);
	}
	static ConstructorHelpers::FObjectFinderOptional<UTextureRenderTarget2D> MaskTarget(TEXT("/Game/Levels/GridRun/RT_NR_WallMask.RT_NR_WallMask"));
	WallMaskTarget = MaskTarget.Get();
}

void ANRGroundScatter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// A 20 cm slab whose top is GroundTop, as wide as the scattered area. The cube is 100 cm.
	constexpr float Thickness = 20.0f;
	Ground->SetRelativeScale3D(FVector(HalfExtent.X * 2.0 / 100.0, HalfExtent.Y * 2.0 / 100.0, Thickness / 100.0));
	Ground->SetRelativeLocation(FVector(0.0, 0.0, GroundTop - Thickness * 0.5));
	if (GroundMaterial)
	{
		Ground->SetMaterial(0, GroundMaterial);
	}

	// The editor shows the whole cover; play clears the modules (BeginPlay).
	UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		Scatter({});
	}
}

void ANRGroundScatter::Rescatter()
{
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		ScatterForLayout();
	}
	else
	{
		Scatter({});
	}
}

void ANRGroundScatter::ScatterForLayout()
{
	// The dirt decals come with the modules, so they are only there once the map is ready.
	bDirtCleared = Generator && Generator->GetState() == ENRMapGenState::Ready;
	if (CurrentLayoutKey() != LayoutKey || Mask.IsEmpty())
	{
		BuildWallMask();
	}
	RaisedBoxes = bDirtCleared ? TaggedBoxes(RaisedGroundTag) : TArray<FBox2D>();
	Scatter(ModuleBoxes(), bCoverOpenFloors ? OpenFloorBoxes() : TArray<FBox2D>(), bDirtCleared ? DirtBoxes() : TArray<FBox2D>());
	RaisedBoxes.Reset();
}

void ANRGroundScatter::BuildWallMask()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	if (Generator && Generator->GetActiveLayout().Rooms.Num() > 0)
	{
		Mask.Build(*Generator, WallMask);
	}
	else
	{
		Mask.Reset();
	}
	Mask.Upload(World, WallMaskTarget);
}

uint32 ANRGroundScatter::CoverSeed() const
{
	const uint32 Base = GetTypeHash(Seed);
	if (!bRandomPerMap || !Generator || Generator->GetActiveLayout().Rooms.Num() == 0)
	{
		return Base;
	}
	const FNRGeneratedLayout& Layout = Generator->GetActiveLayout();
	return HashCombine(Base, HashCombine(GetTypeHash(Layout.Seed), GetTypeHash(Layout.Attempt)));
}

int32 ANRGroundScatter::PlantBermTrees(TMap<UHierarchicalInstancedStaticMeshComponent*, TArray<FTransform>>& Batches)
{
	TArray<UStaticMesh*> Meshes;
	for (UStaticMesh* Mesh : BermTrees.Meshes)
	{
		if (Mesh)
		{
			Meshes.Add(Mesh);
		}
	}
	const double FullRaise = Mask.GetRaiseHeight();
	if (Meshes.Num() == 0 || Mask.IsEmpty() || FullRaise <= 0.0)
	{
		return 0;
	}
	const FBox2D Area = Mask.GetArea();
	FNRScatterLayer Look;
	Look.MaxTilt = BermTrees.MaxTilt;
	Look.bCastShadow = BermTrees.bCastShadow;

	// Dart throwing: candidates on a jittered grid finer than the smallest spacing, taken in random
	// order; each tree keeps its own spacing clear around it. A hash grid finds the neighbours.
	FRandomStream Stream(HashCombine(CoverSeed(), 0x4245524Du));
	const double MinGap = FMath::Max(10.0, FMath::Min(BermTrees.Spacing.X, BermTrees.Spacing.Y));
	const double MaxGap = FMath::Max(MinGap, FMath::Max(BermTrees.Spacing.X, BermTrees.Spacing.Y));
	const double Step = MinGap * 0.5;
	TArray<FVector2D> Candidates;
	for (double Y = Area.Min.Y; Y < Area.Max.Y; Y += Step)
	{
		for (double X = Area.Min.X; X < Area.Max.X; X += Step)
		{
			const FVector2D At(X + Stream.FRandRange(0.0, Step), Y + Stream.FRandRange(0.0, Step));
			if (Mask.RaiseAt(At) >= FullRaise * BermTrees.MinRaise)
			{
				Candidates.Add(At);
			}
		}
	}
	for (int32 Index = Candidates.Num() - 1; Index > 0; --Index)
	{
		Candidates.Swap(Index, Stream.RandRange(0, Index));
	}

	struct FTree
	{
		FVector2D At;
		double Gap;
	};
	TArray<FTree> Trees;
	TMap<FIntPoint, TArray<int32>> Grid;
	auto CellOf = [MaxGap](const FVector2D& At) { return FIntPoint(FMath::FloorToInt(At.X / MaxGap), FMath::FloorToInt(At.Y / MaxGap)); };
	for (const FVector2D& At : Candidates)
	{
		const double Gap = Stream.FRandRange(MinGap, MaxGap);
		const FIntPoint Cell = CellOf(At);
		bool bFree = true;
		for (int32 DY = -1; DY <= 1 && bFree; ++DY)
		{
			for (int32 DX = -1; DX <= 1 && bFree; ++DX)
			{
				const TArray<int32>* Near = Grid.Find(Cell + FIntPoint(DX, DY));
				if (!Near)
				{
					continue;
				}
				for (int32 Other : *Near)
				{
					// Both trees keep their own spacing.
					if (FVector2D::DistSquared(At, Trees[Other].At) < FMath::Square(FMath::Max(Gap, Trees[Other].Gap)))
					{
						bFree = false;
						break;
					}
				}
			}
		}
		if (!bFree)
		{
			continue;
		}
		Grid.FindOrAdd(Cell).Add(Trees.Add({ At, Gap }));
		double Surface = GetActorLocation().Z + GroundTop;
		TerrainHeight(At, -1.0, Surface);
		UStaticMesh* Mesh = Meshes[Stream.RandRange(0, Meshes.Num() - 1)];
		const FRotator Turn(Stream.FRandRange(-BermTrees.MaxTilt, BermTrees.MaxTilt), Stream.FRandRange(0.0f, 360.0f),
			Stream.FRandRange(-BermTrees.MaxTilt, BermTrees.MaxTilt));
		const float Scale = Stream.FRandRange(BermTrees.ScaleRange.X, BermTrees.ScaleRange.Y);
		const FVector Location(At.X, At.Y, Surface + Mask.RaiseAt(At) - Stream.FRandRange(BermTrees.SinkRange.X, BermTrees.SinkRange.Y));
		Batches.FindOrAdd(ComponentFor(Mesh, Look)).Emplace(Turn, Location, FVector(Scale));
	}
	UE_LOG(LogNRGroundScatter, Log, TEXT("%s: %d berm tree(s) from %d candidate point(s), spacing %.0f-%.0f cm"),
		*GetName(), Trees.Num(), Candidates.Num(), MinGap, MaxGap);
	return Trees.Num();
}

TArray<FBox2D> ANRGroundScatter::TaggedBoxes(FName Tag) const
{
	TArray<FBox2D> Boxes;
	if (Tag.IsNone())
	{
		return Boxes;
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(Tag))
		{
			const FBox Box = It->GetComponentsBoundingBox();
			Boxes.Emplace(FVector2D(Box.Min), FVector2D(Box.Max));
		}
	}
	return Boxes;
}

bool ANRGroundScatter::RaisedHeight(const FVector2D& At, double& OutHeight) const
{
	if (!RaisedBoxes.ContainsByPredicate([&At](const FBox2D& Box) { return Box.IsInside(At); }))
	{
		return false;
	}
	TArray<FHitResult> Hits;
	GetWorld()->LineTraceMultiByObjectType(Hits, FVector(At.X, At.Y, 100000.0), FVector(At.X, At.Y, -100000.0),
		FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(NRGroundScatterRaised), true, this));
	for (const FHitResult& Hit : Hits)
	{
		if (Hit.GetActor() && Hit.GetActor()->ActorHasTag(RaisedGroundTag))
		{
			// Only where the mound stands above the ground; its sunk edges stay bare.
			if (Hit.ImpactPoint.Z <= GetActorLocation().Z + GroundTop + 5.0)
			{
				return false;
			}
			OutHeight = Hit.ImpactPoint.Z;
			return true;
		}
	}
	return false;
}

TArray<FBox2D> ANRGroundScatter::DirtBoxes() const
{
	TArray<FBox2D> Boxes;
	if (DirtTag.IsNone())
	{
		return Boxes;
	}
	for (TActorIterator<ADecalActor> It(GetWorld()); It; ++It)
	{
		const UDecalComponent* Decal = It->GetDecal();
		if (!Decal || !It->ActorHasTag(DirtTag))
		{
			continue;
		}
		const FBox Box = Decal->Bounds.GetBox();
		const FVector2D Center(Box.GetCenter());
		const FVector2D Half = FVector2D(Box.GetExtent()) * DirtClearFraction;
		Boxes.Emplace(Center - Half, Center + Half);
	}
	return Boxes;
}

void ANRGroundScatter::BeginPlay()
{
	Super::BeginPlay();

	if (!Generator)
	{
		for (TActorIterator<ANRMapGenerator> It(GetWorld()); It; ++It)
		{
			Generator = *It;
			break;
		}
	}
	// A play world copy starts with no instanced components of its own (they are never saved).
	Instances.Reset();
	ScatterForLayout();
	// The generator builds a layout on its own schedule (play start, a restart, NR.Grid.Start):
	// look a few times a second and clear again as soon as a new one appears, before its modules
	// have finished loading.
	GetWorldTimerManager().SetTimer(LayoutTimer, this, &ANRGroundScatter::CheckLayout, 0.25f, true);
}

void ANRGroundScatter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(LayoutTimer);
	Super::EndPlay(EndPlayReason);
}

uint32 ANRGroundScatter::CurrentLayoutKey() const
{
	if (!Generator)
	{
		return 0;
	}
	const FNRGeneratedLayout& Layout = Generator->GetActiveLayout();
	uint32 Key = HashCombine(GetTypeHash(Layout.Seed), GetTypeHash(Layout.Attempt));
	for (const FNRGeneratedRoom& Room : Layout.Rooms)
	{
		Key = HashCombine(Key, HashCombine(GetTypeHash(Room.Cell), GetTypeHash(Room.Footprint)));
	}
	return HashCombine(Key, GetTypeHash(Layout.Connections.Num()));
}

void ANRGroundScatter::CheckLayout()
{
	if (CurrentLayoutKey() != LayoutKey
		|| (!bDirtCleared && Generator && Generator->GetState() == ENRMapGenState::Ready))
	{
		ScatterForLayout();
	}
	if (CVarNRScatterDebug.GetValueOnGameThread() != 0)
	{
		DrawDebugAreas();
	}
}

void ANRGroundScatter::DrawDebugAreas() const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Redrawn by CheckLayout every 0.25 s, so the lines last a little longer than that.
	constexpr float Life = 0.3f;
	const double Floor = GetActorLocation().Z + GroundTop;
	auto Draw = [World, Floor](const TArray<FBox2D>& Boxes, double Grow, double Lift, const FColor& Color, float Thickness)
	{
		for (const FBox2D& Box : Boxes)
		{
			const FVector2D Min = Box.Min - FVector2D(Grow);
			const FVector2D Max = Box.Max + FVector2D(Grow);
			if (Min.X >= Max.X || Min.Y >= Max.Y)
			{
				continue;
			}
			DrawDebugBox(World, FVector((Min + Max) * 0.5, Floor + Lift), FVector((Max - Min) * 0.5, 0.0),
				Color, false, Life, SDPG_Foreground, Thickness);
		}
	};

	// The module outlines themselves (the clear boxes less ClearMargin): rooms' outer walls,
	// corridors' outer wall lines.
	Draw(DebugClear, -ClearMargin, 40.0, FColor::White, 4.0f);

	// One rectangle per distinct margin, so layers sharing a margin share a colour.
	TArray<float> Extras;
	for (const FNRScatterLayer& Layer : CoverLayers)
	{
		Extras.AddUnique(Layer.ExtraClearMargin);
	}
	Extras.Sort();
	static const FColor Palette[] = { FColor::Red, FColor::Orange, FColor::Magenta, FColor::Purple, FColor::Silver };
	constexpr int32 PaletteSize = UE_ARRAY_COUNT(Palette);
	const uint64 KeyBase = 0x4E52534300000000ull; // "NRSC"
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase, Life, FColor::White,
			TEXT("Ground scatter (NR.Grid.ScatterDebug): white = module outline, cleared out to outline + margin"));
	}
	for (int32 Group = 0; Group < Extras.Num(); ++Group)
	{
		const FColor Color = Palette[Group % PaletteSize];
		Draw(DebugClear, Extras[Group], 60.0 + 20.0 * Group, Color, 8.0f);
		if (!GEngine)
		{
			continue;
		}
		FString Names;
		for (int32 Index = 0; Index < CoverLayers.Num(); ++Index)
		{
			const FNRScatterLayer& Layer = CoverLayers[Index];
			if (Layer.ExtraClearMargin != Extras[Group])
			{
				continue;
			}
			const UStaticMesh* First = Layer.Meshes.Num() > 0 ? Layer.Meshes[0].Get() : nullptr;
			Names += FString::Printf(TEXT("%s%d %s"), Names.IsEmpty() ? TEXT("") : TEXT(", "), Index, First ? *First->GetName() : TEXT("-"));
		}
		GEngine->AddOnScreenDebugMessage(KeyBase + 1 + Group, Life, Color,
			FString::Printf(TEXT("  outline + %.1f m: layer %s"), (ClearMargin + Extras[Group]) / 100.0f, *Names));
	}

	Draw(DebugKeep, 0.0, 30.0, FColor::Green, 6.0f);
	Draw(DebugBare, 0.0, 20.0, FColor::Yellow, 6.0f);
	Draw(DebugRaised, 0.0, 150.0, FColor::Cyan, 4.0f);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(KeyBase + 100, Life, FColor::Green, TEXT("  green = open floor, cover kept (trees excluded)"));
		GEngine->AddOnScreenDebugMessage(KeyBase + 101, Life, FColor::Yellow, TEXT("  yellow = dirt, bare (some tufts stay)"));
		GEngine->AddOnScreenDebugMessage(KeyBase + 102, Life, FColor::Cyan, TEXT("  cyan = mounds, cover on top"));
	}
#endif
}

TArray<FBox2D> ANRGroundScatter::OpenFloorBoxes() const
{
	TArray<FBox2D> Boxes;
	if (!Generator)
	{
		return Boxes;
	}
	const FNRGeneratedLayout& Layout = Generator->GetActiveLayout();
	const double Pitch = Generator->GetCellPitch();
	const double Room = Generator->GetRoomSize();
	auto HalfSize = [Pitch, Room](const FNRGeneratedRoom& Each)
	{
		return FVector2D(((Each.Footprint.X - 1) * Pitch + Room) * 0.5, ((Each.Footprint.Y - 1) * Pitch + Room) * 0.5);
	};
	auto IsIndoor = [](const FNRGeneratedRoom* Each) { return !Each || Each->Environment == ENRRoomEnvironment::Indoor; };

	for (const FNRGeneratedRoom& Each : Layout.Rooms)
	{
		const FVector2D Center(Generator->GetRoomCenter(Each));
		const FVector2D Half = HalfSize(Each);
		if (Each.Environment == ENRRoomEnvironment::Outdoor && Each.RoomType != ENRGridRoomType::Hall)
		{
			// Walls stand on the outline; the hall cells' walkways come from their corridors below.
			const FVector2D Inner = Half - FVector2D(OpenRoomInset);
			if (Inner.X > 0.0 && Inner.Y > 0.0)
			{
				Boxes.Emplace(Center - Inner, Center + Inner);
			}
		}
		else if (Each.Environment == ENRRoomEnvironment::Transition && Each.RoomType != ENRGridRoomType::Hall)
		{
			// The building half faces the Indoor corridor; the outside half is the far edge's strip.
			for (const FNRGeneratedConnection& Connection : Layout.Connections)
			{
				const bool bFromThis = Connection.RoomA == Each.RoomId;
				if (!bFromThis && Connection.RoomB != Each.RoomId)
				{
					continue;
				}
				if (!IsIndoor(Layout.FindRoom(bFromThis ? Connection.RoomB : Connection.RoomA)))
				{
					continue;
				}
				const FVector2D ToIndoor = (FVector2D(Generator->GetCellCenter(bFromThis ? Connection.CellB : Connection.CellA))
					- FVector2D(Generator->GetCellCenter(bFromThis ? Connection.CellA : Connection.CellB))).GetSafeNormal();
				const FVector2D Out = -ToIndoor;
				const FVector2D Across(FMath::Abs(Out.Y), FMath::Abs(Out.X));
				const FVector2D Edge = Center + Out * FVector2D(Half.X, Half.Y);
				const FVector2D Deep = Edge - Out * TransitionOutsideDepth;
				const FVector2D Side = Across * (FVector2D(Half.X, Half.Y) - FVector2D(OpenRoomInset));
				Boxes.Emplace(FVector2D(FMath::Min(Edge.X, Deep.X), FMath::Min(Edge.Y, Deep.Y)) - Side,
					FVector2D(FMath::Max(Edge.X, Deep.X), FMath::Max(Edge.Y, Deep.Y)) + Side);
				break;
			}
		}
	}

	for (const FNRGeneratedConnection& Connection : Layout.Connections)
	{
		const FNRGeneratedRoom* RoomA = Layout.FindRoom(Connection.RoomA);
		const FNRGeneratedRoom* RoomB = Layout.FindRoom(Connection.RoomB);
		if (IsIndoor(RoomA) || IsIndoor(RoomB))
		{
			continue;
		}
		// Between the two rooms' edges; into an Outdoor hall cell up to its centre (the arm).
		const FVector2D A(Generator->GetCellCenter(Connection.CellA));
		const FVector2D B(Generator->GetCellCenter(Connection.CellB));
		const FVector2D Dir = (B - A).GetSafeNormal();
		auto IntoCell = [Room](const FNRGeneratedRoom* Each)
		{
			return Each->Environment == ENRRoomEnvironment::Outdoor && Each->RoomType == ENRGridRoomType::Hall ? 0.0 : Room * 0.5;
		};
		const FVector2D Start = A + Dir * IntoCell(RoomA);
		const FVector2D End = B - Dir * IntoCell(RoomB);
		const FVector2D Side = FVector2D(FMath::Abs(Dir.Y), FMath::Abs(Dir.X)) * (OpenCorridorWidth * 0.5);
		Boxes.Emplace(FVector2D(FMath::Min(Start.X, End.X), FMath::Min(Start.Y, End.Y)) - Side,
			FVector2D(FMath::Max(Start.X, End.X), FMath::Max(Start.Y, End.Y)) + Side);
	}
	return Boxes;
}

TArray<FBox2D> ANRGroundScatter::ModuleBoxes() const
{
	TArray<FBox2D> Boxes;
	if (!Generator)
	{
		return Boxes;
	}
	const FNRGeneratedLayout& Layout = Generator->GetActiveLayout();
	const double Pitch = Generator->GetCellPitch();
	const double Room = Generator->GetRoomSize();
	for (const FNRGeneratedRoom& Each : Layout.Rooms)
	{
		const FVector Center = Generator->GetRoomCenter(Each);
		const FVector2D Half(((Each.Footprint.X - 1) * Pitch + Room) * 0.5 + ClearMargin, ((Each.Footprint.Y - 1) * Pitch + Room) * 0.5 + ClearMargin);
		Boxes.Emplace(FVector2D(Center) - Half, FVector2D(Center) + Half);
	}
	for (const FNRGeneratedConnection& Connection : Layout.Connections)
	{
		// From one cell centre to the other, as wide as the corridor: the rooms' boxes cover the ends.
		const FVector2D A(Generator->GetCellCenter(Connection.CellA));
		const FVector2D B(Generator->GetCellCenter(Connection.CellB));
		const double Side = CorridorClearWidth * 0.5 + ClearMargin;
		const FVector2D Across = FMath::Abs(A.X - B.X) > FMath::Abs(A.Y - B.Y) ? FVector2D(0.0, Side) : FVector2D(Side, 0.0);
		Boxes.Emplace(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)) - Across, FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)) + Across);
	}
	return Boxes;
}

bool ANRGroundScatter::TerrainHeight(const FVector2D& At, double MinNormalZ, double& OutHeight) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// Modules standing there are skipped: only the landscape's own surface counts.
	// An object-type multi trace reports every world static hit along the line, top first.
	TArray<FHitResult> Hits;
	World->LineTraceMultiByObjectType(Hits, FVector(At.X, At.Y, 100000.0), FVector(At.X, At.Y, -100000.0),
		FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(SCENE_QUERY_STAT(NRGroundScatterTerrain), false, this));
	for (const FHitResult& Hit : Hits)
	{
		if (Cast<ALandscapeProxy>(Hit.GetActor()))
		{
			if (Hit.ImpactNormal.Z < MinNormalZ)
			{
				return false;
			}
			OutHeight = Hit.ImpactPoint.Z;
			return true;
		}
	}
	return false;
}

UHierarchicalInstancedStaticMeshComponent* ANRGroundScatter::ComponentFor(UStaticMesh* Mesh, const FNRScatterLayer& Layer)
{
	for (UHierarchicalInstancedStaticMeshComponent* Existing : Instances)
	{
		if (Existing && Existing->GetStaticMesh() == Mesh)
		{
			return Existing;
		}
	}
	// Made at run time (and in the editor on construction), never saved: the same seed makes the
	// same cover again.
	UHierarchicalInstancedStaticMeshComponent* Made = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
	Made->SetStaticMesh(Mesh);
	Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Made->SetCanEverAffectNavigation(false);
	Made->SetCastShadow(Layer.bCastShadow);
	if (Layer.CullEnd > 0.0f)
	{
		Made->SetCullDistances(static_cast<int32>(Layer.CullStart), static_cast<int32>(Layer.CullEnd));
	}
	Made->SetupAttachment(GetRootComponent());
	Made->RegisterComponent();
	Instances.Add(Made);
	return Made;
}

void ANRGroundScatter::Scatter(const TArray<FBox2D>& Clear, const TArray<FBox2D>& Keep, const TArray<FBox2D>& Bare)
{
	LayoutKey = CurrentLayoutKey();
	DebugClear = Clear;
	DebugKeep = Keep;
	DebugBare = Bare;
	DebugRaised = RaisedBoxes;
	for (UHierarchicalInstancedStaticMeshComponent* Each : Instances)
	{
		if (Each)
		{
			Each->ClearInstances();
		}
	}

	const FVector Origin = GetActorLocation();
	TMap<UHierarchicalInstancedStaticMeshComponent*, TArray<FTransform>> Batches;
	int32 Kept = 0;
	int32 Cleared = 0;
	for (int32 LayerIndex = 0; LayerIndex < CoverLayers.Num(); ++LayerIndex)
	{
		const FNRScatterLayer& Layer = CoverLayers[LayerIndex];
		TArray<UStaticMesh*> Meshes;
		for (UStaticMesh* Mesh : Layer.Meshes)
		{
			if (Mesh)
			{
				Meshes.Add(Mesh);
			}
		}
		if (Meshes.Num() == 0)
		{
			continue;
		}
		// Every point is drawn whether or not it is cleared, so with one seed the cover outside the
		// modules is the same for every layout (bRandomPerMap gives each map its own seed).
		FRandomStream Stream(HashCombine(CoverSeed(), GetTypeHash(LayerIndex)));
		const FVector2D Area = Layer.AreaHalfExtent.X > 0.0 && Layer.AreaHalfExtent.Y > 0.0 ? Layer.AreaHalfExtent : HalfExtent;
		const int32 Count = FMath::RoundToInt(Layer.PerSquareMetre * (Area.X * 2.0 / 100.0) * (Area.Y * 2.0 / 100.0));
		const FVector2D Grow(Layer.ExtraClearMargin);
		const double MinNormalZ = FMath::Cos(FMath::DegreesToRadians(Layer.MaxSlope));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D At(Origin.X + Stream.FRandRange(-Area.X, Area.X), Origin.Y + Stream.FRandRange(-Area.Y, Area.Y));
			UStaticMesh* Mesh = Meshes[Stream.RandRange(0, Meshes.Num() - 1)];
			const float Scale = Stream.FRandRange(Layer.ScaleRange.X, Layer.ScaleRange.Y);
			const float Sink = Stream.FRandRange(Layer.SinkRange.X, Layer.SinkRange.Y);
			const FRotator Turn(Stream.FRandRange(-Layer.MaxTilt, Layer.MaxTilt), Stream.FRandRange(0.0f, 360.0f), Stream.FRandRange(-Layer.MaxTilt, Layer.MaxTilt));
			const bool bInModule = Clear.ContainsByPredicate([&At, &Grow](const FBox2D& Box) { return FBox2D(Box.Min - Grow, Box.Max + Grow).IsInside(At); });
			const bool bOpenFloor = !Layer.bSkipOpenFloors && Keep.ContainsByPredicate([&At](const FBox2D& Box) { return Box.IsInside(At); });
			// A few tufts stay on the dirt: picked by a hash of the point, so the stream (and the cover
			// everywhere else) does not change.
			const bool bKeepOnDirt = !Layer.bSkipOpenFloors
				&& (HashCombine(HashCombine(CoverSeed(), GetTypeHash(LayerIndex)), GetTypeHash(Index)) % 1000) < DirtCoverKeep * 1000.0f;
			const bool bOnDirt = !bKeepOnDirt && Bare.ContainsByPredicate([&At](const FBox2D& Box) { return Box.IsInside(At); });
			// Mounds in the modules (RaisedGroundTag) carry cover on top, wherever they stand.
			double RaisedZ = 0.0;
			const bool bOnRaised = !Layer.bSkipOpenFloors && !Layer.bFollowTerrain && RaisedHeight(At, RaisedZ);
			if ((bInModule && !bOpenFloor && !bOnRaised) || bOnDirt)
			{
				++Cleared;
				continue;
			}
			FVector Location(At.X, At.Y, bOnRaised ? RaisedZ - Sink : Origin.Z + GroundTop - Sink);
			if (Layer.bFollowTerrain)
			{
				double Height = 0.0;
				if (!TerrainHeight(At, MinNormalZ, Height))
				{
					continue;
				}
				Location.Z = Height - Sink;
			}
			// Outside the outdoor walls the landscape material raises the ground; the cover goes up with it.
			if (!bOnRaised)
			{
				Location.Z += Mask.RaiseAt(At);
			}
			Batches.FindOrAdd(ComponentFor(Mesh, Layer)).Emplace(Turn, Location, FVector(Scale));
			++Kept;
		}
	}
	Kept += PlantBermTrees(Batches);
	for (TPair<UHierarchicalInstancedStaticMeshComponent*, TArray<FTransform>>& Batch : Batches)
	{
		Batch.Key->AddInstances(Batch.Value, false, true);
	}
	UE_LOG(LogNRGroundScatter, Log, TEXT("%s: %d instance(s) of ground cover, %d left out for %d module box(es), %d open floor box(es) kept, %d dirt box(es) bare"),
		*GetName(), Kept, Cleared, Clear.Num(), Keep.Num(), Bare.Num());
}
