// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRStaminaComponent.h"

UNRStaminaComponent::UNRStaminaComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UNRStaminaComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentStamina = MaxStamina;
}

void UNRStaminaComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	CurrentStamina = FMath::Min(CurrentStamina + RegenPerSecond * DeltaTime, MaxStamina);
}

bool UNRStaminaComponent::Consume(float Amount)
{
	if (Amount <= 0.0f)
	{
		return true;
	}
	if (!HasStamina(Amount))
	{
		return false;
	}
	CurrentStamina = FMath::Max(CurrentStamina - Amount, 0.0f);
	return true;
}

void UNRStaminaComponent::SetMaxStamina(float NewMaxStamina)
{
	NewMaxStamina = FMath::Max(NewMaxStamina, 1.0f);
	const float Delta = NewMaxStamina - MaxStamina;
	MaxStamina = NewMaxStamina;
	CurrentStamina = FMath::Clamp(CurrentStamina + FMath::Max(Delta, 0.0f), 0.0f, MaxStamina);
}
