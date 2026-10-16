// Neural Enemy AI prototype module.

#include "Characters/EnemyCharacter.h"

#include "Animation/AnimSequence.h"
#include "Characters/EnemyVisualSettings.h"
#include "Climb/EnemyClimbComponent.h"
#include "Climb/EnemyClimbSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Components/EnemyAIDebugComponent.h"
#include "Components/EnemySensorComponent.h"
#include "Components/HealthComponent.h"
#include "Components/NeuralBrainComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Controllers/EnemyAIController.h"
#include "Data/EnemyAIProfile.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NeuralAI.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Seconds between walk cycle updates: often enough to follow the speed, rarely enough for a crowd. */
	constexpr float WalkUpdateInterval = 0.1f;
}

/** Console hook for testing the Stunned state, since nothing inflicts a stun yet. */
static FAutoConsoleCommandWithWorldAndArgs GStunEnemyCommand(
	TEXT("NeuralAI.StunEnemy"),
	TEXT("Stuns every enemy for the given number of seconds (default 3)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			const float Duration = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 3.0f;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->ApplyStun(Duration);
			}
		}));

/**
 * Spawns a ring of crawlers around the player, for the scaling target in the brief.
 *
 * Clones the template's profile and state tree so the copies are the same archetype as
 * the hand-placed one: measuring thirty enemies is only meaningful if they are doing the
 * same work as the one that was tuned.
 *
 *   NeuralAI.SpawnEnemies 30 1500
 */
static FAutoConsoleCommandWithWorldAndArgs GSpawnEnemiesCommand(
	TEXT("NeuralAI.SpawnEnemies"),
	TEXT("Spawns <count> enemies in a ring of <radius> cm around the player. Defaults: 10, 1200."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}

			const int32 Count = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 200) : 10;
			const float Radius = Args.Num() > 1 ? FMath::Max(FCString::Atof(*Args[1]), 100.0f) : 1200.0f;

			const APawn* Player = World->GetFirstPlayerController()
				? World->GetFirstPlayerController()->GetPawn() : nullptr;
			const FVector Centre = Player ? Player->GetActorLocation() : FVector::ZeroVector;

			// Copy settings from an existing crawler if there is one to copy from.
			const AEnemyCharacter* Template = nullptr;
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				Template = *It;
				break;
			}

			int32 Spawned = 0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const float Angle = (2.0f * PI * Index) / Count;
				const FVector Location = Centre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius;

				// Deferred: AutoPossessAI possesses the pawn and BeginPlay pushes the
				// profile into the components before SpawnActor even returns, so copying
				// the archetype afterwards would be a step too late and every clone would
				// come up on class defaults with no state tree.
				FTransform SpawnTransform(FRotator::ZeroRotator, Location);
				AEnemyCharacter* Enemy = World->SpawnActorDeferred<AEnemyCharacter>(
					AEnemyCharacter::StaticClass(), SpawnTransform, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

				if (Enemy)
				{
					Enemy->CopyArchetypeFrom(Template);
					Enemy->FinishSpawning(SpawnTransform);
					++Spawned;
				}
			}

			UE_LOG(LogNeuralAI, Log, TEXT("Spawned %d enemies in a %.0fcm ring"), Spawned, Radius);
		}));

AEnemyCharacter::AEnemyCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// Low and wide: a crawler silhouette, and it keeps the body clear of the
	// projectile line when the player aims at torso height.
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 60.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 720.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = MovementSpeed;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.0f;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(RootComponent);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CubeMesh.Object);
		BodyMesh->SetRelativeScale3D(FVector(1.0f, 0.8f, 0.5f));
		BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -35.0f));
	}

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));

	// Order matters only for readability: the sensor binds to health at BeginPlay,
	// and the debug component looks up the sensor there too.
	SensorComponent = CreateDefaultSubobject<UEnemySensorComponent>(TEXT("SensorComponent"));
	NeuralBrain = CreateDefaultSubobject<UNeuralBrainComponent>(TEXT("NeuralBrain"));
	DebugComponent = CreateDefaultSubobject<UEnemyAIDebugComponent>(TEXT("DebugComponent"));
	ClimbComponent = CreateDefaultSubobject<UEnemyClimbComponent>(TEXT("ClimbComponent"));

	AIControllerClass = AEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void AEnemyCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyVisual();
}

void AEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	// A placed actor that was saved before a component existed can come back with the
	// component present on the instance but its UPROPERTY pointer null, because hot
	// reload reinstancing does not always fix up references added after the save.
	// The symptom is silent: the pipeline simply stops at whichever pointer is missing.
	// Re-resolve by class and say so, rather than doing nothing for the rest of the run.
	ResolveComponentReferences();

	// Again here: an actor placed before the settings named a model has none saved on it.
	ApplyVisual();

	if (bShowsModel && WalkAnimation && GetMesh())
	{
		GetMesh()->PlayAnimation(WalkAnimation, true);
		GetMesh()->SetPlayRate(0.0f);
		GetWorldTimerManager().SetTimer(WalkTimer, this, &AEnemyCharacter::UpdateWalkAnimation, WalkUpdateInterval, true);
	}

	// Profile values are pushed outward here, after components exist but before anything
	// has ticked, so every consumer sees the archetype rather than its own default.
	if (AIProfile)
	{
		MovementSpeed = AIProfile->MovementSpeed;

		if (SensorComponent)
		{
			SensorComponent->ApplyProfile(AIProfile);
		}
		if (NeuralBrain)
		{
			NeuralBrain->ApplyProfile(AIProfile);
		}
	}

	GetCharacterMovement()->MaxWalkSpeed = MovementSpeed;

	if (BodyMesh)
	{
		BodyRestTransform = BodyMesh->GetRelativeTransform();
	}

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AEnemyCharacter::HandleDeath);
		HealthComponent->OnDamaged.AddDynamic(this, &AEnemyCharacter::HandleDamaged);
	}
}

void AEnemyCharacter::ResolveComponentReferences()
{
	int32 Recovered = 0;

	if (!HealthComponent)
	{
		HealthComponent = FindComponentByClass<UHealthComponent>();
		Recovered += HealthComponent ? 1 : 0;
	}
	if (!SensorComponent)
	{
		SensorComponent = FindComponentByClass<UEnemySensorComponent>();
		Recovered += SensorComponent ? 1 : 0;
	}
	if (!NeuralBrain)
	{
		NeuralBrain = FindComponentByClass<UNeuralBrainComponent>();
		Recovered += NeuralBrain ? 1 : 0;
	}
	if (!DebugComponent)
	{
		DebugComponent = FindComponentByClass<UEnemyAIDebugComponent>();
		Recovered += DebugComponent ? 1 : 0;
	}
	if (!ClimbComponent)
	{
		ClimbComponent = FindComponentByClass<UEnemyClimbComponent>();
		Recovered += ClimbComponent ? 1 : 0;
	}

	if (Recovered > 0)
	{
		UE_LOG(LogNeuralAI, Warning,
			TEXT("%s recovered %d component reference(s) by class. This actor was placed before those ")
			TEXT("components existed; re-placing it will avoid the lookup."),
			*GetNameSafe(this), Recovered);
	}

	if (!SensorComponent || !NeuralBrain)
	{
		UE_LOG(LogNeuralAI, Error, TEXT("%s is missing sensor or brain entirely; it will not act"),
			*GetNameSafe(this));
	}
}

bool AEnemyCharacter::IsDead() const
{
	return HealthComponent && HealthComponent->IsDead();
}

void AEnemyCharacter::HandleDeath(AActor* Killer)
{
	UE_LOG(LogNeuralAI, Log, TEXT("%s is down"), *GetNameSafe(this));

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Killed on a wall it lets go and falls instead of hanging there.
	if (ClimbComponent)
	{
		ClimbComponent->DropDead();
	}

	// Collapse and stay collapsed: nothing hands the model back to the walk cycle after this.
	GetWorldTimerManager().ClearTimer(WalkTimer);
	GetWorldTimerManager().ClearTimer(OneShotTimer);
	if (bShowsModel && DeathAnimation && GetMesh())
	{
		bPlayingOneShot = true;
		GetMesh()->PlayAnimation(DeathAnimation, false);
		GetMesh()->SetPlayRate(1.0f);
	}

	// The controller checks IsDead() before issuing anything, so unpossessing is not
	// required. Leave the pawn in place so the death state stays inspectable.
}

const FEnemyVisual& AEnemyCharacter::GetVisual() const
{
	return GetDefault<UEnemyVisualSettings>()->Crawler;
}

void AEnemyCharacter::ApplyVisual()
{
	USkeletalMeshComponent* Model = GetMesh();
	if (!Model)
	{
		return;
	}

	const FEnemyVisual& Visual = GetVisual();
	USkeletalMesh* Asset = Visual.Mesh.IsNull() ? nullptr : Visual.Mesh.LoadSynchronous();
	if (!Asset && !Visual.Mesh.IsNull())
	{
		// Content is not in the repository, so a fresh checkout lacks the models. Say so once per model.
		static TSet<FSoftObjectPath> Reported;
		if (!Reported.Contains(Visual.Mesh.ToSoftObjectPath()))
		{
			Reported.Add(Visual.Mesh.ToSoftObjectPath());
			UE_LOG(LogNeuralAI, Warning, TEXT("Enemy model %s is not in this checkout; %s keeps its placeholder cube"),
				*Visual.Mesh.ToString(), *GetClass()->GetName());
		}
	}

	bShowsModel = Asset != nullptr;
	WalkAnimation = bShowsModel && !Visual.WalkAnimation.IsNull() ? Visual.WalkAnimation.LoadSynchronous() : nullptr;
	AttackAnimation = bShowsModel && !Visual.AttackAnimation.IsNull() ? Visual.AttackAnimation.LoadSynchronous() : nullptr;
	HitAnimation = bShowsModel && !Visual.HitAnimation.IsNull() ? Visual.HitAnimation.LoadSynchronous() : nullptr;
	DeathAnimation = bShowsModel && !Visual.DeathAnimation.IsNull() ? Visual.DeathAnimation.LoadSynchronous() : nullptr;
	Model->SetSkeletalMeshAsset(Asset);
	Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Model->SetVisibility(bShowsModel);
	if (bShowsModel)
	{
		ModelRestTransform = FTransform(Visual.Rotation, Visual.Offset, FVector(Visual.Scale));
		Model->SetRelativeTransform(ModelRestTransform);
	}
	if (BodyMesh)
	{
		// Still there without the cube showing: the climb pose turns about it.
		BodyMesh->SetVisibility(!bShowsModel);
	}
}

void AEnemyCharacter::UpdateWalkAnimation()
{
	USkeletalMeshComponent* Model = GetMesh();
	if (!Model || !WalkAnimation || bPlayingOneShot)
	{
		return;
	}
	if (IsDead())
	{
		Model->SetPlayRate(0.0f);
		GetWorldTimerManager().ClearTimer(WalkTimer);
		return;
	}

	// Climbing moves the capsule by hand, without a velocity: use the climb speed then.
	const bool bOnWall = ClimbComponent && ClimbComponent->IsOnWall();
	const float Speed = bOnWall ? GetDefault<UEnemyClimbSettings>()->ClimbSpeed : GetVelocity().Size2D();
	const FEnemyVisual& Visual = GetVisual();
	if (Speed > 10.0f)
	{
		Model->SetPlayRate(FMath::Min(Speed / Visual.WalkAnimationSpeed, Visual.MaxWalkPlayRate));
		return;
	}

	// Every foot is down at the start of each half of the cycle; finish the step to the next one.
	const float Half = WalkAnimation->GetPlayLength() * 0.5f;
	const float Position = Model->GetPosition();
	const float ToRest = Half - FMath::Fmod(Position, Half);
	if (ToRest <= WalkUpdateInterval || ToRest >= Half - KINDA_SMALL_NUMBER)
	{
		Model->SetPosition(FMath::Fmod(FMath::RoundToFloat(Position / Half) * Half, Half * 2.0f), false);
		Model->SetPlayRate(0.0f);
	}
	else
	{
		Model->SetPlayRate(1.0f);
	}
}

void AEnemyCharacter::PlayAttackAnimation()
{
	PlayOneShotAnimation(AttackAnimation);
}

void AEnemyCharacter::HandleDamaged(float Damage, AActor* DamageInstigator, const FVector& HitDirection)
{
	// A killing blow broadcasts here before the health component marks itself dead, so read the
	// health left: the dead keep still rather than flinch.
	if (Damage > 0.0f && HealthComponent && HealthComponent->GetCurrentHealth() > 0.0f)
	{
		PlayOneShotAnimation(HitAnimation);
	}
}

void AEnemyCharacter::PlayOneShotAnimation(UAnimSequence* Animation)
{
	USkeletalMeshComponent* Model = GetMesh();
	if (!bShowsModel || !Animation || !Model)
	{
		return;
	}
	bPlayingOneShot = true;
	Model->PlayAnimation(Animation, false);
	Model->SetPlayRate(1.0f);
	GetWorldTimerManager().SetTimer(OneShotTimer, this, &AEnemyCharacter::FinishOneShotAnimation,
		Animation->GetPlayLength(), false);
}

void AEnemyCharacter::FinishOneShotAnimation()
{
	bPlayingOneShot = false;
	USkeletalMeshComponent* Model = GetMesh();
	if (Model && WalkAnimation)
	{
		// From where every foot is down; the walk timer picks the rate up from here.
		Model->PlayAnimation(WalkAnimation, true);
		Model->SetPlayRate(0.0f);
	}
}

void AEnemyCharacter::SetClimbPose(float Alpha)
{
	if (!BodyMesh)
	{
		return;
	}
	const float A = FMath::Clamp(Alpha, 0.0f, 1.0f);
	const FVector PivotLocation = FMath::Lerp(BodyRestTransform.GetLocation(), ClimbBodyLocation, A);
	const FQuat PivotRotation = FQuat::Slerp(BodyRestTransform.GetRotation(), ClimbBodyRotation.Quaternion(), A);
	BodyMesh->SetRelativeLocationAndRotation(PivotLocation, PivotRotation);

	if (bShowsModel && GetMesh())
	{
		// The model rides the body mesh's pivot as if fixed to it (without the cube's scale).
		const FTransform RestPivot(BodyRestTransform.GetRotation(), BodyRestTransform.GetLocation());
		GetMesh()->SetRelativeTransform(ModelRestTransform.GetRelativeTransform(RestPivot) * FTransform(PivotRotation, PivotLocation));
	}
}

int32 AEnemyCharacter::PerformAttack()
{
	UWorld* World = GetWorld();
	if (!World || IsDead())
	{
		return 0;
	}

	const FVector Origin = GetActorLocation();
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(AttackHalfAngle));

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyAttack), false, this);
	World->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeSphere(AttackRange), Params);

	int32 HitCount = 0;
	TSet<AActor*> AlreadyHit;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Other = Overlap.GetActor();
		if (!Other || Other == this || AlreadyHit.Contains(Other))
		{
			continue;
		}

		// No friendly fire: once allies converge on the same target (see the sensor's
		// alert), a swing at the player would otherwise land on the crawler beside it.
		if (Other->IsA<AEnemyCharacter>())
		{
			continue;
		}

		UHealthComponent* OtherHealth = Other->FindComponentByClass<UHealthComponent>();
		if (!OtherHealth || OtherHealth->IsDead())
		{
			continue;
		}

		// Arc test on the ground plane: a sphere alone would let the crawler hit things
		// behind it, which reads as a bug even though the damage numbers are right.
		FVector ToOther = Other->GetActorLocation() - Origin;
		ToOther.Z = 0.0f;
		const FVector ToOtherDir = ToOther.GetSafeNormal();
		if (FVector::DotProduct(Forward, ToOtherDir) < CosHalfAngle)
		{
			continue;
		}

		AlreadyHit.Add(Other);
		OtherHealth->ApplyDamage(AttackDamage, this, ToOtherDir);
		++HitCount;
	}

#if ENABLE_DRAW_DEBUG
	DrawDebugCone(World, Origin, Forward, AttackRange,
		FMath::DegreesToRadians(AttackHalfAngle), FMath::DegreesToRadians(15.0f), 16,
		HitCount > 0 ? FColor::Red : FColor(150, 150, 150), false, 0.35f, 0, 2.0f);
#endif

	PlayAttackAnimation();

	UE_LOG(LogNeuralAI, Log, TEXT("%s attacked, %d hit"), *GetNameSafe(this), HitCount);
	return HitCount;
}

void AEnemyCharacter::ApplyStun(float Duration)
{
	if (IsDead() || Duration <= 0.0f)
	{
		return;
	}

	bStunned = true;
	GetCharacterMovement()->StopMovementImmediately();

	UE_LOG(LogNeuralAI, Log, TEXT("%s stunned for %.1fs"), *GetNameSafe(this), Duration);

	// Re-arming the timer refreshes the duration rather than stacking stuns.
	GetWorld()->GetTimerManager().SetTimer(
		StunTimer, this, &AEnemyCharacter::ClearStun, Duration, false);
}

void AEnemyCharacter::ClearStun()
{
	bStunned = false;
	UE_LOG(LogNeuralAI, Log, TEXT("%s recovered from stun"), *GetNameSafe(this));
}

void AEnemyCharacter::CopyArchetypeFrom(const AEnemyCharacter* Other)
{
	if (!Other || Other == this)
	{
		return;
	}

	AIProfile = Other->AIProfile;
	CombatStateTree = Other->CombatStateTree;
}
