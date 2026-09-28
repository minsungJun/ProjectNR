// Neural Enemy AI prototype module.

#include "Characters/AITestPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/HealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "NeuralAI.h"
#include "Projectiles/AITestProjectile.h"
#include "UObject/ConstructorHelpers.h"

/**
 * Runtime override for the test player's walk speed.
 *
 * Exists because circling the crawler to provoke a side-on dodge is fiddly at a fixed
 * speed, and recompiling to try a different one costs an editor round trip. Zero or
 * less leaves the character's own MovementSpeed in charge.
 */
static TAutoConsoleVariable<float> CVarPlayerSpeed(
	TEXT("NeuralAI.PlayerSpeed"),
	0.0f,
	TEXT("Walk speed in cm/s for the test player. 0 or less: use the character default."),
	ECVF_Default);

AAITestPlayerCharacter::AAITestPlayerCharacter()
{
	// Ticks only to apply the speed console variable, which is worth one float assignment
	// per frame while the AI is being tuned.
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = MovementSpeed;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.0f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(RootComponent);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderMesh.Object);
		// Cylinder is 100cm tall and 100cm wide; match the 42r/96hh capsule.
		BodyMesh->SetRelativeScale3D(FVector(0.84f, 0.84f, 1.92f));
		BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -96.0f));
	}

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));

	ProjectileClass = AAITestProjectile::StaticClass();

	// Reuse the input assets that already ship with this project rather than
	// authoring duplicates. All four stay overridable per-instance.
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MappingContextAsset(TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (MappingContextAsset.Succeeded())
	{
		DefaultMappingContext = MappingContextAsset.Object;
	}
	static ConstructorHelpers::FObjectFinder<UInputAction> MoveActionAsset(TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	if (MoveActionAsset.Succeeded())
	{
		MoveAction = MoveActionAsset.Object;
	}
	static ConstructorHelpers::FObjectFinder<UInputAction> LookActionAsset(TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
	if (LookActionAsset.Succeeded())
	{
		LookAction = LookActionAsset.Object;
	}
	static ConstructorHelpers::FObjectFinder<UInputAction> MouseLookActionAsset(TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
	if (MouseLookActionAsset.Succeeded())
	{
		MouseLookAction = MouseLookActionAsset.Object;
	}
}

void AAITestPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float Override = CVarPlayerSpeed.GetValueOnGameThread();
	GetCharacterMovement()->MaxWalkSpeed = Override > 0.0f ? Override : MovementSpeed;
}

void AAITestPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	GetCharacterMovement()->MaxWalkSpeed = MovementSpeed;

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
			else
			{
				UE_LOG(LogNeuralAI, Warning, TEXT("%s has no DefaultMappingContext; movement input will not work"), *GetNameSafe(this));
			}
		}
	}
}

void AAITestPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AAITestPlayerCharacter::Move);
		}
		if (LookAction)
		{
			EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AAITestPlayerCharacter::Look);
		}
		if (MouseLookAction)
		{
			EnhancedInput->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AAITestPlayerCharacter::Look);
		}
	}

	// Firing is bound to a raw key rather than an InputAction so this module does not
	// need a content asset of its own just to shoot.
	if (PlayerInputComponent)
	{
		PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AAITestPlayerCharacter::HandleFirePressed);
	}
}

void AAITestPlayerCharacter::HandleFirePressed()
{
	FireProjectile();
}

void AAITestPlayerCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	if (!GetController())
	{
		return;
	}

	const FRotator YawRotation(0.0f, GetController()->GetControlRotation().Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
}

void AAITestPlayerCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	AddControllerYawInput(LookAxisVector.X);
	AddControllerPitchInput(LookAxisVector.Y);
}

FVector AAITestPlayerCharacter::GetAimDirection() const
{
	// Aim down the camera, not the body: the body lags behind because it orients to movement.
	if (FollowCamera)
	{
		return FollowCamera->GetForwardVector();
	}
	return GetActorForwardVector();
}

FVector AAITestPlayerCharacter::GetMuzzleLocation() const
{
	const FVector AimDirection = GetAimDirection();
	const FRotator AimRotation = AimDirection.Rotation();
	return GetActorLocation() + AimRotation.RotateVector(MuzzleOffset);
}

AAITestProjectile* AAITestPlayerCharacter::FireProjectile()
{
	UWorld* World = GetWorld();
	if (!World || !ProjectileClass)
	{
		return nullptr;
	}

	const float Now = World->GetTimeSeconds();
	if (Now - LastFireTime < FireCooldown)
	{
		return nullptr;
	}
	LastFireTime = Now;

	const FVector AimDirection = GetAimDirection();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AAITestProjectile* Projectile = World->SpawnActor<AAITestProjectile>(
		ProjectileClass, GetMuzzleLocation(), AimDirection.Rotation(), SpawnParams);

	if (Projectile)
	{
		Projectile->LaunchInDirection(AimDirection);
		UE_LOG(LogNeuralAI, Verbose, TEXT("Fired %s"), *GetNameSafe(Projectile));
	}

	return Projectile;
}
