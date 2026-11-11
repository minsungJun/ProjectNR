// Copyright Epic Games, Inc. All Rights Reserved.

// Shop pricing, no world: ammo costs the base price while the guns are used evenly (or not at
// all) and up to Base * (1 + Markup) for a gun that does all the firing; part levels follow the
// shop's weights (45 / 40 / 15 by default, above the chests' 70 / 25 / 5); part prices stay in
// their level's range; every kind of line has a name. Automation test ProjectNR.Shop.Pricing.

#include "Misc/AutomationTest.h"
#include "NRShop.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNRShopPricingTest, "ProjectNR.Shop.Pricing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNRShopPricingTest::RunTest(const FString& Parameters)
{
	// Ammo: base 10, markup 30%.
	TestEqual(TEXT("ammo: nothing fired yet is the base price"), NRShop::AmmoPrice(10, 0.3f, { 0.0f, 0.0f }, 0), 10);
	TestEqual(TEXT("ammo: an even split is the base price"), NRShop::AmmoPrice(10, 0.3f, { 2.0f, 2.0f }, 0), 10);
	TestEqual(TEXT("ammo: the gun doing all the firing pays the full markup"), NRShop::AmmoPrice(10, 0.3f, { 4.0f, 0.0f }, 0), 13);
	TestEqual(TEXT("ammo: the unused gun stays at the base price"), NRShop::AmmoPrice(10, 0.3f, { 4.0f, 0.0f }, 1), 10);
	TestEqual(TEXT("ammo: three quarters of the firing pays half the markup"), NRShop::AmmoPrice(100, 0.3f, { 3.0f, 1.0f }, 0), 115);
	TestEqual(TEXT("ammo: a single gun has nothing to compare with"), NRShop::AmmoPrice(10, 0.3f, { 5.0f }, 0), 10);

	TStrongObjectPtr<UNRShopData> Data(NewObject<UNRShopData>(GetTransientPackage()));
	TestEqual(TEXT("three part price ranges by default"), Data->PartPrice.Num(), 3);

	// Part levels by the default weights.
	FRandomStream Stream(1234);
	int32 Counts[3] = { 0, 0, 0 };
	constexpr int32 Rolls = 20000;
	for (int32 Index = 0; Index < Rolls; ++Index)
	{
		++Counts[static_cast<int32>(NRShop::RollPartLevel(*Data, Stream))];
	}
	const float Total = Data->PartLevelIWeight + Data->PartLevelIIWeight + Data->PartLevelIIIWeight;
	const float Expected[3] = { Data->PartLevelIWeight / Total, Data->PartLevelIIWeight / Total, Data->PartLevelIIIWeight / Total };
	for (int32 Level = 0; Level < 3; ++Level)
	{
		const float Share = static_cast<float>(Counts[Level]) / Rolls;
		AddInfo(FString::Printf(TEXT("level %d: %.1f%% (weight %.1f%%)"), Level + 1, Share * 100.0f, Expected[Level] * 100.0f));
		TestTrue(FString::Printf(TEXT("level %d share near its weight"), Level + 1), FMath::Abs(Share - Expected[Level]) < 0.02f);
	}

	// Part prices stay in range.
	for (int32 Level = 0; Level < 3; ++Level)
	{
		const FNRShopPriceRange& Range = Data->PartPrice[Level];
		bool bInRange = true;
		for (int32 Index = 0; Index < 500; ++Index)
		{
			const int32 Price = NRShop::RollPartPrice(*Data, static_cast<ENRWeaponPartLevel>(Level), Stream);
			bInRange &= Price >= Range.Min && Price <= Range.Max;
		}
		TestTrue(FString::Printf(TEXT("level %d prices within %d-%d"), Level + 1, Range.Min, Range.Max), bInRange);
	}
	TestTrue(TEXT("higher level parts cost more"), Data->PartPrice[0].Max < Data->PartPrice[1].Min && Data->PartPrice[1].Max < Data->PartPrice[2].Min);

	// No weights at all: level I.
	Data->PartLevelIWeight = Data->PartLevelIIWeight = Data->PartLevelIIIWeight = 0.0f;
	TestEqual(TEXT("all weights 0 gives level I"), NRShop::RollPartLevel(*Data, Stream), ENRWeaponPartLevel::I);

	// Names.
	FNRShopOffer Offer;
	for (ENRItemType Type : { ENRItemType::Potion, ENRItemType::Shield, ENRItemType::Ammo, ENRItemType::WeaponPart })
	{
		Offer.ItemType = Type;
		TestFalse(FString::Printf(TEXT("line %d has a name"), static_cast<int32>(Type)), NRShop::DescribeOffer(Offer).IsEmpty());
	}
	return true;
}

#endif
