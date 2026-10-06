// Copyright Epic Games, Inc. All Rights Reserved.

// NR.TurnTrace [seconds]
//
// Logs, every 0.05 s for the given time (default 1.5 s), where the player's capsule faces and
// where the visible body faces (the mesh's root bone, corrected for the mesh's own yaw offset).
// The two differ whenever the animation blueprint lets the body lag the capsule - the sample's
// Offset Root Bone in Accumulate mode does exactly that - so this measures how long a turn
// really takes on screen rather than how long the movement component takes.

#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NRAimComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRTurnTrace, Log, All);

namespace NRTurnTrace
{
	void Run(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
		USkeletalMeshComponent* Mesh = Pawn ? Pawn->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
		if (!Mesh)
		{
			UE_LOG(LogNRTurnTrace, Warning, TEXT("No player pawn with a skeletal mesh"));
			return;
		}

		const float Duration = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 1.5f;
		const double Start = World->GetTimeSeconds();
		TWeakObjectPtr<APawn> WeakPawn(Pawn);
		TWeakObjectPtr<USkeletalMeshComponent> WeakMesh(Mesh);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		double NextLog = Start;

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[WeakPawn, WeakMesh, WeakWorld, Start, Duration, NextLog](float) mutable
			{
				const APawn* P = WeakPawn.Get();
				const USkeletalMeshComponent* M = WeakMesh.Get();
				const UWorld* W = WeakWorld.Get();
				if (!P || !M || !W)
				{
					return false;
				}

				const double Now = W->GetTimeSeconds();
				if (Now - Start > Duration)
				{
					return false;
				}
				if (Now < NextLog)
				{
					return true;
				}
				NextLog = Now + 0.05;

				const float CapsuleYaw = P->GetActorRotation().Yaw;
				// Root bone yaw, minus the mesh's fixed yaw offset from the capsule (the
				// mannequin is authored facing +Y, so the mesh sits at -90).
				const float RootYaw = M->GetSocketRotation(TEXT("root")).Yaw - M->GetRelativeRotation().Yaw;
				const float Delta = FRotator::NormalizeAxis(RootYaw - CapsuleYaw);
				const UNRAimComponent* Aim = P->FindComponentByClass<UNRAimComponent>();

				UE_LOG(LogNRTurnTrace, Log, TEXT("t=%.2f capsule=%7.1f body=%7.1f lag=%6.1f speed=%4.0f aim=%s"),
					Now - Start, CapsuleYaw, FRotator::NormalizeAxis(RootYaw), Delta, P->GetVelocity().Size2D(),
					Aim ? *UEnum::GetValueAsString(Aim->GetAimMode()) : TEXT("-"));
				return true;
			}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRTurnTraceCommand(TEXT("NR.TurnTrace"),
	TEXT("NR.TurnTrace [seconds]: log capsule yaw vs visible body (root bone) yaw every 0.05 s."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRTurnTrace::Run));
