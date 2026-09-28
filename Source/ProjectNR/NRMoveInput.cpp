// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRMoveInput.h"

#include "GameFramework/Actor.h"
#include "MoverComponent.h"

FVector NRMoveInput::Read(AActor* Owner)
{
	if (!Owner)
	{
		return FVector::ZeroVector;
	}

	if (UFunction* GetMoveInput = Owner->FindFunction(TEXT("Get_MoveInput")))
	{
		TArray<uint8> Params;
		Params.SetNumZeroed(GetMoveInput->ParmsSize);
		Owner->ProcessEvent(GetMoveInput, Params.GetData());

		for (TFieldIterator<FStructProperty> It(GetMoveInput); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_OutParm) && It->Struct == TBaseStructure<FVector>::Get())
			{
				FVector Input = *It->ContainerPtrToValuePtr<FVector>(Params.GetData());
				Input.Z = 0.0f;
				return Input;
			}
		}
	}

	if (const UMoverComponent* Mover = Owner->FindComponentByClass<UMoverComponent>())
	{
		FVector Intent = Mover->GetMovementIntent();
		Intent.Z = 0.0f;
		return Intent;
	}
	return FVector::ZeroVector;
}
