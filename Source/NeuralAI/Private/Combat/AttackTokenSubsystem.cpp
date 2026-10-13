// Neural Enemy AI prototype module.

#include "Combat/AttackTokenSubsystem.h"

#include "Combat/AttackTokenSettings.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "NeuralAI.h"

static TAutoConsoleVariable<int32> CVarAttackTokenDebug(TEXT("NeuralAI.AttackTokens.Debug"), 0,
	TEXT("1: log every attack token taken."));

namespace
{
	/** Seconds without asking again before an enemy stops counting as waiting for a token. */
	constexpr float AttackTokenWaitLapse = 0.5f;
}

bool UAttackTokenSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UAttackTokenSubsystem::IsEnabled() const
{
	return GetDefault<UAttackTokenSettings>()->bEnabled;
}

bool UAttackTokenSubsystem::TryTakeToken(AActor* Enemy, AActor* Target)
{
	const UWorld* World = GetWorld();
	if (!World || !Enemy || !Target)
	{
		return false;
	}
	if (!IsEnabled())
	{
		return true;
	}
	const float Now = World->GetTimeSeconds();
	const UAttackTokenSettings* Settings = GetDefault<UAttackTokenSettings>();

	Targets.RemoveAll([](const FTargetTokens& Entry) { return !Entry.Target.IsValid(); });
	FTargetTokens* Entry = Targets.FindByPredicate([Target](const FTargetTokens& Candidate) { return Candidate.Target.Get() == Target; });
	if (!Entry)
	{
		Entry = &Targets.AddDefaulted_GetRef();
		Entry->Target = Target;
	}

	Entry->Tokens.RemoveAll([Now](const FToken& Token) { return !Token.Holder.IsValid() || Token.Until <= Now; });
	Entry->Waiting.RemoveAll([Now](const FWaiting& Waiting) { return !Waiting.Enemy.IsValid() || Now - Waiting.LastAsked > AttackTokenWaitLapse; });

	FWaiting* Mine = Entry->Waiting.FindByPredicate([Enemy](const FWaiting& Waiting) { return Waiting.Enemy.Get() == Enemy; });
	if (!Mine)
	{
		Mine = &Entry->Waiting.AddDefaulted_GetRef();
		Mine->Enemy = Enemy;
		Mine->Since = Now;
	}
	Mine->LastAsked = Now;
	const float MySince = Mine->Since;

	// Full, or someone has been kept waiting longer: not yet.
	const bool bFull = Entry->Tokens.Num() >= Settings->MaxAttackers;
	const bool bQueued = Entry->Waiting.ContainsByPredicate([Enemy, MySince](const FWaiting& Waiting)
	{
		return Waiting.Enemy.Get() != Enemy && Waiting.Since < MySince;
	});
	if (bFull || bQueued)
	{
		return false;
	}

	Entry->Waiting.RemoveAll([Enemy](const FWaiting& Waiting) { return Waiting.Enemy.Get() == Enemy; });
	FToken& Token = Entry->Tokens.AddDefaulted_GetRef();
	Token.Holder = Enemy;
	Token.Until = Now + Settings->TokenHold;
	if (CVarAttackTokenDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogNeuralAI, Log, TEXT("%s takes an attack token on %s (%d/%d out, %d waiting)"), *GetNameSafe(Enemy),
			*GetNameSafe(Target), Entry->Tokens.Num(), Settings->MaxAttackers, Entry->Waiting.Num());
	}
	return true;
}

void UAttackTokenSubsystem::Release(AActor* Enemy)
{
	for (FTargetTokens& Entry : Targets)
	{
		Entry.Tokens.RemoveAll([Enemy](const FToken& Token) { return Token.Holder.Get() == Enemy; });
		Entry.Waiting.RemoveAll([Enemy](const FWaiting& Waiting) { return Waiting.Enemy.Get() == Enemy; });
	}
}

void UAttackTokenSubsystem::LogStatus() const
{
	const UAttackTokenSettings* Settings = GetDefault<UAttackTokenSettings>();
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.0f;
	UE_LOG(LogNeuralAI, Log, TEXT("Attack tokens: %s, %d target(s)"), IsEnabled() ? TEXT("on") : TEXT("off"), Targets.Num());
	for (const FTargetTokens& Entry : Targets)
	{
		const int32 Out = Entry.Tokens.FilterByPredicate([Now](const FToken& Token) { return Token.Holder.IsValid() && Token.Until > Now; }).Num();
		UE_LOG(LogNeuralAI, Log, TEXT("  %s: tokens %d/%d, %d waiting"), *GetNameSafe(Entry.Target.Get()), Out, Settings->MaxAttackers, Entry.Waiting.Num());
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNeuralAIAttackTokensStatusCommand(TEXT("NeuralAI.AttackTokens.Status"),
	TEXT("Log every target's attack tokens out and who is waiting."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
	{
		if (const UAttackTokenSubsystem* Tokens = World ? World->GetSubsystem<UAttackTokenSubsystem>() : nullptr)
		{
			Tokens->LogStatus();
		}
	}));
