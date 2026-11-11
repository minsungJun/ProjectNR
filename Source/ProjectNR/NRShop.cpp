// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRShop.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NRConsumableComponent.h"
#include "NRHUD.h"
#include "NRLootTable.h"
#include "NRRunSubsystem.h"
#include "NRWeaponComponent.h"
#include "NRWeaponPartComponent.h"
#include "NRWeaponPartData.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRShop, Log, All);

namespace NRShopShape
{
	// A counter-sized box: depth along X (the front faces +X), width along Y, height.
	constexpr float Depth = 80.0f;
	constexpr float Width = 160.0f;
	constexpr float Height = 110.0f;
}

// --- Data ------------------------------------------------------------------------

UNRShopData::UNRShopData()
{
	PartPrice = { { 30, 45 }, { 55, 80 }, { 95, 130 } };
}

// --- Pricing ---------------------------------------------------------------------

int32 NRShop::AmmoPrice(int32 Base, float Markup, const TArray<float>& Fired, int32 Index)
{
	float Total = 0.0f;
	for (float Each : Fired)
	{
		Total += FMath::Max(Each, 0.0f);
	}
	const int32 Guns = Fired.Num();
	if (Base <= 0 || Guns < 2 || Total <= 0.0f || !Fired.IsValidIndex(Index))
	{
		return FMath::Max(Base, 0);
	}
	// 0 at an even share, 1 when this gun did all the firing.
	const float Even = 1.0f / Guns;
	const float Share = FMath::Max(Fired[Index], 0.0f) / Total;
	const float Over = FMath::Clamp((Share - Even) / (1.0f - Even), 0.0f, 1.0f);
	return FMath::RoundToInt32(Base * (1.0f + FMath::Max(Markup, 0.0f) * Over));
}

ENRWeaponPartLevel NRShop::RollPartLevel(const UNRShopData& Data, FRandomStream& Stream)
{
	const float Weights[] = { FMath::Max(Data.PartLevelIWeight, 0.0f), FMath::Max(Data.PartLevelIIWeight, 0.0f), FMath::Max(Data.PartLevelIIIWeight, 0.0f) };
	const float Total = Weights[0] + Weights[1] + Weights[2];
	if (Total <= 0.0f)
	{
		return ENRWeaponPartLevel::I;
	}
	float Roll = Stream.FRand() * Total;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Weights[Index] > 0.0f && Roll < Weights[Index])
		{
			return static_cast<ENRWeaponPartLevel>(Index);
		}
		Roll -= Weights[Index];
	}
	return Weights[2] > 0.0f ? ENRWeaponPartLevel::III : Weights[1] > 0.0f ? ENRWeaponPartLevel::II : ENRWeaponPartLevel::I;
}

int32 NRShop::RollPartPrice(const UNRShopData& Data, ENRWeaponPartLevel Level, FRandomStream& Stream)
{
	const int32 Index = static_cast<int32>(Level);
	if (!Data.PartPrice.IsValidIndex(Index))
	{
		return 0;
	}
	const FNRShopPriceRange& Range = Data.PartPrice[Index];
	return Range.Max > Range.Min ? Stream.RandRange(Range.Min, Range.Max) : FMath::Max(Range.Min, 0);
}

FText NRShop::DescribeOffer(const FNRShopOffer& Offer)
{
	switch (Offer.ItemType)
	{
	case ENRItemType::Potion:
		return FText::Format(NSLOCTEXT("NRShop", "Potion", "HEALING POTION x{0}"), FMath::RoundToInt32(Offer.Amount));
	case ENRItemType::Shield:
		return FText::Format(NSLOCTEXT("NRShop", "Shield", "ARMOR PLATE x{0}"), FMath::RoundToInt32(Offer.Amount));
	case ENRItemType::Ammo:
	{
		const FText Gun = Offer.WeaponId == ENRWeaponId::Shotgun ? NSLOCTEXT("NRShop", "Shotgun", "SHOTGUN")
			: Offer.WeaponId == ENRWeaponId::Melee ? NSLOCTEXT("NRShop", "Melee", "MELEE") : NSLOCTEXT("NRShop", "Rifle", "RIFLE");
		return FText::Format(NSLOCTEXT("NRShop", "Ammo", "{0} AMMO x{1} MAG"), Gun, FText::AsNumber(Offer.Amount));
	}
	case ENRItemType::WeaponPart:
		if (Offer.Part)
		{
			const FText Name = Offer.Part->DisplayName.IsEmpty() ? FText::FromString(Offer.Part->GetName()) : Offer.Part->DisplayName;
			return FText::Format(NSLOCTEXT("NRShop", "Part", "{0} {1}"), Name, UNRWeaponPartData::GetLevelText(Offer.PartLevel));
		}
		return NSLOCTEXT("NRShop", "NoPart", "PART");
	default:
		return FText::FromString(NRItems::GetDisplayName(Offer.ItemType));
	}
}

// --- Shop ------------------------------------------------------------------------

ANRShop::ANRShop()
{
	PrimaryActorTick.bCanEverTick = false;

	using namespace NRShopShape;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	TintMaterial = MaterialFinder.Object;

	// Origin on the floor; the box blocks the player and is what the interaction scan finds.
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetStaticMesh(CubeFinder.Object);
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, Height * 0.5f));
	Body->SetRelativeScale3D(FVector(Depth, Width, Height) / 100.0f);
	Body->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	Sign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Sign"));
	Sign->SetupAttachment(Root);
	Sign->SetRelativeLocation(FVector(Depth * 0.5f + 1.0f, 0.0f, Height + 25.0f));
	Sign->SetHorizontalAlignment(EHTA_Center);
	Sign->SetVerticalAlignment(EVRTA_TextCenter);
	Sign->SetWorldSize(36.0f);
	Sign->SetTextRenderColor(FColor(255, 200, 60));
	Sign->SetText(NSLOCTEXT("NRShop", "Sign", "SHOP"));

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Root);
	Glow->SetRelativeLocation(FVector(Depth + 40.0f, 0.0f, Height + 40.0f));
	Glow->SetIntensity(900.0f);
	Glow->SetAttenuationRadius(350.0f);
	Glow->SetCastShadows(false);
	Glow->SetLightColor(BodyColor);

	Prompt = NSLOCTEXT("NRShop", "Prompt", "SHOP");
}

void ANRShop::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (Body && TintMaterial)
	{
		if (UMaterialInstanceDynamic* Material = Body->CreateDynamicMaterialInstance(0, TintMaterial))
		{
			Material->SetVectorParameterValue(TEXT("Color"), BodyColor);
		}
	}
	if (Glow)
	{
		Glow->SetLightColor(BodyColor);
	}
}

void ANRShop::BeginPlay()
{
	Super::BeginPlay();
	if (bStockOnBeginPlay)
	{
		StockIfNeeded();
	}
}

const UNRShopData& ANRShop::GetData() const
{
	return ShopData ? *ShopData : *GetDefault<UNRShopData>();
}

FVector ANRShop::GetInteractLocation() const
{
	const FVector Location = GetActorLocation();
	const float Top = Body && Body->GetStaticMesh() ? Body->Bounds.GetBox().Max.Z : Location.Z + NRShopShape::Height;
	return FVector(Location.X, Location.Y, Top + 60.0f);
}

void ANRShop::StockIfNeeded()
{
	if (!bStocked)
	{
		Restock();
	}
}

void ANRShop::Restock()
{
	const UNRShopData& Data = GetData();
	FRandomStream Stream(Seed != 0 ? Seed : FMath::Rand());
	PartStock.Reset();
	for (int32 Index = 0; Index < Data.PartOffers; ++Index)
	{
		// UNRLootTable::PickPart uses FMath::Rand; fine for which part, the level and price follow the stream.
		UNRWeaponPartData* Part = UNRLootTable::PickPart(Data.PartPool);
		if (!Part)
		{
			UE_LOG(LogNRShop, Warning, TEXT("%s: no weapon part to sell"), *GetName());
			break;
		}
		FNRShopOffer Offer;
		Offer.ItemType = ENRItemType::WeaponPart;
		Offer.Part = Part;
		Offer.PartLevel = NRShop::RollPartLevel(Data, Stream);
		Offer.Price = NRShop::RollPartPrice(Data, Offer.PartLevel, Stream);
		PartStock.Add(Offer);
	}
	bStocked = true;

	FString Parts;
	for (const FNRShopOffer& Offer : PartStock)
	{
		Parts += FString::Printf(TEXT("%s%s (%d)"), Parts.IsEmpty() ? TEXT("") : TEXT(", "), *NRShop::DescribeOffer(Offer).ToString(), Offer.Price);
	}
	UE_LOG(LogNRShop, Log, TEXT("%s stocked: %s"), *GetName(), Parts.IsEmpty() ? TEXT("no parts") : *Parts);
}

TArray<FNRShopOffer> ANRShop::GetOffers(const APawn* Buyer) const
{
	const UNRShopData& Data = GetData();
	TArray<FNRShopOffer> Offers;

	FNRShopOffer Potion;
	Potion.ItemType = ENRItemType::Potion;
	Potion.Price = Data.PotionPrice;
	Offers.Add(Potion);

	FNRShopOffer Shield;
	Shield.ItemType = ENRItemType::Shield;
	Shield.Price = Data.ShieldPrice;
	Offers.Add(Shield);

	// One line per gun the buyer carries, priced from how much each has been fired.
	if (const UNRWeaponComponent* Weapons = Buyer ? Buyer->FindComponentByClass<UNRWeaponComponent>() : nullptr)
	{
		const TArray<ENRWeaponId> Guns = Weapons->GetRangedWeaponIds();
		TArray<float> Fired;
		for (ENRWeaponId Gun : Guns)
		{
			Fired.Add(Weapons->GetMagazinesFired(Gun));
		}
		for (int32 Index = 0; Index < Guns.Num(); ++Index)
		{
			FNRShopOffer Ammo;
			Ammo.ItemType = ENRItemType::Ammo;
			Ammo.WeaponId = Guns[Index];
			Ammo.Amount = Data.AmmoMagazinesPerBuy;
			Ammo.Price = NRShop::AmmoPrice(Data.AmmoBasePrice, Data.AmmoUsageMarkup, Fired, Index);
			Offers.Add(Ammo);
		}
	}

	Offers.Append(PartStock);
	return Offers;
}

ENRShopResult ANRShop::Buy(APawn* Buyer, int32 Index)
{
	StockIfNeeded();
	UNRRunSubsystem* Run = GetWorld() ? GetWorld()->GetSubsystem<UNRRunSubsystem>() : nullptr;
	const TArray<FNRShopOffer> Offers = GetOffers(Buyer);
	if (!Buyer || !Run || !Offers.IsValidIndex(Index))
	{
		return ENRShopResult::Invalid;
	}
	const FNRShopOffer& Offer = Offers[Index];
	if (Offer.bSoldOut)
	{
		return ENRShopResult::SoldOut;
	}
	if (Run->GetMoney() < Offer.Price)
	{
		return ENRShopResult::NotEnoughGold;
	}

	bool bTaken = false;
	switch (Offer.ItemType)
	{
	case ENRItemType::Potion:
	case ENRItemType::Shield:
		if (UNRConsumableComponent* Consumables = Buyer->FindComponentByClass<UNRConsumableComponent>())
		{
			bTaken = Consumables->AddItem(Offer.ItemType, FMath::RoundToInt32(Offer.Amount));
		}
		break;
	case ENRItemType::Ammo:
		if (UNRWeaponComponent* Weapons = Buyer->FindComponentByClass<UNRWeaponComponent>())
		{
			bTaken = Weapons->AddReserveMagazinesFor(Offer.WeaponId, Offer.Amount) > 0;
		}
		break;
	case ENRItemType::WeaponPart:
		if (UNRWeaponPartComponent* Parts = Buyer->FindComponentByClass<UNRWeaponPartComponent>())
		{
			bTaken = Offer.Part && Parts->AddPart(Offer.Part, Offer.PartLevel).IsValid();
		}
		break;
	default:
		break;
	}
	if (!bTaken)
	{
		return ENRShopResult::CannotCarry;
	}

	Run->AddMoney(-Offer.Price);
	if (Offer.ItemType == ENRItemType::WeaponPart)
	{
		// Parts come after the fixed lines, in PartStock's order.
		const int32 PartIndex = Index - (Offers.Num() - PartStock.Num());
		if (PartStock.IsValidIndex(PartIndex))
		{
			PartStock[PartIndex].bSoldOut = true;
		}
	}
	UE_LOG(LogNRShop, Log, TEXT("%s bought %s for %d gold, %d left"), *GetNameSafe(Buyer), *NRShop::DescribeOffer(Offer).ToString(), Offer.Price, Run->GetMoney());
	return ENRShopResult::Bought;
}

void ANRShop::Interact(APawn* User)
{
	StockIfNeeded();
	APlayerController* PC = User ? Cast<APlayerController>(User->GetController()) : nullptr;
	if (ANRHUD* HUD = PC ? PC->GetHUD<ANRHUD>() : nullptr)
	{
		HUD->OpenShopMenu(this);
	}
	else
	{
		UE_LOG(LogNRShop, Warning, TEXT("%s: no ANRHUD to show the shop on"), *GetName());
	}
}

FText ANRShop::DescribeResult(ENRShopResult Result)
{
	switch (Result)
	{
	case ENRShopResult::Bought: return NSLOCTEXT("NRShop", "Bought", "Bought.");
	case ENRShopResult::NotEnoughGold: return NSLOCTEXT("NRShop", "NotEnoughGold", "Not enough gold.");
	case ENRShopResult::SoldOut: return NSLOCTEXT("NRShop", "SoldOut", "Sold out.");
	case ENRShopResult::CannotCarry: return NSLOCTEXT("NRShop", "CannotCarry", "You cannot carry any more of that.");
	default: return NSLOCTEXT("NRShop", "Invalid", "Cannot buy that.");
	}
}

// --- Console ---------------------------------------------------------------------

namespace
{
	APawn* ConsolePlayer(UWorld* World)
	{
		return World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	}

	ANRShop* NearestShop(UWorld* World, const FVector& From)
	{
		ANRShop* Best = nullptr;
		double BestDistance = TNumericLimits<double>::Max();
		for (TActorIterator<ANRShop> It(World); It; ++It)
		{
			const double Distance = FVector::DistSquared(It->GetActorLocation(), From);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = *It;
			}
		}
		return Best;
	}

	void LogOffers(ANRShop* Shop, APawn* Player)
	{
		const UNRRunSubsystem* Run = Shop->GetWorld()->GetSubsystem<UNRRunSubsystem>();
		UE_LOG(LogNRShop, Log, TEXT("%s, %d gold:"), *Shop->GetName(), Run ? Run->GetMoney() : 0);
		const TArray<FNRShopOffer> Offers = Shop->GetOffers(Player);
		for (int32 Index = 0; Index < Offers.Num(); ++Index)
		{
			UE_LOG(LogNRShop, Log, TEXT("  %d  %s  %d gold%s"), Index, *NRShop::DescribeOffer(Offers[Index]).ToString(), Offers[Index].Price,
				Offers[Index].bSoldOut ? TEXT("  (sold out)") : TEXT(""));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRShopSpawnCommand(TEXT("NR.Shop.Spawn"),
	TEXT("Puts a shop box 2.5 m in front of the player, facing them."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APawn* Player = ConsolePlayer(World);
		if (!Player)
		{
			return;
		}
		const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
		FVector At = Player->GetActorLocation() + Forward * 250.0f;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, At + FVector(0, 0, 200), At - FVector(0, 0, 1000), ECC_WorldStatic))
		{
			At = Hit.ImpactPoint;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ANRShop* Shop = World->SpawnActor<ANRShop>(At, (-Forward).Rotation(), Params))
		{
			UE_LOG(LogNRShop, Log, TEXT("Spawned %s at %s"), *Shop->GetName(), *At.ToCompactString());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GNRShopListCommand(TEXT("NR.Shop.List"),
	TEXT("Logs the nearest shop's list as the player sees it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APawn* Player = ConsolePlayer(World);
		if (ANRShop* Shop = Player ? NearestShop(World, Player->GetActorLocation()) : nullptr)
		{
			Shop->Buy(nullptr, -1); // stocks it if it has not been opened yet
			LogOffers(Shop, Player);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GNRShopBuyCommand(TEXT("NR.Shop.Buy"),
	TEXT("NR.Shop.Buy <line> - buys a line of the nearest shop's list for the player."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APawn* Player = ConsolePlayer(World);
		ANRShop* Shop = Player ? NearestShop(World, Player->GetActorLocation()) : nullptr;
		if (!Shop || Args.Num() < 1)
		{
			return;
		}
		const ENRShopResult Result = Shop->Buy(Player, FCString::Atoi(*Args[0]));
		UE_LOG(LogNRShop, Log, TEXT("NR.Shop.Buy %s: %s"), *Args[0], *ANRShop::DescribeResult(Result).ToString());
	}));

static FAutoConsoleCommandWithWorldAndArgs GNRShopRestockCommand(TEXT("NR.Shop.Restock"),
	TEXT("Rolls the nearest shop's parts again."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APawn* Player = ConsolePlayer(World);
		if (ANRShop* Shop = Player ? NearestShop(World, Player->GetActorLocation()) : nullptr)
		{
			Shop->Restock();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GNRShopGoldCommand(TEXT("NR.Shop.Gold"),
	TEXT("NR.Shop.Gold <amount> - adds (or, negative, takes) run gold, for trying the shop."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (UNRRunSubsystem* Run = World ? World->GetSubsystem<UNRRunSubsystem>() : nullptr)
		{
			Run->AddMoney(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 100);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GNRShopOpenCommand(TEXT("NR.Shop.Open"),
	TEXT("Opens the nearest shop's screen, as the interact key does next to it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APawn* Player = ConsolePlayer(World);
		if (ANRShop* Shop = Player ? NearestShop(World, Player->GetActorLocation()) : nullptr)
		{
			Shop->Interact(Player);
		}
	}));
