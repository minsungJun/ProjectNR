// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRHUD.h"

#include "CanvasItem.h"
#include "Components/CapsuleComponent.h"
#include "Components/HealthComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NRLoadoutWidget.h"
#include "NRRoom.h"
#include "NRRunSubsystem.h"
#include "NRWeaponComponent.h"
#include "NRWeaponPartComponent.h"

void ANRHUD::BeginPlay()
{
	Super::BeginPlay();
	AnyDamagedHandle = UHealthComponent::OnAnyDamaged.AddUObject(this, &ANRHUD::HandleAnyDamaged);

	if (UNRRunSubsystem* Run = GetWorld() ? GetWorld()->GetSubsystem<UNRRunSubsystem>() : nullptr)
	{
		Run->OnRunStateChanged.AddUniqueDynamic(this, &ANRHUD::HandleRunStateChanged);
	}

	// The HUD's own input component, pushed onto the controller's stack. Tab is not mapped by
	// the sample, so a plain key binding is enough.
	if (PlayerOwner)
	{
		EnableInput(PlayerOwner);
		if (InputComponent)
		{
			InputComponent->BindKey(LoadoutMenuKey, IE_Pressed, this, &ANRHUD::ToggleLoadoutMenu);
			InputComponent->BindKey(RestartKey, IE_Pressed, this, &ANRHUD::OnRestartPressed);
		}
	}
}

void ANRHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsLoadoutMenuOpen())
	{
		CloseLoadoutMenu();
	}
	UHealthComponent::OnAnyDamaged.Remove(AnyDamagedHandle);
	AnyDamagedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void ANRHUD::HandleRunStateChanged(ENRRunState NewState)
{
	// The run is over: no more loadout changes, and the cursor and input mode the menu set
	// must not outlive it.
	if (NewState == ENRRunState::Won || NewState == ENRRunState::Lost)
	{
		CloseLoadoutMenu();
	}
}

void ANRHUD::OnRestartPressed()
{
	UNRRunSubsystem* Run = GetWorld() ? GetWorld()->GetSubsystem<UNRRunSubsystem>() : nullptr;
	if (Run && Run->GetTimeSinceEnded() >= RestartPromptDelay)
	{
		Run->RestartRun();
	}
}

bool ANRHUD::IsLoadoutMenuOpen() const
{
	return LoadoutWidget && LoadoutWidget->IsInViewport();
}

void ANRHUD::ToggleLoadoutMenu()
{
	if (IsLoadoutMenuOpen())
	{
		CloseLoadoutMenu();
	}
	else
	{
		OpenLoadoutMenu();
	}
}

void ANRHUD::OpenLoadoutMenu()
{
	if (!PlayerOwner)
	{
		return;
	}

	if (!LoadoutWidget)
	{
		const TSubclassOf<UNRLoadoutWidget> Class = LoadoutWidgetClass ? LoadoutWidgetClass : TSubclassOf<UNRLoadoutWidget>(UNRLoadoutWidget::StaticClass());
		LoadoutWidget = CreateWidget<UNRLoadoutWidget>(PlayerOwner, Class);
		if (!LoadoutWidget)
		{
			return;
		}
		LoadoutWidget->OnCloseRequested.BindUObject(this, &ANRHUD::CloseLoadoutMenu);
	}

	// Looked up on every open: after a respawn the parts belong to a different pawn.
	const APawn* Pawn = GetOwningPawn();
	LoadoutWidget->SetPartComponent(Pawn ? Pawn->FindComponentByClass<UNRWeaponPartComponent>() : nullptr);
	LoadoutWidget->AddToViewport(50);

	// GameAndUI so the world keeps running behind the menu. The widget swallows clicks and
	// look/move input is ignored, so the character neither fires nor turns while choosing.
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(LoadoutWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerOwner->SetInputMode(InputMode);
	PlayerOwner->SetShowMouseCursor(true);
	PlayerOwner->SetIgnoreLookInput(true);
	PlayerOwner->SetIgnoreMoveInput(true);
}

void ANRHUD::CloseLoadoutMenu()
{
	if (!IsLoadoutMenuOpen())
	{
		return;
	}

	LoadoutWidget->RemoveFromParent();
	if (PlayerOwner)
	{
		PlayerOwner->SetInputMode(FInputModeGameOnly());
		PlayerOwner->SetShowMouseCursor(false);
		PlayerOwner->SetIgnoreLookInput(false);
		PlayerOwner->SetIgnoreMoveInput(false);
	}
}

UNRWeaponComponent* ANRHUD::FindWeapon() const
{
	const APawn* Pawn = GetOwningPawn();
	return Pawn ? Pawn->FindComponentByClass<UNRWeaponComponent>() : nullptr;
}

FVector ANRHUD::GetOverheadLocation(const AActor* Actor) const
{
	// Capsule first: its top is stable, whereas mesh bounds grow and shrink with the pose
	// and would make the bar bob during attacks.
	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Actor->GetRootComponent()))
	{
		return Capsule->GetComponentLocation()
			+ FVector(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight() + HealthBarWorldOffset);
	}

	FVector Origin;
	FVector Extent;
	Actor->GetActorBounds(true, Origin, Extent);
	return Origin + FVector(0.0f, 0.0f, Extent.Z + HealthBarWorldOffset);
}

bool ANRHUD::ProjectToScreen(const FVector& WorldLocation, FVector2D& OutScreen) const
{
	if (!PlayerOwner || !PlayerOwner->PlayerCameraManager)
	{
		return false;
	}

	// Canvas::Project mirrors points behind the camera onto the screen, so reject those
	// against the view direction before projecting.
	const FVector CameraLocation = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const FVector CameraForward = PlayerOwner->PlayerCameraManager->GetCameraRotation().Vector();
	if (FVector::DotProduct(WorldLocation - CameraLocation, CameraForward) <= 0.0f)
	{
		return false;
	}

	const FVector Screen = Project(WorldLocation, false);
	OutScreen = FVector2D(Screen.X, Screen.Y);
	return true;
}

void ANRHUD::HandleAnyDamaged(UHealthComponent* Health, float Damage)
{
	if (!Health || Health->GetWorld() != GetWorld() || Damage <= 0.0f)
	{
		return;
	}

	const AActor* Target = Health->GetOwner();
	if (!Target || Target == GetOwningPawn())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	BarStates.FindOrAdd(Health).LastDamageTime = Now;

	// OnAnyDamaged fires before the dead flag latches, so the killing blow is the one that
	// leaves nothing behind.
	const bool bKillingHit = Health->GetCurrentHealth() <= 0.0f;

	if (DamageNumberMergeWindow > 0.0f)
	{
		for (int32 Index = DamageNumbers.Num() - 1; Index >= 0; --Index)
		{
			FDamageNumber& Existing = DamageNumbers[Index];
			if (Existing.Target == Health && Now - Existing.FirstHitTime <= DamageNumberMergeWindow)
			{
				Existing.Amount += Damage;
				Existing.StartTime = Now;
				Existing.bKillingHit |= bKillingHit;
				return;
			}
		}
	}

	FDamageNumber& Number = DamageNumbers.AddDefaulted_GetRef();
	Number.Target = Health;
	// Just under the bar so the number rises through it rather than starting above it.
	Number.WorldLocation = GetOverheadLocation(Target) - FVector(0.0f, 0.0f, HealthBarWorldOffset * 0.5f);
	Number.Amount = Damage;
	Number.StartTime = Now;
	Number.FirstHitTime = Now;
	Number.JitterX = FMath::FRandRange(-22.0f, 22.0f);
	Number.bKillingHit = bKillingHit;
}

void ANRHUD::DrawHealthBars()
{
	UWorld* World = GetWorld();
	if (!World || !PlayerOwner || !PlayerOwner->PlayerCameraManager)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	const FVector CameraLocation = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const APawn* OwningPawn = GetOwningPawn();

	for (const TWeakObjectPtr<UHealthComponent>& Entry : UHealthComponent::GetActiveComponents())
	{
		UHealthComponent* Health = Entry.Get();
		if (!Health || Health->GetWorld() != World || Health->IsDead())
		{
			continue;
		}

		const AActor* Target = Health->GetOwner();
		if (!Target || Target == OwningPawn || Target->IsHidden())
		{
			continue;
		}

		const FVector Anchor = GetOverheadLocation(Target);
		if (FVector::DistSquared(Anchor, CameraLocation) > FMath::Square(HealthBarMaxDistance))
		{
			continue;
		}

		if (bHealthBarRequiresLineOfSight)
		{
			// Static geometry only: an ally standing in front shouldn't hide an enemy's bar.
			FCollisionQueryParams Params(SCENE_QUERY_STAT(NRHealthBarSight), false, Target);
			Params.AddIgnoredActor(OwningPawn);
			FHitResult Hit;
			if (World->LineTraceSingleByObjectType(Hit, CameraLocation, Anchor,
				FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				continue;
			}
		}

		FVector2D Screen;
		if (!ProjectToScreen(Anchor, Screen))
		{
			continue;
		}

		// The lost chunk holds for a moment, then drains down to the real value. Healing
		// snaps it up so it never shows a loss that didn't happen.
		const float Fraction = Health->GetHealthNormalized();
		FHealthBarState& State = BarStates.FindOrAdd(Health);
		if (Fraction >= State.TrailFraction)
		{
			State.TrailFraction = Fraction;
		}
		else if (Now - State.LastDamageTime >= HealthBarTrailDelay)
		{
			State.TrailFraction = FMath::Max(Fraction, State.TrailFraction - HealthBarTrailDrainRate * RenderDelta);
		}

		const float Left = FMath::RoundToFloat(Screen.X - HealthBarWidth * 0.5f);
		const float Top = FMath::RoundToFloat(Screen.Y - HealthBarHeight * 0.5f);

		// One-pixel dark border, then trail, then fill on top.
		DrawRect(HealthBarBackColor, Left - 1.0f, Top - 1.0f, HealthBarWidth + 2.0f, HealthBarHeight + 2.0f);
		if (State.TrailFraction > Fraction)
		{
			DrawRect(HealthBarTrailColor, Left, Top, HealthBarWidth * State.TrailFraction, HealthBarHeight);
		}
		DrawRect(HealthBarFillColor, Left, Top, HealthBarWidth * Fraction, HealthBarHeight);
	}

	// Forget components that are gone so the map doesn't grow across respawns.
	for (auto It = BarStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void ANRHUD::DrawDamageNumbers()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;

	for (int32 Index = DamageNumbers.Num() - 1; Index >= 0; --Index)
	{
		const FDamageNumber& Number = DamageNumbers[Index];
		const float Age = Now - Number.StartTime;
		if (Age >= DamageNumberLifetime || Age < 0.0f)
		{
			DamageNumbers.RemoveAtSwap(Index);
			continue;
		}

		FVector2D Screen;
		if (!ProjectToScreen(Number.WorldLocation, Screen))
		{
			continue;
		}

		const float T = Age / DamageNumberLifetime;
		const bool bBig = Number.Amount >= BigHitThreshold;

		// Ease-out rise, a short pop on spawn, and a fade over the last 40%.
		const float Rise = DamageNumberRise * (1.0f - FMath::Square(1.0f - T));
		const float Pop = 1.0f + 0.45f * (1.0f - FMath::Clamp(Age / 0.12f, 0.0f, 1.0f));
		const float Alpha = T < 0.6f ? 1.0f : 1.0f - (T - 0.6f) / 0.4f;
		const float Scale = DamageNumberScale * (bBig ? 1.4f : 1.0f) * Pop;

		FLinearColor Color = Number.bKillingHit ? KillingHitColor : (bBig ? BigHitColor : DamageNumberColor);
		Color.A *= Alpha;

		FCanvasTextItem Text(FVector2D(Screen.X + Number.JitterX, Screen.Y - Rise),
			FText::AsNumber(FMath::RoundToInt(Number.Amount)), Font, Color);
		Text.Scale = FVector2D(Scale, Scale);
		Text.bCentreX = true;
		Text.bCentreY = true;
		Text.bOutlined = true;
		Text.OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.85f * Alpha);
		Canvas->DrawItem(Text);
	}
}

void ANRHUD::DrawRoomStatus()
{
	const APawn* Pawn = GetOwningPawn();
	const ANRRoom* Room = Pawn ? ANRRoom::FindRoomAt(GetWorld(), Pawn->GetActorLocation()) : nullptr;
	if (!Room)
	{
		return;
	}

	FString Label;
	FLinearColor Color = NeutralColor;
	float Scale = 1.4f;
	if (Room->GetRoomState() == ENRRoomState::Active)
	{
		Label = FString::Printf(TEXT("ENEMIES LEFT  %d"), Room->GetRemainingEnemies());
		Color = HostileColor;
	}
	else if (Room->GetRoomState() == ENRRoomState::Cleared && Room->GetTotalEnemies() > 0
		&& Room->GetTimeSinceCleared() < 3.0f)
	{
		// A banner for a moment, fading over its last second; then the room says nothing.
		Label = TEXT("ROOM CLEARED");
		Color = FLinearColor(0.4f, 1.0f, 0.5f, FMath::Clamp(3.0f - Room->GetTimeSinceCleared(), 0.0f, 1.0f));
		Scale = 2.0f;
	}
	if (Label.IsEmpty())
	{
		return;
	}

	float Width = 0.0f;
	float Height = 0.0f;
	GetTextSize(Label, Width, Height, nullptr, Scale);
	DrawText(Label, Color, (Canvas->SizeX - Width) * 0.5f, 40.0f, nullptr, Scale);
}

void ANRHUD::DrawRunStatus()
{
	const UNRRunSubsystem* Run = GetWorld() ? GetWorld()->GetSubsystem<UNRRunSubsystem>() : nullptr;
	if (!Run || Run->GetRunState() == ENRRunState::NoRun)
	{
		return;
	}

	const int32 Seconds = FMath::FloorToInt(Run->GetElapsedTime());
	const FString Panel = FString::Printf(TEXT("RUN  %02d:%02d    ROOMS %d/%d    KILLS %d"),
		Seconds / 60, Seconds % 60, Run->GetRoomsCleared(), Run->GetTotalRooms(), Run->GetKills());
	DrawText(Panel, NeutralColor, 24.0f, 20.0f, nullptr, 1.1f);

	if (Run->GetRunState() == ENRRunState::InProgress)
	{
		return;
	}

	const bool bWon = Run->GetRunState() == ENRRunState::Won;
	const FString Banner = bWon ? TEXT("RUN COMPLETE") : TEXT("RUN FAILED");
	const FLinearColor Color = bWon ? FLinearColor(0.4f, 1.0f, 0.5f, 1.0f) : HostileColor;
	float Width = 0.0f;
	float Height = 0.0f;
	GetTextSize(Banner, Width, Height, nullptr, 3.0f);
	const float BannerY = Canvas->SizeY * 0.38f;
	DrawText(Banner, Color, (Canvas->SizeX - Width) * 0.5f, BannerY, nullptr, 3.0f);

	if (Run->GetTimeSinceEnded() >= RestartPromptDelay)
	{
		const FString Prompt = FString::Printf(TEXT("[%s] Restart"), *RestartKey.GetDisplayName().ToString());
		float PromptWidth = 0.0f;
		float PromptHeight = 0.0f;
		GetTextSize(Prompt, PromptWidth, PromptHeight, nullptr, 1.4f);
		DrawText(Prompt, NeutralColor, (Canvas->SizeX - PromptWidth) * 0.5f, BannerY + Height + 12.0f, nullptr, 1.4f);
	}
}

void ANRHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	// World-space UI first so the crosshair always sits on top of it.
	DrawHealthBars();
	DrawDamageNumbers();
	DrawRoomStatus();
	DrawRunStatus();

	const UNRWeaponComponent* Weapon = FindWeapon();
	if (!Weapon)
	{
		return;
	}

	// Equipped weapon, bottom centre. Drawn even when the crosshair is hidden so a switch
	// made mid-vault is still confirmed on screen.
	FString WeaponLabel = FString::Printf(TEXT("[%d] %s"),
		Weapon->GetCurrentWeaponIndex() + 1, *Weapon->GetCurrentWeaponName().ToString());
	if (Weapon->IsReloading())
	{
		WeaponLabel += FString::Printf(TEXT("   RELOADING %d%%"), FMath::RoundToInt(Weapon->GetReloadProgress() * 100.0f));
	}
	else if (Weapon->GetMaxAmmo() > 0)
	{
		WeaponLabel += FString::Printf(TEXT("   %d / %d"), Weapon->GetCurrentAmmo(), Weapon->GetMaxAmmo());
	}
	float LabelWidth = 0.0f;
	float LabelHeight = 0.0f;
	GetTextSize(WeaponLabel, LabelWidth, LabelHeight, nullptr, WeaponLabelScale);
	DrawText(WeaponLabel, NeutralColor,
		(Canvas->SizeX - LabelWidth) * 0.5f, Canvas->SizeY - LabelHeight - WeaponLabelBottomMargin,
		nullptr, WeaponLabelScale);

	if (!Weapon->ShouldShowCrosshair() || IsLoadoutMenuOpen())
	{
		return;
	}

	// Canvas centre is the same screen point the weapon traces through, so no offset or
	// calibration is needed to make the two agree.
	const float CenterX = Canvas->SizeX * 0.5f;
	const float CenterY = Canvas->SizeY * 0.5f;

	const float Gap = BaseGap + Weapon->GetCrosshairSpread() * MaxSpreadPixels;
	const FLinearColor Color = Weapon->IsAimingAtHostile() ? HostileColor : NeutralColor;

	const float Inner = Gap;
	const float Outer = Gap + LineLength;

	DrawLine(CenterX - Outer, CenterY, CenterX - Inner, CenterY, Color, LineThickness);
	DrawLine(CenterX + Inner, CenterY, CenterX + Outer, CenterY, Color, LineThickness);
	DrawLine(CenterX, CenterY - Outer, CenterX, CenterY - Inner, Color, LineThickness);
	DrawLine(CenterX, CenterY + Inner, CenterX, CenterY + Outer, Color, LineThickness);

	if (bDrawCenterDot)
	{
		DrawRect(Color, CenterX - 1.0f, CenterY - 1.0f, 2.0f, 2.0f);
	}
}
