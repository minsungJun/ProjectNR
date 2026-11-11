// Copyright Epic Games, Inc. All Rights Reserved.

// Checks for enemy adaptation without a world: what one weapon teaches each kind of enemy,
// retention when the player switches, weapon resistance and its cap, the stat bonuses, health
// and damage from level and stage, and the spawn mix shift in FNRSpawnPicker.
// Automation tests ProjectNR.Adaptation.Model and ProjectNR.Adaptation.Spawns.

#include "Misc/AutomationTest.h"
#include "NRAdaptationConfig.h"
#include "NRSpawnPicker.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace NRAdaptationTests
{
	/** A room fought with one weapon only. */
	FNRRoomCombatRecord Room(EAdaptationWeapon Weapon, int32 Kills = 5)
	{
		FNRRoomCombatRecord Record;
		const int32 Index = NRAdaptWeaponIndex(Weapon);
		Record.Damage[Index] = 100.0f * Kills;
		Record.Kills[Index] = Kills;
		return Record;
	}

	bool Near(float A, float B) { return FMath::IsNearlyEqual(A, B, 0.001f); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNRAdaptationModelTest, "ProjectNR.Adaptation.Model",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNRAdaptationModelTest::RunTest(const FString& Parameters)
{
	using namespace NRAdaptationTests;
	const UNRAdaptationConfig& Config = *GetDefault<UNRAdaptationConfig>();

	// One rifle room: dodge++ strafe++ caution+ approach+ at the learn rate, per kind of enemy.
	FNRAdaptationModel Model;
	Model.CommitRoom(Room(EAdaptationWeapon::Rifle), Config);
	const FEnemyAdaptationProfile& Melee = Model.GetProfile(ENREnemyFamily::Melee);
	const FEnemyAdaptationProfile& Ranged = Model.GetProfile(ENREnemyFamily::Ranged);
	const FEnemyAdaptationProfile& Boss = Model.GetProfile(ENREnemyFamily::Boss);
	TestTrue(TEXT("Rifle: crawler dodge 0.5"), Near(Melee.Dodge, 0.5f));
	TestTrue(TEXT("Rifle: crawler strafe 0.5"), Near(Melee.Strafe, 0.5f));
	TestTrue(TEXT("Rifle: crawler caution 0.2 (0.5 x 0.5 x 0.8)"), Near(Melee.Caution, 0.2f));
	TestTrue(TEXT("Rifle: crawler approach 0.3 (0.5 x 0.5 x 1.2)"), Near(Melee.Approach, 0.3f));
	TestTrue(TEXT("Rifle: no retreat"), Near(Melee.Retreat, 0.0f));
	TestTrue(TEXT("Rifle: shooters dodge more (0.6)"), Near(Ranged.Dodge, 0.6f));
	TestTrue(TEXT("Rifle: the boss half as much (0.25)"), Near(Boss.Dodge, 0.25f));
	TestTrue(TEXT("Rifle: weapon level 0.5"), Near(Model.GetWeaponLevel(EAdaptationWeapon::Rifle), 0.5f));
	TestEqual(TEXT("Stage counts the room"), Model.GetStage(), 1);
	TestTrue(TEXT("Rifle shifts spawns to close-in enemies"), Model.GetRangedBias(Config) < -0.4f);

	// Two more rifle rooms: the weapon level saturates, resistance stops at 20%.
	Model.CommitRoom(Room(EAdaptationWeapon::Rifle), Config);
	Model.CommitRoom(Room(EAdaptationWeapon::Rifle), Config);
	TestTrue(TEXT("Rifle level after 3 rooms is 1"), Near(Model.GetWeaponLevel(EAdaptationWeapon::Rifle), 1.0f));
	TestTrue(TEXT("Axes stay within 1"), Model.GetProfile(ENREnemyFamily::Ranged).Dodge <= 1.0f);
	FEnemyCombatModifiers Mods = Model.BuildModifiers(ENREnemyFamily::Melee, 1, Config);
	TestTrue(TEXT("Rifle resistance 20%"), Near(Mods.RifleResistance, 0.2f));
	TestTrue(TEXT("No shotgun resistance"), Near(Mods.ShotgunResistance, 0.0f));
	TestTrue(TEXT("Rifle: +15% speed"), Near(Mods.MoveSpeedMultiplier, 1.15f));
	TestTrue(TEXT("Rifle: +10% attack rate"), Near(Mods.AttackSpeedMultiplier, 1.1f));
	TestTrue(TEXT("Rifle: no regeneration"), Near(Mods.RegenPerSecond, 0.0f));
	TestTrue(TEXT("Stage 3 at level 1: health x1.15"), Near(Mods.HealthMultiplier, 1.15f));
	TestTrue(TEXT("Stage 3 at level 1: damage x1.09"), Near(Mods.DamageMultiplier, 1.09f));
	const FEnemyCombatModifiers Level5 = Model.BuildModifiers(ENREnemyFamily::Melee, 5, Config);
	TestTrue(TEXT("Level 5, stage 3: health x1.39"), Near(Level5.HealthMultiplier, 1.39f));
	TestTrue(TEXT("Level 5, stage 3: damage x1.25"), Near(Level5.DamageMultiplier, 1.25f));

	// Switch to the shotgun: the rifle fades by retention, the shotgun takes over.
	const float DodgeBefore = Model.GetProfile(ENREnemyFamily::Melee).Dodge;
	Model.CommitRoom(Room(EAdaptationWeapon::Shotgun), Config);
	TestTrue(TEXT("Switch: rifle dodge fades to 70%"), Near(Model.GetProfile(ENREnemyFamily::Melee).Dodge, DodgeBefore * 0.7f));
	TestTrue(TEXT("Switch: rifle level fades to 0.7"), Near(Model.GetWeaponLevel(EAdaptationWeapon::Rifle), 0.7f));
	TestTrue(TEXT("Switch: shotgun level 0.5"), Near(Model.GetWeaponLevel(EAdaptationWeapon::Shotgun), 0.5f));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Model.CommitRoom(Room(EAdaptationWeapon::Shotgun), Config);
	}
	const FEnemyAdaptationProfile& Shotgunned = Model.GetProfile(ENREnemyFamily::Melee);
	TestTrue(TEXT("Shotgun: crawlers stop approaching"), Shotgunned.Approach < -0.5f);
	TestTrue(TEXT("Shotgun: crawlers retreat"), Shotgunned.Retreat > 0.5f);
	TestTrue(TEXT("Shotgun shifts spawns to shooters"), Model.GetRangedBias(Config) > 0.5f);
	Mods = Model.BuildModifiers(ENREnemyFamily::Melee, 1, Config);
	TestTrue(TEXT("Shotgun resistance builds up"), Mods.ShotgunResistance > 0.15f);
	TestTrue(TEXT("Rifle resistance fades"), Mods.RifleResistance < 0.05f);
	TestTrue(TEXT("Shotgun: regeneration"), Mods.RegenPerSecond > 0.01f);

	// Melee: retreat+, strafe+, approach-; shooters.
	FNRAdaptationModel MeleeModel;
	MeleeModel.CommitRoom(Room(EAdaptationWeapon::Melee), Config);
	const FEnemyAdaptationProfile& Axed = MeleeModel.GetProfile(ENREnemyFamily::Melee);
	TestTrue(TEXT("Melee: retreat up"), Axed.Retreat > 0.0f);
	TestTrue(TEXT("Melee: strafe up"), Axed.Strafe > 0.0f);
	TestTrue(TEXT("Melee: approach down"), Axed.Approach < 0.0f);
	TestTrue(TEXT("Melee shifts spawns to shooters"), MeleeModel.GetRangedBias(Config) > 0.0f);
	TestTrue(TEXT("Melee: attack rate"), MeleeModel.BuildModifiers(ENREnemyFamily::Melee, 1, Config).AttackSpeedMultiplier > 1.0f);

	// Mixed room: damage share splits the push; two kills only count half for the weapon level.
	FNRAdaptationModel Mixed;
	FNRRoomCombatRecord Record;
	Record.Damage[0] = 300.0f;
	Record.Damage[1] = 100.0f;
	Record.Kills[0] = 2;
	Mixed.CommitRoom(Record, Config);
	TestTrue(TEXT("Mixed: dodge 0.75 x 0.5"), Near(Mixed.GetProfile(ENREnemyFamily::Melee).Dodge, 0.375f));
	TestTrue(TEXT("Mixed: 2 of 4 kills count half"), Near(Mixed.GetWeaponLevel(EAdaptationWeapon::Rifle), 0.25f));

	// No data: the stage still counts, nothing is learned.
	FNRAdaptationModel Empty;
	Empty.CommitRoom(FNRRoomCombatRecord(), Config);
	TestEqual(TEXT("Empty room: stage 1"), Empty.GetStage(), 1);
	TestEqual(TEXT("Empty room: nothing learned"), Empty.GetRoomsLearned(), 0);

	// Only magazines (all missed): still tells which weapon was used.
	FNRAdaptationModel Missed;
	FNRRoomCombatRecord Misses;
	Misses.Magazines[1] = 2.0f;
	Missed.CommitRoom(Misses, Config);
	TestTrue(TEXT("Misses with the shotgun teach retreat"), Missed.GetProfile(ENREnemyFamily::Melee).Retreat > 0.0f);
	TestTrue(TEXT("Misses give no resistance"), Near(Missed.GetWeaponLevel(EAdaptationWeapon::Shotgun), 0.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNRAdaptationSpawnsTest, "ProjectNR.Adaptation.Spawns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNRAdaptationSpawnsTest::RunTest(const FString& Parameters)
{
	using FPicker = FNRSpawnPicker;
	auto MakePoints = [](int32 Count, ENRSpawnKind Kind)
	{
		TArray<FPicker::FCandidate> Points;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FPicker::FCandidate& Point = Points.AddDefaulted_GetRef();
			Point.Location = FVector(400.0f * Index, 0.0f, 0.0f);
			Point.Kind = Kind;
		}
		return Points;
	};
	auto CountRanged = [](const TArray<FPicker::FPick>& Picks)
	{
		return Picks.FilterByPredicate([](const FPicker::FPick& Pick) { return Pick.Kind == ENRSpawnKind::Ranged; }).Num();
	};

	FPicker::FParams Params;
	Params.Rule.MinUseRatio = 0.8f;
	Params.Rule.MaxUseRatio = 0.8f;
	Params.Rule.MaxRanged = 2;

	// No bias: exactly the draws from before adaptation existed.
	const TArray<FPicker::FCandidate> Mixed = [&]()
	{
		TArray<FPicker::FCandidate> Points = MakePoints(10, ENRSpawnKind::Any);
		Points.Append(MakePoints(4, ENRSpawnKind::Melee));
		return Points;
	}();
	int32 BaseRanged = 0;
	int32 ShooterRanged = 0;
	int32 CloseRanged = 0;
	for (int32 Seed = 0; Seed < 200; ++Seed)
	{
		FRandomStream A(Seed);
		BaseRanged += CountRanged(FPicker::Pick(Mixed, Params, A));
		FPicker::FParams More = Params;
		More.RangedBias = 1.0f;
		FRandomStream B(Seed);
		ShooterRanged += CountRanged(FPicker::Pick(Mixed, More, B));
		FPicker::FParams Fewer = Params;
		Fewer.RangedBias = -1.0f;
		FRandomStream C(Seed);
		CloseRanged += CountRanged(FPicker::Pick(Mixed, Fewer, C));
	}
	AddInfo(FString::Printf(TEXT("Ranged over 200 rooms: base %d, +1 %d, -1 %d"), BaseRanged, ShooterRanged, CloseRanged));
	TestTrue(TEXT("Bias +1 gives more shooters"), ShooterRanged > BaseRanged + 200);
	TestTrue(TEXT("Bias -1 gives fewer shooters"), CloseRanged < BaseRanged);

	// Bias +1 lifts the cap to 4 and can turn melee points ranged.
	FPicker::FParams More = Params;
	More.RangedBias = 1.0f;
	int32 MaxSeen = 0;
	int32 Converted = 0;
	for (int32 Seed = 0; Seed < 200; ++Seed)
	{
		FRandomStream Random(Seed);
		const int32 Count = CountRanged(FPicker::Pick(MakePoints(10, ENRSpawnKind::Melee), More, Random));
		MaxSeen = FMath::Max(MaxSeen, Count);
		Converted += Count;
	}
	TestTrue(TEXT("Melee points become shooters at +1"), Converted > 0);
	TestTrue(TEXT("Never past the shifted cap of 4"), MaxSeen <= 4);

	// Bias -1 turns some Ranged points close-in; the cap drops to 0.
	FPicker::FParams Fewer = Params;
	Fewer.RangedBias = -1.0f;
	FRandomStream Random(7);
	TestEqual(TEXT("Cap 2 - 2 = 0: no shooters at -1"), CountRanged(FPicker::Pick(MakePoints(10, ENRSpawnKind::Ranged), Fewer, Random)), 0);

	FPicker::FParams Half = Params;
	Half.RangedBias = -0.5f;
	Half.Rule.MaxRanged = 10;
	int32 HalfRanged = 0;
	for (int32 Seed = 0; Seed < 100; ++Seed)
	{
		FRandomStream Stream(Seed);
		HalfRanged += CountRanged(FPicker::Pick(MakePoints(10, ENRSpawnKind::Ranged), Half, Stream));
	}
	TestTrue(TEXT("-0.5 turns some Ranged points close-in"), HalfRanged < 800);
	return true;
}

#endif
