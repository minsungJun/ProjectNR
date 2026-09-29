// Neural Enemy AI prototype module.

#include "Components/HealthComponent.h"
#include "NeuralAI.h"

UHealthComponent::UHealthComponent()
{
	// Nothing to do per frame; health only changes when someone calls ApplyDamage.
	PrimaryComponentTick.bCanEverTick = false;
}

FOnAnyHealthDamaged UHealthComponent::OnAnyDamaged;
TArray<TWeakObjectPtr<UHealthComponent>> UHealthComponent::ActiveComponents;

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetHealth();

	// Stale entries from components destroyed without EndPlay are pruned here too.
	ActiveComponents.RemoveAll([](const TWeakObjectPtr<UHealthComponent>& Entry) { return !Entry.IsValid(); });
	ActiveComponents.AddUnique(this);
}

void UHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ActiveComponents.Remove(this);
	Super::EndPlay(EndPlayReason);
}

void UHealthComponent::ResetHealth()
{
	MaxHealth = FMath::Max(MaxHealth, 1.0f);
	CurrentHealth = MaxHealth;
	bDead = false;
}

float UHealthComponent::GetHealthNormalized() const
{
	return MaxHealth > 0.0f ? FMath::Clamp(CurrentHealth / MaxHealth, 0.0f, 1.0f) : 0.0f;
}

float UHealthComponent::ApplyDamage(float Amount, AActor* DamageInstigator, const FVector& HitDirection)
{
	if (bDead || Amount <= 0.0f)
	{
		return 0.0f;
	}

	const float Applied = FMath::Min(Amount, CurrentHealth);
	CurrentHealth -= Applied;

	UE_LOG(LogNeuralAI, Verbose, TEXT("%s took %.1f damage (%.1f/%.1f)"),
		*GetNameSafe(GetOwner()), Applied, CurrentHealth, MaxHealth);

	OnDamaged.Broadcast(Applied, DamageInstigator, HitDirection);
	OnAnyDamaged.Broadcast(this, Applied);

	if (CurrentHealth <= 0.0f)
	{
		bDead = true;
		UE_LOG(LogNeuralAI, Log, TEXT("%s died"), *GetNameSafe(GetOwner()));
		OnDeath.Broadcast(DamageInstigator);
	}

	return Applied;
}
