// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRRagdollComponent.h"

#include "Components/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogNRRagdoll, Log, All);

UNRRagdollComponent::UNRRagdollComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNRRagdollComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UHealthComponent* Health = GetOwner()->FindComponentByClass<UHealthComponent>())
	{
		Health->OnDeath.AddUniqueDynamic(this, &UNRRagdollComponent::HandleDeath);
	}
	else
	{
		UE_LOG(LogNRRagdoll, Warning, TEXT("%s has no UHealthComponent; it will never ragdoll"), *GetNameSafe(GetOwner()));
	}
}

void UNRRagdollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UHealthComponent* Health = GetOwner()->FindComponentByClass<UHealthComponent>())
	{
		Health->OnDeath.RemoveDynamic(this, &UNRRagdollComponent::HandleDeath);
	}
	Super::EndPlay(EndPlayReason);
}

USkeletalMeshComponent* UNRRagdollComponent::FindRagdollMesh() const
{
	TArray<USkeletalMeshComponent*> Meshes;
	GetOwner()->GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh && Mesh->GetPhysicsAsset())
		{
			return Mesh;
		}
	}
	return nullptr;
}

void UNRRagdollComponent::HandleDeath(AActor* Killer)
{
	// Away from the killer, flattened, then tipped up a little.
	FVector Away = GetOwner()->GetActorForwardVector() * -1.0f;
	if (Killer && Killer != GetOwner())
	{
		Away = (GetOwner()->GetActorLocation() - Killer->GetActorLocation()).GetSafeNormal2D();
	}
	StartRagdoll((Away * (1.0f - DeathPushUp) + FVector::UpVector * DeathPushUp).GetSafeNormal());
}

void UNRRagdollComponent::StartRagdoll(const FVector& PushDirection)
{
	if (bRagdolling)
	{
		return;
	}

	USkeletalMeshComponent* Mesh = FindRagdollMesh();
	if (!Mesh)
	{
		UE_LOG(LogNRRagdoll, Warning, TEXT("%s has no skeletal mesh with a physics asset; cannot ragdoll"), *GetNameSafe(GetOwner()));
		return;
	}
	bRagdolling = true;

	// Physics owns the pose from here; the animation keeps running underneath but is fully
	// overridden. The body must not collide with its own capsule, which stays put.
	Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->SetAllBodiesSimulatePhysics(true);
	Mesh->SetSimulatePhysics(true);
	Mesh->WakeAllRigidBodies();
	Mesh->bBlendPhysics = true;

	if (DeathPushSpeed > 0.0f && !PushDirection.IsNearlyZero())
	{
		const FName Bone = Mesh->GetBoneIndex(PushBone) != INDEX_NONE ? PushBone : NAME_None;
		Mesh->AddImpulse(PushDirection * DeathPushSpeed, Bone, /*bVelChange=*/true);
	}

	UE_LOG(LogNRRagdoll, Log, TEXT("%s ragdolls (%s)"), *GetNameSafe(GetOwner()), *GetNameSafe(Mesh->GetPhysicsAsset()));
}
