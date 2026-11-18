// Neural Enemy AI prototype module.

// The boss's choice of pattern, no world: close in it slams when the slam is ready, otherwise it
// sweeps; it charges only at a seen target between the charge distances with the cooldown over.
// Automation test NeuralAI.Boss.Patterns.

#include "Characters/BossEnemyCharacter.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossPatternChoiceTest, "NeuralAI.Boss.Patterns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBossPatternChoiceTest::RunTest(const FString& Parameters)
{
	const ABossEnemyCharacter* Boss = GetDefault<ABossEnemyCharacter>();
	TestTrue(TEXT("the boss locks the body only while a pattern runs"), !Boss->IsAttackLocked());
	TestFalse(TEXT("the boss never retreats"), Boss->CanRetreat());
	TestTrue(TEXT("a crawler may retreat"), GetDefault<AEnemyCharacter>()->CanRetreat());

	TestTrue(TEXT("close and ready: slam"), ABossEnemyCharacter::ChooseCloseInPattern(300.0f, 380.0f, true) == EBossPattern::Slam);
	TestTrue(TEXT("close, slam cooling down: sweep"), ABossEnemyCharacter::ChooseCloseInPattern(300.0f, 380.0f, false) == EBossPattern::Sweep);
	TestTrue(TEXT("middle range: sweep"), ABossEnemyCharacter::ChooseCloseInPattern(550.0f, 380.0f, true) == EBossPattern::Sweep);

	TestTrue(TEXT("far, seen, ready: charge"), ABossEnemyCharacter::ShouldCharge(1200.0f, true, 700.0f, 2000.0f, true));
	TestFalse(TEXT("too close to charge"), ABossEnemyCharacter::ShouldCharge(500.0f, true, 700.0f, 2000.0f, true));
	TestFalse(TEXT("too far to charge"), ABossEnemyCharacter::ShouldCharge(2500.0f, true, 700.0f, 2000.0f, true));
	TestFalse(TEXT("not seen: no charge"), ABossEnemyCharacter::ShouldCharge(1200.0f, false, 700.0f, 2000.0f, true));
	TestFalse(TEXT("cooling down: no charge"), ABossEnemyCharacter::ShouldCharge(1200.0f, true, 700.0f, 2000.0f, false));
	return true;
}

#endif
