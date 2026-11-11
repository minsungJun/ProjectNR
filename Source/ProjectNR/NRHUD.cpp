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
#include "NRConsumableComponent.h"
#include "NRInteractable.h"
#include "NRInteractionComponent.h"
#include "NRStaminaComponent.h"
#include "NRStatsComponent.h"
#include "NRLoadoutWidget.h"
#include "NRShop.h"
#include "NRShopWidget.h"
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
	CloseShopMenu();
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
		CloseShopMenu();
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
	CloseShopMenu();

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

	EnterMenuInput(LoadoutWidget);
}

void ANRHUD::EnterMenuInput(UUserWidget* Widget)
{
	if (!PlayerOwner || !Widget)
	{
		return;
	}
	// GameAndUI so the world keeps running behind the menu. The widget swallows clicks and
	// look/move input is ignored, so the character neither fires nor turns while choosing.
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(Widget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerOwner->SetInputMode(InputMode);
	PlayerOwner->SetShowMouseCursor(true);
	PlayerOwner->SetIgnoreLookInput(true);
	PlayerOwner->SetIgnoreMoveInput(true);
}

void ANRHUD::LeaveMenuInput()
{
	if (PlayerOwner)
	{
		PlayerOwner->SetInputMode(FInputModeGameOnly());
		PlayerOwner->SetShowMouseCursor(false);
		PlayerOwner->SetIgnoreLookInput(false);
		PlayerOwner->SetIgnoreMoveInput(false);
	}
}

bool ANRHUD::IsShopMenuOpen() const
{
	return ShopWidget && ShopWidget->IsInViewport();
}

void ANRHUD::OpenShopMenu(ANRShop* Shop)
{
	if (!PlayerOwner || !Shop)
	{
		return;
	}
	CloseLoadoutMenu();
	if (!ShopWidget)
	{
		ShopWidget = CreateWidget<UNRShopWidget>(PlayerOwner, UNRShopWidget::StaticClass());
		if (!ShopWidget)
		{
			return;
		}
		ShopWidget->OnCloseRequested.BindUObject(this, &ANRHUD::CloseShopMenu);
	}
	ShopWidget->SetShop(Shop);
	if (!ShopWidget->IsInViewport())
	{
		ShopWidget->AddToViewport(50);
		EnterMenuInput(ShopWidget);
	}
}

void ANRHUD::CloseShopMenu()
{
	if (!IsShopMenuOpen())
	{
		return;
	}
	ShopWidget->RemoveFromParent();
	ShopWidget->SetShop(nullptr);
	LeaveMenuInput();
}

void ANRHUD::CloseLoadoutMenu()
{
	if (!IsLoadoutMenuOpen())
	{
		return;
	}

	LoadoutWidget->RemoveFromParent();
	LeaveMenuInput();
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
	if (Room->GetRoomState() == ENRRoomState::Active && Room->IsSpawning())
	{
		Label = TEXT("ENEMIES INCOMING");
		Color = HostileColor;
	}
	else if (Room->GetRoomState() == ENRRoomState::Active)
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

namespace NRHudStyle
{
	const FLinearColor Bright(0.95f, 0.97f, 1.0f, 0.95f);
	const FLinearColor Dim(0.72f, 0.78f, 0.82f, 0.85f);
	const FLinearColor Panel(0.02f, 0.04f, 0.05f, 0.45f);
	const FLinearColor Edge(0.85f, 0.9f, 0.95f, 0.35f);
	const FLinearColor Track(0.0f, 0.0f, 0.0f, 0.45f);
	const FLinearColor Fill(0.88f, 0.9f, 0.92f, 0.95f);
	const FLinearColor Warning(1.0f, 0.55f, 0.25f, 0.95f);
	const FLinearColor Danger(0.95f, 0.3f, 0.25f, 0.95f);
	const FLinearColor Shield(0.55f, 0.82f, 1.0f, 0.95f);
	const FLinearColor Stamina(1.0f, 0.88f, 0.4f, 0.95f);
	const FLinearColor Gold(1.0f, 0.78f, 0.1f, 1.0f);

	/** Draws Text with a soft shadow; Anchor 0 = left edge at X, 1 = right edge, 0.5 = centred. */
	FVector2D DrawLabel(UCanvas* Canvas, UFont* Font, const FString& Text, float X, float CenterY, float Scale,
		const FLinearColor& Color, float Anchor = 0.0f)
	{
		float Width = 0.0f;
		float Height = 0.0f;
		Canvas->TextSize(Font, Text, Width, Height, Scale, Scale);
		FCanvasTextItem Item(FVector2D(X - Width * Anchor, CenterY - Height * 0.5f), FText::FromString(Text), Font, Color);
		Item.Scale = FVector2D(Scale, Scale);
		Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
		Canvas->DrawItem(Item);
		return FVector2D(Width, Height);
	}

	void DrawFrame(AHUD* HUD, float X, float Y, float W, float H, const FLinearColor& Color, float T)
	{
		HUD->DrawRect(Color, X, Y, W, T);
		HUD->DrawRect(Color, X, Y + H - T, W, T);
		HUD->DrawRect(Color, X, Y + T, T, H - 2.0f * T);
		HUD->DrawRect(Color, X + W - T, Y + T, T, H - 2.0f * T);
	}
}

void ANRHUD::DrawPlayerStatus()
{
	// Bottom left, after the reference layout: a plus, the number, a light bar, the shield bar
	// under it, stamina below that, and the consumables and level above. Sized from the canvas
	// height so it keeps its proportions at any resolution.
	const APawn* Pawn = GetOwningPawn();
	const UHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Health || !Font)
	{
		return;
	}

	const float U = Canvas->SizeY / 1080.0f;
	const float X = 40.0f * U;
	const float Y = Canvas->SizeY - 110.0f * U;
	const float Fraction = Health->GetHealthNormalized();
	const FLinearColor BarColor = Fraction <= 0.3f ? NRHudStyle::Danger : NRHudStyle::Fill;

	// Plus sign.
	DrawRect(NRHudStyle::Bright, X, Y - 3.5f * U, 26.0f * U, 7.0f * U);
	DrawRect(NRHudStyle::Bright, X + 9.5f * U, Y - 13.0f * U, 7.0f * U, 26.0f * U);

	NRHudStyle::DrawLabel(Canvas, Font, FString::FromInt(FMath::CeilToInt(Health->GetCurrentHealth())),
		X + 38.0f * U, Y, 2.0f * U, NRHudStyle::Bright);

	const float BarX = X + 110.0f * U;
	const float BarW = 240.0f * U;
	const float BarH = 12.0f * U;
	// The track runs a little past the fill on every side so a full, light bar still has a dark
	// edge against a light floor.
	const float Pad = 2.0f * U;
	DrawRect(NRHudStyle::Track, BarX - Pad, Y - BarH * 0.5f - Pad, BarW + 2.0f * Pad, BarH + 2.0f * Pad);
	DrawRect(BarColor, BarX, Y - BarH * 0.5f, BarW * Fraction, BarH);

	// Shield: a thinner bar under health, empty until a shield is used.
	const float ShieldY = Y + BarH * 0.5f + 8.0f * U;
	const float ShieldH = 6.0f * U;
	const float MaxShield = Health->GetMaxShield();
	const float Shield = Health->GetCurrentShield();
	UFont* Small = GEngine->GetMediumFont();
	DrawRect(NRHudStyle::Track, BarX - Pad, ShieldY - Pad, BarW + 2.0f * Pad, ShieldH + 2.0f * Pad);
	if (MaxShield > 0.0f && Shield > 0.0f)
	{
		DrawRect(NRHudStyle::Shield, BarX, ShieldY, BarW * FMath::Clamp(Shield / MaxShield, 0.0f, 1.0f), ShieldH);
		if (Small)
		{
			NRHudStyle::DrawLabel(Canvas, Small, FString::FromInt(FMath::CeilToInt(Shield)),
				BarX + BarW + 10.0f * U, ShieldY + ShieldH * 0.5f, 0.9f * U, NRHudStyle::Shield);
		}
	}

	// Stamina: a bolt and a shorter bar under the shield, with the number after it.
	if (const UNRStaminaComponent* Stamina = Pawn->FindComponentByClass<UNRStaminaComponent>())
	{
		const float StaminaY = ShieldY + ShieldH + 26.0f * U;
		const float BoltX = X + 6.0f * U;
		const float Thick = FMath::Max(1.5f, 3.0f * U);
		DrawLine(BoltX + 14.0f * U, StaminaY - 12.0f * U, BoltX + 4.0f * U, StaminaY + 1.0f * U, NRHudStyle::Bright, Thick);
		DrawLine(BoltX + 4.0f * U, StaminaY + 1.0f * U, BoltX + 13.0f * U, StaminaY + 1.0f * U, NRHudStyle::Bright, Thick);
		DrawLine(BoltX + 13.0f * U, StaminaY + 1.0f * U, BoltX + 3.0f * U, StaminaY + 14.0f * U, NRHudStyle::Bright, Thick);

		const float MaxStamina = Stamina->GetMaxStamina();
		const float StaminaFraction = MaxStamina > 0.0f ? FMath::Clamp(Stamina->GetCurrentStamina() / MaxStamina, 0.0f, 1.0f) : 0.0f;
		const float StaminaW = 170.0f * U;
		const float StaminaH = 8.0f * U;
		DrawRect(NRHudStyle::Track, BarX - Pad, StaminaY - StaminaH * 0.5f - Pad, StaminaW + 2.0f * Pad, StaminaH + 2.0f * Pad);
		DrawRect(StaminaFraction < 0.3f ? NRHudStyle::Warning : NRHudStyle::Stamina,
			BarX, StaminaY - StaminaH * 0.5f, StaminaW * StaminaFraction, StaminaH);
		if (Small)
		{
			NRHudStyle::DrawLabel(Canvas, Small, FString::Printf(TEXT("%d / %d"),
				FMath::FloorToInt(Stamina->GetCurrentStamina()), FMath::RoundToInt(MaxStamina)),
				BarX + StaminaW + 10.0f * U, StaminaY, 0.9f * U, NRHudStyle::Dim);
		}
	}

	// Consumables: a key box and the count for each, above the health row.
	const UNRConsumableComponent* Consumables = Pawn->FindComponentByClass<UNRConsumableComponent>();
	if (Consumables && Small)
	{
		const float Box = 24.0f * U;
		const float RowY = Y - 44.0f * U;
		float ItemX = X;
		auto DrawItem = [&](const FKey& Key, const TCHAR* Label, int32 Count)
		{
			const FLinearColor Color = Count > 0 ? NRHudStyle::Bright : NRHudStyle::Dim * FLinearColor(1.0f, 1.0f, 1.0f, 0.6f);
			DrawRect(NRHudStyle::Panel, ItemX, RowY - Box * 0.5f, Box, Box);
			NRHudStyle::DrawFrame(this, ItemX, RowY - Box * 0.5f, Box, Box, NRHudStyle::Edge, FMath::Max(1.0f, 1.5f * U));
			NRHudStyle::DrawLabel(Canvas, Small, Key.GetDisplayName().ToString(), ItemX + Box * 0.5f, RowY, 0.9f * U, Color, 0.5f);
			const FVector2D Size = NRHudStyle::DrawLabel(Canvas, Small, FString::Printf(TEXT("%s x%d"), Label, Count),
				ItemX + Box + 8.0f * U, RowY, 0.9f * U, Color);
			ItemX += Box + 8.0f * U + Size.X + 22.0f * U;
		};
		DrawItem(Consumables->GetPotionKey(), TEXT("HEAL"), Consumables->GetPotionCount());
		DrawItem(Consumables->GetShieldKey(), TEXT("SHIELD"), Consumables->GetShieldCount());
	}

	// Level and experience above the consumables, with a reminder while points wait in the
	// Tab menu.
	const UNRStatsComponent* Stats = Pawn->FindComponentByClass<UNRStatsComponent>();
	if (Stats && Small)
	{
		const float LevelY = Y - 80.0f * U;
		const FVector2D LevelSize = NRHudStyle::DrawLabel(Canvas, Small, FString::Printf(TEXT("LV %d"), Stats->GetLevel()),
			X, LevelY, 1.0f * U, NRHudStyle::Bright);
		const float XpX = X + LevelSize.X + 12.0f * U;
		const float XpW = 140.0f * U;
		const float XpH = 4.0f * U;
		const int32 Needed = Stats->GetExperienceToNextLevel();
		DrawRect(NRHudStyle::Track, XpX, LevelY - XpH * 0.5f, XpW, XpH);
		DrawRect(NRHudStyle::Shield, XpX, LevelY - XpH * 0.5f,
			XpW * (Needed > 0 ? FMath::Clamp(static_cast<float>(Stats->GetExperience()) / Needed, 0.0f, 1.0f) : 0.0f), XpH);
		if (Stats->GetUnspentPoints() > 0)
		{
			NRHudStyle::DrawLabel(Canvas, Small, FString::Printf(TEXT("+%d STAT  [Tab]"), Stats->GetUnspentPoints()),
				XpX + XpW + 12.0f * U, LevelY, 0.9f * U, NRHudStyle::Gold);
		}
	}
}

void ANRHUD::DrawWeaponPanel(const UNRWeaponComponent* Weapon)
{
	// Bottom right, after the reference layout: a framed, translucent panel with the rounds
	// in the magazine large, the magazine size small, and the weapon on the right.
	UFont* Large = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* Medium = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Weapon || !Large || !Medium)
	{
		return;
	}

	const float U = Canvas->SizeY / 1080.0f;
	const float W = 320.0f * U;
	const float H = 80.0f * U;
	const float X = Canvas->SizeX - 110.0f * U - W;
	const float Y = Canvas->SizeY - 95.0f * U - H;
	const float MidY = Y + H * 0.46f;
	const float Line = FMath::Max(1.0f, 1.5f * U);

	DrawRect(NRHudStyle::Panel, X, Y, W, H);
	NRHudStyle::DrawFrame(this, X, Y, W, H, NRHudStyle::Edge, Line);

	// Divider between the ammo and the weapon, as in the reference.
	const float DividerX = X + W * 0.5f;
	DrawRect(NRHudStyle::Edge, DividerX, Y + 12.0f * U, Line, H - 24.0f * U);

	const int32 MaxAmmo = Weapon->GetMaxAmmo();
	const float UnderlineX = X + 18.0f * U;
	const float UnderlineY = Y + H - 16.0f * U;
	const float UnderlineW = DividerX - UnderlineX - 18.0f * U;

	if (MaxAmmo <= 0)
	{
		// Melee: no rounds to count.
		NRHudStyle::DrawLabel(Canvas, Large, TEXT("--"), UnderlineX, MidY, 2.0f * U, NRHudStyle::Dim);
	}
	else if (Weapon->IsReloading())
	{
		NRHudStyle::DrawLabel(Canvas, Medium, TEXT("RELOADING"), UnderlineX, MidY, 1.2f * U, NRHudStyle::Dim);
		DrawRect(NRHudStyle::Track, UnderlineX, UnderlineY, UnderlineW, 3.0f * U);
		DrawRect(NRHudStyle::Fill, UnderlineX, UnderlineY, UnderlineW * Weapon->GetReloadProgress(), 3.0f * U);
	}
	else if (Weapon->IsOutOfAmmo())
	{
		// Nothing in the magazine or in reserve: ammo has to be picked up.
		NRHudStyle::DrawLabel(Canvas, Medium, TEXT("NO AMMO"), UnderlineX, MidY, 1.3f * U, NRHudStyle::Danger);
		DrawRect(NRHudStyle::Danger, UnderlineX, UnderlineY, UnderlineW, 3.0f * U);
	}
	else
	{
		const int32 Ammo = Weapon->GetCurrentAmmo();
		const int32 Reserve = Weapon->GetReserveAmmo();
		const float AmmoFraction = static_cast<float>(Ammo) / MaxAmmo;
		const FLinearColor AmmoColor = Ammo == 0 ? NRHudStyle::Danger
			: (AmmoFraction <= 0.25f ? NRHudStyle::Warning : NRHudStyle::Bright);

		const FVector2D AmmoSize = NRHudStyle::DrawLabel(Canvas, Large, FString::FromInt(Ammo), UnderlineX, MidY, 2.3f * U, AmmoColor);
		// Small separator, then the rounds in reserve (red once it is empty).
		const float SepX = UnderlineX + AmmoSize.X + 12.0f * U;
		DrawRect(NRHudStyle::Edge, SepX, MidY - 12.0f * U, Line, 24.0f * U);
		NRHudStyle::DrawLabel(Canvas, Medium, FString::FromInt(Reserve), SepX + 12.0f * U, MidY, 1.3f * U,
			Reserve > 0 ? NRHudStyle::Dim : NRHudStyle::Danger);

		// The short line under the count, as long as the magazine is full.
		DrawRect(NRHudStyle::Track, UnderlineX, UnderlineY, UnderlineW, 3.0f * U);
		DrawRect(AmmoColor, UnderlineX, UnderlineY, UnderlineW * AmmoFraction, 3.0f * U);
	}

	// Gold for the run, just above the panel's right edge.
	if (const UNRRunSubsystem* Run = GetWorld() ? GetWorld()->GetSubsystem<UNRRunSubsystem>() : nullptr)
	{
		NRHudStyle::DrawLabel(Canvas, Medium, FString::Printf(TEXT("GOLD  %d"), Run->GetMoney()), X + W, Y - 18.0f * U, 1.1f * U, NRHudStyle::Gold, 1.0f);
	}

	// Weapon: no icons yet, so its name and slot stand where the reference has a silhouette.
	const FString Name = Weapon->GetCurrentWeaponName().ToString().ToUpper();
	NRHudStyle::DrawLabel(Canvas, Medium, Name, X + W - 18.0f * U, Y + H * 0.42f, 1.3f * U, NRHudStyle::Bright, 1.0f);
	NRHudStyle::DrawLabel(Canvas, Medium, FString::Printf(TEXT("[%d]"), Weapon->GetCurrentWeaponIndex() + 1),
		X + W - 18.0f * U, Y + H * 0.74f, 0.9f * U, NRHudStyle::Dim, 1.0f);
}

void ANRHUD::DrawInteractPrompt()
{
	const APawn* Pawn = GetOwningPawn();
	const UNRInteractionComponent* Interaction = Pawn ? Pawn->FindComponentByClass<UNRInteractionComponent>() : nullptr;
	const INRInteractable* Interactable = Interaction ? Cast<INRInteractable>(Interaction->GetTarget()) : nullptr;
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	FVector2D Screen;
	if (!Interactable || !Font || !ProjectToScreen(Interactable->GetInteractLocation(), Screen))
	{
		return;
	}

	// A key box and the verb, centred on the target's prompt point.
	const float U = Canvas->SizeY / 1080.0f;
	const float Box = 30.0f * U;
	const float Gap = 10.0f * U;
	const float Scale = 1.1f * U;
	const FString Verb = Interactable->GetInteractPrompt().ToString();
	float VerbWidth = 0.0f;
	float VerbHeight = 0.0f;
	Canvas->TextSize(Font, Verb, VerbWidth, VerbHeight, Scale, Scale);

	const float PadX = 12.0f * U;
	const float PanelW = Box + Gap + VerbWidth + PadX * 2.0f;
	const float PanelH = Box + 12.0f * U;
	const float X = Screen.X - PanelW * 0.5f;
	const float Y = Screen.Y - PanelH * 0.5f;
	const float Line = FMath::Max(1.0f, 1.5f * U);

	DrawRect(NRHudStyle::Panel, X, Y, PanelW, PanelH);
	NRHudStyle::DrawFrame(this, X, Y, PanelW, PanelH, NRHudStyle::Edge, Line);

	const float BoxX = X + PadX;
	const float BoxY = Y + (PanelH - Box) * 0.5f;
	DrawRect(NRHudStyle::Bright, BoxX, BoxY, Box, Box);
	NRHudStyle::DrawLabel(Canvas, Font, Interaction->GetInteractKey().GetDisplayName().ToString(),
		BoxX + Box * 0.5f, BoxY + Box * 0.5f, Scale, FLinearColor(0.02f, 0.03f, 0.04f, 1.0f), 0.5f);
	NRHudStyle::DrawLabel(Canvas, Font, Verb, BoxX + Box + Gap, Y + PanelH * 0.5f, Scale, NRHudStyle::Bright);
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
	DrawPlayerStatus();
	if (!IsLoadoutMenuOpen())
	{
		DrawInteractPrompt();
	}

	const UNRWeaponComponent* Weapon = FindWeapon();
	if (!Weapon)
	{
		return;
	}

	// Ammo and weapon, bottom right. Drawn even when the crosshair is hidden so a switch made
	// mid-vault is still confirmed on screen.
	DrawWeaponPanel(Weapon);

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
