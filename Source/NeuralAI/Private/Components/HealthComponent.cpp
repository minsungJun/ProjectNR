// Neural Enemy AI prototype module.

#include "Components/HealthComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
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
	CurrentShield = 0.0f;
	bDead = false;
	InvulnerableUntil = -1.0f;
}

void UHealthComponent::SetInvulnerableFor(float Seconds)
{
	if (const UWorld* World = GetWorld(); World && Seconds > 0.0f)
	{
		InvulnerableUntil = FMath::Max(InvulnerableUntil, World->GetTimeSeconds() + Seconds);
	}
}

bool UHealthComponent::IsInvulnerable() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < InvulnerableUntil;
}

void UHealthComponent::SetMaxHealth(float NewMaxHealth)
{
	NewMaxHealth = FMath::Max(NewMaxHealth, 1.0f);
	const float Delta = NewMaxHealth - MaxHealth;
	MaxHealth = NewMaxHealth;
	if (!bDead)
	{
		CurrentHealth = FMath::Clamp(CurrentHealth + FMath::Max(Delta, 0.0f), 0.0f, MaxHealth);
	}
}

float UHealthComponent::Heal(float Amount)
{
	if (bDead || Amount <= 0.0f)
	{
		return 0.0f;
	}

	const float Restored = FMath::Min(Amount, MaxHealth - CurrentHealth);
	CurrentHealth += Restored;
	return Restored;
}

float UHealthComponent::AddShield(float Amount)
{
	if (bDead || Amount <= 0.0f)
	{
		return 0.0f;
	}

	const float Added = FMath::Min(Amount, FMath::Max(MaxShield - CurrentShield, 0.0f));
	CurrentShield += Added;
	return Added;
}

float UHealthComponent::GetHealthNormalized() const
{
	return MaxHealth > 0.0f ? FMath::Clamp(CurrentHealth / MaxHealth, 0.0f, 1.0f) : 0.0f;
}

float UHealthComponent::ApplyDamage(float Amount, AActor* DamageInstigator, const FVector& HitDirection, EAdaptationWeapon Source)
{
	if (bDead || Amount <= 0.0f)
	{
		return 0.0f;
	}

	// The weapon this enemy has adapted to bites less. Before armor, so armor's floor still holds.
	if (const float Resistance = GetWeaponResistance(Source); Resistance > 0.0f)
	{
		Amount *= 1.0f - Resistance;
	}

	if (IsInvulnerable())
	{
		UE_LOG(LogNeuralAI, Log, TEXT("%s ignored %.1f damage (invulnerable)"), *GetNameSafe(GetOwner()), Amount);
		return 0.0f;
	}

	if (Armor > 0.0f)
	{
		Amount = FMath::Max(Amount - Armor, FMath::Min(Amount, MinDamageAfterArmor));
	}

	const float Absorbed = FMath::Min(Amount, CurrentShield);
	CurrentShield -= Absorbed;
	const float Applied = Absorbed + FMath::Min(Amount - Absorbed, CurrentHealth);
	CurrentHealth -= Applied - Absorbed;

	UE_LOG(LogNeuralAI, Verbose, TEXT("%s took %.1f damage (%.1f/%.1f, shield %.1f)"),
		*GetNameSafe(GetOwner()), Applied, CurrentHealth, MaxHealth, CurrentShield);

	LastDamageSource = Source;
	if (const UWorld* World = GetWorld())
	{
		LastDamageTime = World->GetTimeSeconds();
	}

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

void UHealthComponent::SetWeaponResistance(EAdaptationWeapon Weapon, float Resistance)
{
	const int32 Index = static_cast<int32>(Weapon);
	if (Weapon != EAdaptationWeapon::None && Index < static_cast<int32>(EAdaptationWeapon::Num))
	{
		WeaponResistance[Index] = FMath::Clamp(Resistance, 0.0f, 0.9f);
	}
}

float UHealthComponent::GetWeaponResistance(EAdaptationWeapon Weapon) const
{
	const int32 Index = static_cast<int32>(Weapon);
	return Weapon != EAdaptationWeapon::None && Index < static_cast<int32>(EAdaptationWeapon::Num) ? WeaponResistance[Index] : 0.0f;
}

void UHealthComponent::SetRegeneration(float FractionPerSecond, float DelaySeconds)
{
	RegenPerSecond = FMath::Clamp(FractionPerSecond, 0.0f, 1.0f);
	RegenDelay = FMath::Max(DelaySeconds, 0.0f);
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (RegenPerSecond > 0.0f)
	{
		if (!World->GetTimerManager().IsTimerActive(RegenTimer))
		{
			World->GetTimerManager().SetTimer(RegenTimer, this, &UHealthComponent::TickRegeneration, 0.25f, true);
		}
	}
	else
	{
		World->GetTimerManager().ClearTimer(RegenTimer);
	}
}

void UHealthComponent::TickRegeneration()
{
	const UWorld* World = GetWorld();
	if (!World || bDead || RegenPerSecond <= 0.0f || CurrentHealth >= MaxHealth
		|| World->GetTimeSeconds() - LastDamageTime < RegenDelay)
	{
		return;
	}
	Heal(MaxHealth * RegenPerSecond * 0.25f);
}
