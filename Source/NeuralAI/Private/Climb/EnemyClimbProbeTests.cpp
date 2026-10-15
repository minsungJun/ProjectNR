// Neural Enemy AI prototype module.

// Checks for the pure part of FEnemyClimbProbe: which surfaces are walls, which heights are
// climbed, and where the foot, edge and stand points of a climb go. Automation test
// NeuralAI.Climb.Probe. The traces against the world are checked in Lvl_ClimbAutoTest.

#include "Climb/EnemyClimbProbe.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyClimbProbeTest, "NeuralAI.Climb.Probe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEnemyClimbProbeTest::RunTest(const FString& Parameters)
{
	using FProbe = FEnemyClimbProbe;
	FProbe::FBody Body;
	Body.Radius = 42.0f;
	Body.HalfHeight = 60.0f;

	// Heights: steps are walked, the rest climbed up to the limit.
	{
		TestTrue(TEXT("97 cm is a step"), FProbe::CheckHeight(97.0f, 100.0f, 600.0f) == EEnemyClimbProbeResult::TooLow);
		TestTrue(TEXT("99 cm is climbed, within the tolerance"), FProbe::CheckHeight(99.0f, 100.0f, 600.0f) == EEnemyClimbProbeResult::Ok);
		TestTrue(TEXT("100 cm is climbed"), FProbe::CheckHeight(100.0f, 100.0f, 600.0f) == EEnemyClimbProbeResult::Ok);
		TestTrue(TEXT("400 cm is climbed"), FProbe::CheckHeight(400.0f, 100.0f, 600.0f) == EEnemyClimbProbeResult::Ok);
		TestTrue(TEXT("600 cm is climbed"), FProbe::CheckHeight(600.0f, 100.0f, 600.0f) == EEnemyClimbProbeResult::Ok);
		TestTrue(TEXT("600.5 cm is climbed, within the tolerance"), FProbe::CheckHeight(600.5f, 100.0f, 600.0f) == EEnemyClimbProbeResult::Ok);
		TestTrue(TEXT("603 cm is too high"), FProbe::CheckHeight(603.0f, 100.0f, 600.0f) == EEnemyClimbProbeResult::TooHigh);
	}

	// Walls: upright faces, a little lean either way; not floors, ceilings or ramps.
	{
		TestTrue(TEXT("upright face"), FProbe::IsUpright(FVector(-1.0f, 0.0f, 0.0f), 0.3f));
		TestTrue(TEXT("leaning a little"), FProbe::IsUpright(FVector(-0.96f, 0.0f, 0.28f), 0.3f));
		TestTrue(TEXT("overhanging a little"), FProbe::IsUpright(FVector(0.0f, 0.96f, -0.28f), 0.3f));
		TestFalse(TEXT("ramp"), FProbe::IsUpright(FVector(-0.8f, 0.0f, 0.6f), 0.3f));
		TestFalse(TEXT("floor"), FProbe::IsUpright(FVector(0.0f, 0.0f, 1.0f), 0.3f));
		TestFalse(TEXT("ceiling"), FProbe::IsUpright(FVector(0.0f, 0.0f, -1.0f), 0.3f));
	}

	// A wall facing -X at X = 500, floor at 0, top at 400.
	const FVector WallPoint(500.0f, 0.0f, 60.0f);
	const FVector WallNormal(-1.0f, 0.0f, 0.0f);
	const FVector Base = FProbe::GetBaseCenter(WallPoint, WallNormal, 0.0f, Body, 2.0f);
	const FVector Edge = FProbe::GetEdgeCenter(Base, 400.0f, Body, 10.0f);
	const FVector Stand = FProbe::GetStandCenter(WallPoint, WallNormal, 400.0f, Body, 20.0f);
	{
		TestTrue(FString::Printf(TEXT("foot: a radius and the gap off the face, standing on the floor (got %s)"), *Base.ToString()),
			Base.Equals(FVector(456.0f, 0.0f, 62.0f), 0.01f));
		TestTrue(FString::Printf(TEXT("edge: over the foot, feet 10 cm above the top (got %s)"), *Edge.ToString()),
			Edge.Equals(FVector(456.0f, 0.0f, 470.0f), 0.01f));
		TestTrue(FString::Printf(TEXT("stand: on top, a radius and the inset past the edge (got %s)"), *Stand.ToString()),
			Stand.Equals(FVector(562.0f, 0.0f, 462.0f), 0.01f));
		TestTrue(TEXT("standing clear of the edge"), Stand.X - Body.Radius >= WallPoint.X + 20.0f - 0.01f);
	}

	// A leaning wall's normal is flattened: the foot is still straight out from the face.
	{
		const FVector Leaning = FProbe::GetBaseCenter(WallPoint, FVector(-0.96f, 0.0f, 0.28f), 0.0f, Body, 2.0f);
		TestTrue(TEXT("leaning wall: foot straight out"), Leaning.Equals(FVector(456.0f, 0.0f, 62.0f), 0.01f));
	}

	// Over the edge: in first, down only once past it.
	{
		TestTrue(TEXT("mantle starts at the edge"), FProbe::GetMantlePoint(Edge, Stand, 0.0f).Equals(Edge, 0.01f));
		TestTrue(TEXT("mantle ends standing"), FProbe::GetMantlePoint(Edge, Stand, 1.0f).Equals(Stand, 0.01f));
		const FVector Half = FProbe::GetMantlePoint(Edge, Stand, 0.5f);
		TestEqual(TEXT("half way: half way in"), Half.X, (Edge.X + Stand.X) * 0.5f, 0.01);
		TestEqual(TEXT("half way: not down yet"), Half.Z, Edge.Z, 0.01);
		TestTrue(TEXT("half way: past the face"), Half.X > WallPoint.X);
		TestEqual(TEXT("three quarters: half way down"), FProbe::GetMantlePoint(Edge, Stand, 0.75f).Z, (Edge.Z + Stand.Z) * 0.5f, 0.01);
	}

	TestEqual(TEXT("ledge height"), [&] { FEnemyClimbLedge Ledge; Ledge.FloorZ = 20.0f; Ledge.TopZ = 420.0f; return Ledge.GetHeight(); }(), 400.0f, 0.01f);
	return true;
}

#endif
