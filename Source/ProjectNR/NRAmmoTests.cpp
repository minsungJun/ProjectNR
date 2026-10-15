// Copyright Epic Games, Inc. All Rights Reserved.

// Checks for NRAmmo: reloading from a finite reserve and topping the reserve up to its cap.
// Automation test ProjectNR.Ammo.Math.

#include "NRAmmo.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNRAmmoMathTest, "ProjectNR.Ammo.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNRAmmoMathTest::RunTest(const FString& Parameters)
{
	// A reload fills the magazine from the reserve.
	{
		int32 Magazine = 5, Reserve = 60;
		TestEqual(TEXT("reload moves what the magazine lacks"), NRAmmo::Reload(Magazine, Reserve, 30), 25);
		TestEqual(TEXT("magazine full"), Magazine, 30);
		TestEqual(TEXT("reserve down by the same"), Reserve, 35);
	}
	// Not enough in reserve: what there is.
	{
		int32 Magazine = 0, Reserve = 12;
		TestEqual(TEXT("short reserve: all of it"), NRAmmo::Reload(Magazine, Reserve, 30), 12);
		TestEqual(TEXT("short reserve: magazine part full"), Magazine, 12);
		TestEqual(TEXT("short reserve: reserve empty"), Reserve, 0);
	}
	// Nothing to do.
	{
		int32 Magazine = 30, Reserve = 40;
		TestEqual(TEXT("full magazine: nothing moved"), NRAmmo::Reload(Magazine, Reserve, 30), 0);
		TestEqual(TEXT("full magazine: reserve kept"), Reserve, 40);
		int32 Empty = 3, None = 0;
		TestEqual(TEXT("empty reserve: nothing moved"), NRAmmo::Reload(Empty, None, 30), 0);
		TestFalse(TEXT("empty reserve: cannot reload"), NRAmmo::CanReload(3, 0, 30));
		TestFalse(TEXT("full magazine: cannot reload"), NRAmmo::CanReload(30, 10, 30));
		TestTrue(TEXT("room and reserve: can reload"), NRAmmo::CanReload(3, 10, 30));
	}
	// A magazine shrunk by a part: over-full stays, nothing moves the wrong way.
	{
		int32 Magazine = 40, Reserve = 10;
		TestEqual(TEXT("over-full magazine: nothing moved"), NRAmmo::Reload(Magazine, Reserve, 30), 0);
		TestEqual(TEXT("over-full magazine: reserve kept"), Reserve, 10);
	}
	// Topping up the reserve.
	{
		int32 Reserve = 150;
		TestEqual(TEXT("pickup: up to the cap"), NRAmmo::AddToReserve(Reserve, 40, 180), 30);
		TestEqual(TEXT("pickup: reserve at the cap"), Reserve, 180);
		TestEqual(TEXT("pickup at the cap: nothing taken"), NRAmmo::AddToReserve(Reserve, 40, 180), 0);
		int32 Low = 0;
		TestEqual(TEXT("pickup into an empty reserve"), NRAmmo::AddToReserve(Low, 30, 180), 30);
	}
	TestTrue(TEXT("out of ammo: both empty"), NRAmmo::IsOutOfAmmo(0, 0));
	TestFalse(TEXT("not out: reserve left"), NRAmmo::IsOutOfAmmo(0, 5));
	TestFalse(TEXT("not out: rounds in the magazine"), NRAmmo::IsOutOfAmmo(2, 0));
	return true;
}

#endif
