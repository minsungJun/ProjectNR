// Copyright Epic Games, Inc. All Rights Reserved.

// Permanent growth rules, no world: level costs 1, 2, 4, 6, 8 and nothing past level 5; a run gives
// 1 + rooms / 3 (+3 when won); gains per level match the spec (5 / 5 / 1 magazine / 4 % x3 / 1 slot);
// the save game survives a round trip through a slot of its own. Automation test
// ProjectNR.Meta.Progression.

#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "NRMetaProgression.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNRMetaProgressionTest, "ProjectNR.Meta.Progression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNRMetaProgressionTest::RunTest(const FString& Parameters)
{
	const UNRMetaProgressionSettings& Settings = *GetDefault<UNRMetaProgressionSettings>();

	// Costs.
	const int32 Expected[] = { 1, 2, 4, 6, 8 };
	int32 Total = 0;
	for (int32 Level = 0; Level < 5; ++Level)
	{
		TestEqual(FString::Printf(TEXT("cost to level %d"), Level + 1), NRMeta::CostOfNextLevel(Settings, Level), Expected[Level]);
		Total += NRMeta::CostOfNextLevel(Settings, Level);
	}
	TestEqual(TEXT("a whole stat costs 21"), Total, 21);
	TestEqual(TEXT("nothing past the max level"), NRMeta::CostOfNextLevel(Settings, 5), 0);

	// Run rewards.
	TestEqual(TEXT("a lost run with no rooms gives 1"), NRMeta::PointsForRun(Settings, 0, false), 1);
	TestEqual(TEXT("5 rooms lost gives 2"), NRMeta::PointsForRun(Settings, 5, false), 2);
	TestEqual(TEXT("10 rooms won gives 1 + 3 + 3"), NRMeta::PointsForRun(Settings, 10, true), 7);

	// Gains.
	TestEqual(TEXT("max health +25 at level 5"), NRMeta::GainAt(Settings, ENRMetaStat::MaxHealth, 5), 25.0f);
	TestEqual(TEXT("max stamina +10 at level 2"), NRMeta::GainAt(Settings, ENRMetaStat::MaxStamina, 2), 10.0f);
	TestEqual(TEXT("ammo +3 magazines at level 3"), NRMeta::GainAt(Settings, ENRMetaStat::AmmoReserve, 3), 3.0f);
	TestTrue(TEXT("drop rate +20% at level 5"), FMath::IsNearlyEqual(NRMeta::GainAt(Settings, ENRMetaStat::ItemDropRate, 5), 0.2f, 1.e-4f));
	TestTrue(TEXT("experience +4% at level 1"), FMath::IsNearlyEqual(NRMeta::GainAt(Settings, ENRMetaStat::ExperienceGain, 1), 0.04f, 1.e-4f));
	TestTrue(TEXT("gold +12% at level 3"), FMath::IsNearlyEqual(NRMeta::GainAt(Settings, ENRMetaStat::GoldGain, 3), 0.12f, 1.e-4f));
	TestEqual(TEXT("inventory +5 slots at level 5"), NRMeta::GainAt(Settings, ENRMetaStat::InventorySlots, 5), 5.0f);
	TestEqual(TEXT("level is clamped to the max"), NRMeta::GainAt(Settings, ENRMetaStat::MaxHealth, 9), 25.0f);
	for (int32 Index = 0; Index < NRMetaStatCount; ++Index)
	{
		const ENRMetaStat Stat = static_cast<ENRMetaStat>(Index);
		TestFalse(FString::Printf(TEXT("stat %d has a name"), Index), NRMeta::GetStatName(Stat).IsEmpty());
		TestFalse(FString::Printf(TEXT("stat %d shows its gain"), Index), NRMeta::FormatGain(Stat, NRMeta::GainAt(Settings, Stat, 2)).IsEmpty());
	}

	// Save round trip, in a slot of its own so the real progress is untouched.
	const FString Slot = TEXT("NRMeta_AutomationTest");
	UNRMetaSave* Out = Cast<UNRMetaSave>(UGameplayStatics::CreateSaveGameObject(UNRMetaSave::StaticClass()));
	Out->Points = 7;
	Out->TotalEarned = 30;
	Out->RunsFinished = 4;
	Out->Levels = { 1, 0, 2, 0, 0, 3, 5 };
	TestTrue(TEXT("save written"), UGameplayStatics::SaveGameToSlot(Out, Slot, 0));
	const UNRMetaSave* In = Cast<UNRMetaSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	TestNotNull(TEXT("save read back"), In);
	if (In)
	{
		TestEqual(TEXT("points survive"), In->Points, 7);
		TestEqual(TEXT("total survives"), In->TotalEarned, 30);
		TestEqual(TEXT("runs survive"), In->RunsFinished, 4);
		TestEqual(TEXT("levels survive"), In->Levels, Out->Levels);
	}
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

#endif
