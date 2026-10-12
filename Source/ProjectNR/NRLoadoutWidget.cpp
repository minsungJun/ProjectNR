// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRLoadoutWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "NRWeaponPartComponent.h"
#include "NRWeaponPartData.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "NRStatsComponent.h"
#include "NRStatsPanel.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"

namespace NRLoadoutStyle
{
	const FLinearColor Backdrop(0.0f, 0.0f, 0.0f, 0.55f);
	const FLinearColor Panel(0.015f, 0.06f, 0.07f, 0.94f);
	const FLinearColor PanelEdge(0.1f, 0.55f, 0.6f, 1.0f);
	const FLinearColor Cell(0.02f, 0.09f, 0.1f, 1.0f);
	const FLinearColor CellHover(0.05f, 0.2f, 0.22f, 1.0f);
	const FLinearColor Common(0.1f, 0.85f, 0.9f, 1.0f);
	const FLinearColor Exclusive(1.0f, 0.78f, 0.1f, 1.0f);
	const FLinearColor Selected(1.0f, 0.92f, 0.25f, 1.0f);
	const FLinearColor EmptyEdge(0.1f, 0.3f, 0.32f, 1.0f);
	const FLinearColor DropOk(0.25f, 1.0f, 0.35f, 1.0f);
	const FLinearColor DropBad(1.0f, 0.25f, 0.2f, 1.0f);
	const FLinearColor TextColor(0.85f, 0.97f, 1.0f, 1.0f);
	const FLinearColor Dim(0.45f, 0.62f, 0.65f, 1.0f);
	const FLinearColor Level(1.0f, 0.85f, 0.35f, 1.0f);
	const FLinearColor Error(1.0f, 0.4f, 0.35f, 1.0f);

	constexpr float CellSize = 64.0f;
	constexpr int32 GridColumns = 5;
	constexpr int32 MinGridRows = 6;

	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, FLinearColor Color, bool bBold = true)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Block->SetText(Text);
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
		return Block;
	}
}

// --- UNRLoadoutCell ------------------------------------------------------------------

void UNRLoadoutCell::Configure(UNRLoadoutWidget* InOwner, const FConfig& InConfig)
{
	Owner = InOwner;
	Config = InConfig;
	// A user widget is self-hit-test-invisible by default; the cell must receive presses and
	// drops itself. The drag visual overrides this afterwards.
	SetVisibility(ESlateVisibility::Visible);
}

void UNRLoadoutCell::SetOutlineColor(const FLinearColor& Color)
{
	if (OutlineBorder)
	{
		OutlineBorder->SetBrushColor(Color);
	}
}

TSharedRef<SWidget> UNRLoadoutCell::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetWidthOverride(NRLoadoutStyle::CellSize);
		Size->SetHeightOverride(NRLoadoutStyle::CellSize);

		// Outline shows through the padding around the dark inner square. 4px rather than 2 so
		// it survives DPI scaling in a small PIE viewport (about x0.5), where thinner edges
		// round down to nothing on some sides.
		OutlineBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		OutlineBorder->SetBrushColor(Config.Outline);
		OutlineBorder->SetPadding(FMargin(4.0f));
		Size->SetContent(OutlineBorder);

		InnerBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		InnerBorder->SetBrushColor(NRLoadoutStyle::Cell);
		InnerBorder->SetHorizontalAlignment(HAlign_Center);
		InnerBorder->SetVerticalAlignment(VAlign_Center);
		OutlineBorder->SetContent(InnerBorder);

		UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		if (UTexture2D* Icon = Config.Icon.Get())
		{
			UImage* Image = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			Image->SetBrushFromTexture(Icon);
			Image->SetDesiredSizeOverride(FVector2D(36.0f, 36.0f));
			Content->AddChildToVerticalBox(Image)->SetHorizontalAlignment(HAlign_Center);
		}
		else if (!Config.Label.IsEmpty())
		{
			UTextBlock* Label = NRLoadoutStyle::MakeText(WidgetTree, Config.Label, 12, Config.LabelColor);
			Label->SetJustification(ETextJustify::Center);
			Content->AddChildToVerticalBox(Label)->SetHorizontalAlignment(HAlign_Center);
		}
		if (!Config.Level.IsEmpty())
		{
			Content->AddChildToVerticalBox(NRLoadoutStyle::MakeText(WidgetTree, Config.Level, 11, NRLoadoutStyle::Level))
				->SetHorizontalAlignment(HAlign_Center);
		}
		InnerBorder->SetContent(Content);

		WidgetTree->RootWidget = Size;
		if (!Config.Tooltip.IsEmpty())
		{
			SetToolTipText(Config.Tooltip);
		}
	}
	return Super::RebuildWidget();
}

FReply UNRLoadoutCell::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (UNRLoadoutWidget* OwnerWidget = Owner.Get())
		{
			OwnerWidget->HandleCellRightClicked(this);
		}
		return FReply::Handled();
	}
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Handled();
	}

	bPressed = true;
	// Only a cell holding a part can start a drag; Slate calls NativeOnDragDetected once the
	// cursor has moved far enough, and otherwise the release below is a click.
	if (Config.PartId.IsValid() && GetCachedWidget().IsValid())
	{
		return FReply::Handled().DetectDrag(GetCachedWidget().ToSharedRef(), EKeys::LeftMouseButton);
	}
	return FReply::Handled();
}

FReply UNRLoadoutCell::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bPressed)
	{
		bPressed = false;
		if (UNRLoadoutWidget* OwnerWidget = Owner.Get())
		{
			OwnerWidget->HandleCellClicked(this);
		}
	}
	return FReply::Handled();
}

void UNRLoadoutCell::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	if (InnerBorder)
	{
		InnerBorder->SetBrushColor(NRLoadoutStyle::CellHover);
	}
}

void UNRLoadoutCell::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bPressed = false;
	if (InnerBorder)
	{
		InnerBorder->SetBrushColor(NRLoadoutStyle::Cell);
	}
}

void UNRLoadoutCell::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	bPressed = false;
	UNRLoadoutWidget* OwnerWidget = Owner.Get();
	OutOperation = OwnerWidget ? OwnerWidget->HandleCellDragStarted(this) : nullptr;
}

void UNRLoadoutCell::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	const UNRPartDragOperation* PartDrag = Cast<UNRPartDragOperation>(InOperation);
	UNRLoadoutWidget* OwnerWidget = Owner.Get();
	if (PartDrag && OwnerWidget)
	{
		// Over a slot the drop goes to that slot, so the card around it must not look like
		// the target too.
		OwnerWidget->ClearCardHighlight();
		SetOutlineColor(OwnerWidget->CanDropOnCell(this, PartDrag) ? NRLoadoutStyle::DropOk : NRLoadoutStyle::DropBad);
	}
}

void UNRLoadoutCell::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	RestoreOutlineColor();
}

bool UNRLoadoutCell::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	return true;
}

bool UNRLoadoutCell::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	RestoreOutlineColor();
	UNRPartDragOperation* PartDrag = Cast<UNRPartDragOperation>(InOperation);
	UNRLoadoutWidget* OwnerWidget = Owner.Get();
	return PartDrag && OwnerWidget && OwnerWidget->HandleCellDrop(this, PartDrag);
}

// --- UNRLoadoutWidget ----------------------------------------------------------------

void UNRLoadoutWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Takes keyboard focus for Tab/Escape, and is hit-testable itself so clicks on the
	// backdrop stop here instead of reaching the game as a shot.
	SetIsFocusable(true);
	SetVisibility(ESlateVisibility::Visible);
}

void UNRLoadoutWidget::NativeDestruct()
{
	UnbindFromParts();
	UnbindFromStats();
	// Closing the menu drops whatever was staged on the stat page.
	if (StatsPanel)
	{
		StatsPanel->ResetStaged();
	}
	Super::NativeDestruct();
}

TSharedRef<SWidget> UNRLoadoutWidget::RebuildWidget()
{
	// No widget asset: the tree is built here, the first time Slate asks for it.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
		RootBorder->SetBrushColor(NRLoadoutStyle::Backdrop);
		RootBorder->SetHorizontalAlignment(HAlign_Center);
		RootBorder->SetVerticalAlignment(VAlign_Center);
		WidgetTree->RootWidget = RootBorder;
		Refresh();
	}
	return Super::RebuildWidget();
}

FReply UNRLoadoutWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Handled here because Slate would otherwise spend Tab on focus navigation and the
	// game would never see it.
	if (InKeyEvent.GetKey() == EKeys::Tab || InKeyEvent.GetKey() == EKeys::Escape)
	{
		OnCloseRequested.ExecuteIfBound();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UNRLoadoutWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Swallow clicks that land between cells: in GameAndUI mode they would go on to the
	// viewport and fire the weapon.
	return FReply::Handled();
}

bool UNRLoadoutWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// Reaches here only when no cell took the drop: a weapon card outside its slots, a gap in
	// the grid, or outside every panel.
	ClearCardHighlight();
	UNRPartDragOperation* PartDrag = Cast<UNRPartDragOperation>(InOperation);
	if (!PartDrag)
	{
		return true;
	}

	const FVector2D Position = InDragDropEvent.GetScreenSpacePosition();
	const int32 Card = FindCardUnder(Position);
	if (Card != INDEX_NONE)
	{
		DropOnWeapon(PartDrag, static_cast<ENRWeaponId>(Card));
	}
	else if (PartDrag->bFromSlot && InventoryPanel && InventoryPanel->GetCachedGeometry().IsUnderLocation(Position))
	{
		DropOnInventory(PartDrag);
	}
	// Anywhere else the drag is simply dropped: nothing changes.
	return true;
}

bool UNRLoadoutWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	const UNRPartDragOperation* PartDrag = Cast<UNRPartDragOperation>(InOperation);
	const int32 Card = PartDrag ? FindCardUnder(InDragDropEvent.GetScreenSpacePosition()) : INDEX_NONE;
	SetHighlightedCard(Card, Card != INDEX_NONE && FindAutoSlot(PartDrag, static_cast<ENRWeaponId>(Card)) != INDEX_NONE);
	return true;
}

void UNRLoadoutWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	ClearCardHighlight();
}

int32 UNRLoadoutWidget::FindCardUnder(const FVector2D& ScreenPosition) const
{
	for (int32 Index = 0; Index < CardEdges.Num(); ++Index)
	{
		if (CardEdges[Index] && CardEdges[Index]->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UNRLoadoutWidget::SetHighlightedCard(int32 WeaponIndex, bool bCanFit)
{
	if (WeaponIndex == HighlightedCard && bCanFit == bHighlightCanFit)
	{
		return;
	}

	if (CardEdges.IsValidIndex(HighlightedCard) && CardEdges[HighlightedCard])
	{
		CardEdges[HighlightedCard]->SetBrushColor(NRLoadoutStyle::PanelEdge);
	}
	HighlightedCard = WeaponIndex;
	bHighlightCanFit = bCanFit;
	if (CardEdges.IsValidIndex(HighlightedCard) && CardEdges[HighlightedCard])
	{
		CardEdges[HighlightedCard]->SetBrushColor(bCanFit ? NRLoadoutStyle::DropOk : NRLoadoutStyle::DropBad);
	}
}

void UNRLoadoutWidget::SetPartComponent(UNRWeaponPartComponent* InParts)
{
	if (Parts.Get() == InParts)
	{
		RequestRefresh();
		return;
	}

	UnbindFromParts();
	Parts = InParts;
	SelectedPart.Invalidate();

	if (InParts)
	{
		InParts->OnInventoryChanged.AddUniqueDynamic(this, &UNRLoadoutWidget::HandleInventoryChanged);
		InParts->OnLoadoutChanged.AddUniqueDynamic(this, &UNRLoadoutWidget::HandleLoadoutChanged);
		InParts->OnRuntimeStatsChanged.AddUniqueDynamic(this, &UNRLoadoutWidget::HandleRuntimeStatsChanged);
	}
	RequestRefresh();
}

void UNRLoadoutWidget::UnbindFromParts()
{
	if (UNRWeaponPartComponent* Old = Parts.Get())
	{
		Old->OnInventoryChanged.RemoveDynamic(this, &UNRLoadoutWidget::HandleInventoryChanged);
		Old->OnLoadoutChanged.RemoveDynamic(this, &UNRLoadoutWidget::HandleLoadoutChanged);
		Old->OnRuntimeStatsChanged.RemoveDynamic(this, &UNRLoadoutWidget::HandleRuntimeStatsChanged);
	}
	Parts = nullptr;
}

void UNRLoadoutWidget::HandleInventoryChanged()
{
	RequestRefresh();
}

void UNRLoadoutWidget::HandleLoadoutChanged(ENRWeaponId WeaponId)
{
	RequestRefresh();
}

void UNRLoadoutWidget::HandleRuntimeStatsChanged(ENRWeaponId WeaponId, const FNRWeaponRuntimeStats& Stats)
{
	RequestRefresh();
}

void UNRLoadoutWidget::RequestRefresh()
{
	// Batched to the next frame: one equip fires several events, and the cell that was
	// clicked or dropped on must outlive its own event.
	UWorld* World = GetWorld();
	if (!World)
	{
		Refresh();
		return;
	}
	if (bRefreshPending)
	{
		return;
	}
	bRefreshPending = true;
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		bRefreshPending = false;
		Refresh();
	}));
}

void UNRLoadoutWidget::SetStatus(const FText& Text, bool bError)
{
	StatusText = Text;
	bStatusIsError = bError;
}

FString UNRLoadoutWidget::DescribePart(const FGuid& PartId) const
{
	const UNRWeaponPartComponent* PartComponent = Parts.Get();
	const FNRWeaponPartInstance* Part = PartComponent ? PartComponent->FindPart(PartId) : nullptr;
	return Part && Part->PartData
		? FString::Printf(TEXT("%s %s"), *Part->PartData->DisplayName.ToString(), *UNRWeaponPartData::GetLevelText(Part->Level).ToString())
		: FString();
}

// --- Building ------------------------------------------------------------------------

void UNRLoadoutWidget::ShowStatsPage(bool bStats)
{
	const EPage Wanted = bStats ? EPage::Stats : EPage::Parts;
	if (ActivePage != Wanted)
	{
		ActivePage = Wanted;
		RequestRefresh();
	}
}

void UNRLoadoutWidget::UnbindFromStats()
{
	if (UNRStatsComponent* Old = BoundStats.Get())
	{
		Old->OnStatsChanged.Remove(StatsChangedHandle);
	}
	StatsChangedHandle.Reset();
	BoundStats = nullptr;
}

UWidget* UNRLoadoutWidget::MakeTabBar()
{
	UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	const APawn* Pawn = GetOwningPlayerPawn();
	UNRStatsComponent* Stats = Pawn ? Pawn->FindComponentByClass<UNRStatsComponent>() : nullptr;
	const int32 Points = Stats ? Stats->GetUnspentPoints() : 0;
	if (BoundStats.Get() != Stats || !StatsChangedHandle.IsValid())
	{
		UnbindFromStats();
		BoundStats = Stats;
		if (Stats)
		{
			StatsChangedHandle = Stats->OnStatsChanged.AddWeakLambda(this, [this]() { RequestRefresh(); });
		}
	}

	auto AddTab = [this, Bar](EPage Page, const FText& Label)
	{
		const bool bActive = ActivePage == Page;
		UNRClickBox* Tab = CreateWidget<UNRClickBox>(this, UNRClickBox::StaticClass());
		UNRClickBox::FStyle Style;
		Style.Edge = bActive ? NRLoadoutStyle::Common : NRLoadoutStyle::EmptyEdge;
		Style.Fill = bActive ? NRLoadoutStyle::CellHover : NRLoadoutStyle::Panel;
		Style.HoverFill = NRLoadoutStyle::CellHover;
		Style.Padding = FMargin(24.0f, 8.0f);
		const FLinearColor Color = bActive ? NRLoadoutStyle::TextColor : NRLoadoutStyle::Dim;
		Tab->Configure([Label, Color](UWidgetTree* Tree)
		{
			return NRLoadoutStyle::MakeText(Tree, Label, 16, Color);
		}, Style);
		Tab->OnClicked.BindWeakLambda(this, [this, Page]()
		{
			if (ActivePage != Page)
			{
				ActivePage = Page;
				RequestRefresh();
			}
		});
		Bar->AddChildToHorizontalBox(Tab)->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	};

	AddTab(EPage::Parts, NSLOCTEXT("NRLoadout", "PartsTab", "무기 파츠"));
	AddTab(EPage::Stats, Points > 0
		? FText::Format(NSLOCTEXT("NRLoadout", "StatsTabPoints", "스탯 강화  +{0}"), FText::AsNumber(Points))
		: NSLOCTEXT("NRLoadout", "StatsTab", "스탯 강화"));
	return Bar;
}

UTextBlock* UNRLoadoutWidget::MakeText(const FText& Text, int32 Size, FLinearColor Color, bool bBold)
{
	return NRLoadoutStyle::MakeText(WidgetTree, Text, Size, Color, bBold);
}

UNRLoadoutCell::FConfig UNRLoadoutWidget::MakePartConfig(const FNRWeaponPartInstance* Part, FLinearColor OutlineColor) const
{
	UNRLoadoutCell::FConfig Config;
	Config.Outline = OutlineColor;
	Config.LabelColor = NRLoadoutStyle::TextColor;

	if (!Part || !Part->PartData)
	{
		return Config;
	}

	Config.PartId = Part->InstanceId;
	Config.Label = Part->PartData->ShortLabel;
	Config.Level = UNRWeaponPartData::GetLevelText(Part->Level);
	Config.Icon = Part->PartData->Icon;

	// Tooltip: name and level, the effect, and which weapons it fits.
	FString Fits;
	for (ENRWeaponId Id : { ENRWeaponId::Rifle, ENRWeaponId::Shotgun, ENRWeaponId::Melee })
	{
		if (Part->PartData->IsCompatibleWith(Id))
		{
			Fits += Fits.IsEmpty() ? TEXT("") : TEXT(", ");
			Fits += UEnum::GetDisplayValueAsText(Id).ToString();
		}
	}
	Config.Tooltip = FText::FromString(FString::Printf(TEXT("%s %s\n%s\nFits: %s%s"),
		*Part->PartData->DisplayName.ToString(), *Config.Level.ToString(),
		*Part->PartData->DescribeEffect(Part->Level).ToString(), *Fits,
		Part->PartData->SlotType == ENRWeaponPartSlotType::Exclusive ? TEXT("\nExclusive slot") : TEXT("")));
	return Config;
}

UNRLoadoutCell* UNRLoadoutWidget::CreateCell(const UNRLoadoutCell::FConfig& Config)
{
	UNRLoadoutCell* Cell = CreateWidget<UNRLoadoutCell>(this, UNRLoadoutCell::StaticClass());
	Cell->Configure(this, Config);
	return Cell;
}

UWidget* UNRLoadoutWidget::MakeWeaponCard(ENRWeaponId WeaponId)
{
	const UNRWeaponPartComponent* PartComponent = Parts.Get();

	// 2px so the drop highlight on the card edge is visible in a small viewport too.
	UBorder* Edge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Edge->SetBrushColor(NRLoadoutStyle::PanelEdge);
	Edge->SetPadding(FMargin(2.0f));
	CardEdges[static_cast<int32>(WeaponId)] = Edge;

	UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Card->SetBrushColor(NRLoadoutStyle::Panel);
	Card->SetPadding(FMargin(14.0f, 10.0f));
	Edge->SetContent(Card);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Card->SetContent(Column);

	// Header: name, then the stats this loadout produces.
	static const TCHAR* Names[] = { TEXT("RIFLE"), TEXT("SHOTGUN"), TEXT("MELEE") };
	static const TCHAR* Silhouettes[] = {
		TEXT("=[|####|]=======-"),
		TEXT("==[###]===========-"),
		TEXT("o==|>=============>") };
	const int32 WeaponIndex = static_cast<int32>(WeaponId);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Header->AddChildToHorizontalBox(MakeText(FText::FromString(Names[WeaponIndex]), 18, NRLoadoutStyle::TextColor))
		->SetVerticalAlignment(VAlign_Center);

	if (PartComponent)
	{
		const FNRWeaponRuntimeStats Stats = PartComponent->GetRuntimeStats(WeaponId);
		FString StatLine = FString::Printf(TEXT("DMG x%.2f   SPD x%.2f   CRIT %.0f%% x%.2f"),
			Stats.DamageMultiplier, Stats.AttackSpeedMultiplier, Stats.CriticalChance * 100.0f, Stats.CriticalMultiplier);
		if (WeaponId != ENRWeaponId::Melee)
		{
			StatLine += FString::Printf(TEXT("   MAG %d"), Stats.MaxMagazine);
		}
		UHorizontalBoxSlot* StatSlot = Header->AddChildToHorizontalBox(MakeText(FText::FromString(StatLine), 10, NRLoadoutStyle::Dim, false));
		StatSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		StatSlot->SetHorizontalAlignment(HAlign_Right);
		StatSlot->SetVerticalAlignment(VAlign_Center);
		StatSlot->SetPadding(FMargin(24.0f, 0.0f, 0.0f, 0.0f));
	}
	Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

	auto AddSlot = [this, PartComponent, WeaponId](UHorizontalBox* Row, int32 SlotIndex)
	{
		const FGuid Id = PartComponent ? PartComponent->GetEquippedPart(WeaponId, SlotIndex) : FGuid();
		const FNRWeaponPartInstance* Part = Id.IsValid() ? PartComponent->FindPart(Id) : nullptr;
		const bool bExclusive = FNRWeaponPartLoadout::GetSlotType(SlotIndex) == ENRWeaponPartSlotType::Exclusive;

		UNRLoadoutCell::FConfig Config = MakePartConfig(Part, bExclusive ? NRLoadoutStyle::Exclusive : NRLoadoutStyle::Common);
		Config.bIsSlot = true;
		Config.WeaponId = WeaponId;
		Config.SlotIndex = SlotIndex;
		if (Part)
		{
			Config.Tooltip = FText::FromString(Config.Tooltip.ToString() + TEXT("\nRight-click: take off"));
		}
		else
		{
			Config.Label = FText::FromString(bExclusive ? TEXT("EX") : TEXT("+"));
			Config.LabelColor = bExclusive ? NRLoadoutStyle::Exclusive : NRLoadoutStyle::Dim;
			Config.Tooltip = bExclusive
				? NSLOCTEXT("NRLoadout", "ExclusiveEmpty", "Exclusive slot\nNo exclusive parts yet")
				: NSLOCTEXT("NRLoadout", "CommonEmpty", "Common slot");
		}
		Row->AddChildToHorizontalBox(CreateCell(Config))->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	};

	// Exclusive slot next to the weapon, the five common slots underneath - the same six
	// on every weapon.
	UHorizontalBox* WeaponRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	AddSlot(WeaponRow, FNRWeaponPartLoadout::ExclusiveSlotIndex);
	UHorizontalBoxSlot* SilhouetteSlot = WeaponRow->AddChildToHorizontalBox(
		MakeText(FText::FromString(Silhouettes[WeaponIndex]), 22, NRLoadoutStyle::Dim));
	SilhouetteSlot->SetVerticalAlignment(VAlign_Center);
	SilhouetteSlot->SetPadding(FMargin(12.0f, 0.0f));
	Column->AddChildToVerticalBox(WeaponRow)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

	UHorizontalBox* CommonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	for (int32 SlotIndex = 1; SlotIndex < FNRWeaponPartLoadout::NumSlots; ++SlotIndex)
	{
		AddSlot(CommonRow, SlotIndex);
	}
	Column->AddChildToVerticalBox(CommonRow);

	return Edge;
}

UWidget* UNRLoadoutWidget::MakeInventoryPanel()
{
	const UNRWeaponPartComponent* PartComponent = Parts.Get();

	UBorder* Edge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Edge->SetBrushColor(NRLoadoutStyle::PanelEdge);
	Edge->SetPadding(FMargin(1.0f));

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Panel->SetBrushColor(NRLoadoutStyle::Panel);
	Panel->SetPadding(FMargin(14.0f));
	Edge->SetContent(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Column);
	Column->AddChildToVerticalBox(MakeText(NSLOCTEXT("NRLoadout", "Attachments", "ATTACHMENTS"), 18, NRLoadoutStyle::TextColor))
		->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));

	UUniformGridPanel* Grid = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass());
	Grid->SetSlotPadding(FMargin(4.0f));
	Column->AddChildToVerticalBox(Grid);

	// Unfitted parts only: a fitted one is shown on its weapon. Grouped by stat, then level.
	TArray<const FNRWeaponPartInstance*> Loose;
	if (PartComponent)
	{
		for (const FNRWeaponPartInstance& Part : PartComponent->GetInventory())
		{
			if (Part.IsValid() && !PartComponent->IsEquipped(Part.InstanceId))
			{
				Loose.Add(&Part);
			}
		}
	}
	Loose.StableSort([](const FNRWeaponPartInstance& A, const FNRWeaponPartInstance& B)
	{
		return A.PartData->Stat != B.PartData->Stat ? A.PartData->Stat < B.PartData->Stat : A.Level < B.Level;
	});

	// Empty cells stay: they are drop targets for taking a part off.
	const int32 Rows = FMath::Max(NRLoadoutStyle::MinGridRows, FMath::DivideAndRoundUp(Loose.Num(), NRLoadoutStyle::GridColumns));
	for (int32 Index = 0; Index < Rows * NRLoadoutStyle::GridColumns; ++Index)
	{
		const FNRWeaponPartInstance* Part = Loose.IsValidIndex(Index) ? Loose[Index] : nullptr;
		const bool bSelected = Part && Part->InstanceId == SelectedPart;
		const UNRLoadoutCell::FConfig Config = MakePartConfig(Part,
			bSelected ? NRLoadoutStyle::Selected : (Part ? NRLoadoutStyle::Common : NRLoadoutStyle::EmptyEdge));
		Grid->AddChildToUniformGrid(CreateCell(Config), Index / NRLoadoutStyle::GridColumns, Index % NRLoadoutStyle::GridColumns);
	}

	InventoryPanel = Edge;
	return Edge;
}

void UNRLoadoutWidget::Refresh()
{
	if (!RootBorder || !WidgetTree)
	{
		return;
	}

	UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Page->AddChildToVerticalBox(MakeTabBar())->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));

	CardEdges.Reset();
	if (ActivePage == EPage::Stats)
	{
		HighlightedCard = INDEX_NONE;
		if (!StatsPanel)
		{
			StatsPanel = CreateWidget<UNRStatsPanel>(this, UNRStatsPanel::StaticClass());
		}
		StatsPanel->RequestRefresh();
		// Framed like a weapon card: the stat page has no cards of its own to sit on.
		UBorder* Edge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Edge->SetBrushColor(NRLoadoutStyle::PanelEdge);
		Edge->SetPadding(FMargin(2.0f));
		UBorder* Fill = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Fill->SetBrushColor(NRLoadoutStyle::Panel);
		Fill->SetPadding(FMargin(24.0f, 16.0f));
		Fill->SetContent(StatsPanel);
		Edge->SetContent(Fill);
		Page->AddChildToVerticalBox(Edge);
		RootBorder->SetContent(Page);
		return;
	}

	CardEdges.SetNum(NRWeaponIdCount);
	HighlightedCard = INDEX_NONE;
	bHighlightCanFit = false;

	UHorizontalBox* Main = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UVerticalBox* Cards = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	for (ENRWeaponId Id : { ENRWeaponId::Rifle, ENRWeaponId::Shotgun, ENRWeaponId::Melee })
	{
		Cards->AddChildToVerticalBox(MakeWeaponCard(Id))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}
	Main->AddChildToHorizontalBox(Cards)->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
	Main->AddChildToHorizontalBox(MakeInventoryPanel())->SetVerticalAlignment(VAlign_Top);
	Page->AddChildToVerticalBox(Main);

	const UNRWeaponPartComponent* PartComponent = Parts.Get();
	FText Hint = !PartComponent
		? NSLOCTEXT("NRLoadout", "NoParts", "This character has no weapon part component.")
		: PartComponent->CanEditLoadout()
			? NSLOCTEXT("NRLoadout", "Hint", "Drag a part onto a slot, or onto a weapon for its first free slot.   Drag back to the grid or right-click to take it off.   [Tab] Close")
			: NSLOCTEXT("NRLoadout", "Locked", "Parts can only be changed in a safe area.   [Tab] Close");
	Page->AddChildToVerticalBox(MakeText(Hint, 11, NRLoadoutStyle::Dim, false))->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));

	if (!StatusText.IsEmpty())
	{
		Page->AddChildToVerticalBox(MakeText(StatusText, 12, bStatusIsError ? NRLoadoutStyle::Error : NRLoadoutStyle::Level))
			->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
	}

	RootBorder->SetContent(Page);
}

// --- Click ---------------------------------------------------------------------------

void UNRLoadoutWidget::HandleCellClicked(UNRLoadoutCell* Cell)
{
	UNRWeaponPartComponent* PartComponent = Parts.Get();
	if (!Cell || !PartComponent)
	{
		return;
	}
	const UNRLoadoutCell::FConfig& Config = Cell->GetConfig();

	if (!Config.bIsSlot)
	{
		// Inventory cell: toggle the selection. Empty cells do nothing.
		if (!Config.PartId.IsValid())
		{
			return;
		}
		SelectedPart = SelectedPart == Config.PartId ? FGuid() : Config.PartId;
		SetStatus(SelectedPart.IsValid()
			? FText::FromString(FString::Printf(TEXT("%s selected - click a slot"), *DescribePart(SelectedPart)))
			: FText::GetEmpty(), false);
		RequestRefresh();
		return;
	}

	const FString WeaponName = UEnum::GetDisplayValueAsText(Config.WeaponId).ToString();

	if (SelectedPart.IsValid())
	{
		const FString Name = DescribePart(SelectedPart);
		const ENRWeaponPartEquipResult Result = PartComponent->EquipPart(SelectedPart, Config.WeaponId, Config.SlotIndex);
		if (Result == ENRWeaponPartEquipResult::Success)
		{
			SetStatus(FText::FromString(FString::Printf(TEXT("%s fitted to %s"), *Name, *WeaponName)), false);
			SelectedPart.Invalidate();
		}
		else
		{
			SetStatus(UNRWeaponPartComponent::DescribeEquipResult(Result), true);
		}
		RequestRefresh();
		return;
	}

	const FGuid Fitted = PartComponent->GetEquippedPart(Config.WeaponId, Config.SlotIndex);
	if (!Fitted.IsValid())
	{
		SetStatus(NSLOCTEXT("NRLoadout", "SelectFirst", "Drag a part here, or select one first"), false);
		RequestRefresh();
		return;
	}

	// Taking a part off keeps it selected, so the next slot clicked moves it there.
	if (PartComponent->UnequipSlot(Config.WeaponId, Config.SlotIndex))
	{
		SelectedPart = Fitted;
		SetStatus(FText::FromString(FString::Printf(TEXT("%s removed from %s - click a slot to fit it elsewhere"),
			*DescribePart(Fitted), *WeaponName)), false);
	}
	else
	{
		SetStatus(UNRWeaponPartComponent::DescribeEquipResult(ENRWeaponPartEquipResult::EditingLocked), true);
	}
	RequestRefresh();
}

void UNRLoadoutWidget::HandleCellRightClicked(UNRLoadoutCell* Cell)
{
	UNRWeaponPartComponent* PartComponent = Parts.Get();
	if (!Cell || !PartComponent)
	{
		return;
	}
	const UNRLoadoutCell::FConfig& Config = Cell->GetConfig();
	if (!Config.bIsSlot || !Config.PartId.IsValid())
	{
		return;
	}

	// Unlike a left click, the part is not kept selected: it simply goes back to the grid.
	const FString Name = DescribePart(Config.PartId);
	if (PartComponent->UnequipSlot(Config.WeaponId, Config.SlotIndex))
	{
		if (SelectedPart == Config.PartId)
		{
			SelectedPart.Invalidate();
		}
		SetStatus(FText::FromString(FString::Printf(TEXT("%s taken off %s"), *Name,
			*UEnum::GetDisplayValueAsText(Config.WeaponId).ToString())), false);
	}
	else
	{
		SetStatus(UNRWeaponPartComponent::DescribeEquipResult(ENRWeaponPartEquipResult::EditingLocked), true);
	}
	RequestRefresh();
}

// --- Drag and drop -------------------------------------------------------------------

UNRPartDragOperation* UNRLoadoutWidget::HandleCellDragStarted(UNRLoadoutCell* Cell)
{
	const UNRWeaponPartComponent* PartComponent = Parts.Get();
	if (!Cell || !PartComponent || !Cell->GetConfig().PartId.IsValid())
	{
		return nullptr;
	}
	const UNRLoadoutCell::FConfig& Source = Cell->GetConfig();

	UNRPartDragOperation* Operation = NewObject<UNRPartDragOperation>(this);
	Operation->PartId = Source.PartId;
	Operation->bFromSlot = Source.bIsSlot;
	Operation->FromWeapon = Source.WeaponId;
	Operation->FromSlot = Source.SlotIndex;
	Operation->Pivot = EDragPivot::CenterCenter;

	// A copy of the cell follows the cursor; it must not be hit-testable or it would sit
	// between the cursor and every drop target.
	UNRLoadoutCell::FConfig VisualConfig = Source;
	VisualConfig.Outline = NRLoadoutStyle::Selected;
	VisualConfig.Tooltip = FText::GetEmpty();
	UNRLoadoutCell* Visual = CreateCell(VisualConfig);
	Visual->SetVisibility(ESlateVisibility::HitTestInvisible);
	Visual->SetRenderOpacity(0.85f);
	Operation->DefaultDragVisual = Visual;

	// No refresh here: rebuilding would destroy the cell the drag started from.
	SelectedPart.Invalidate();
	return Operation;
}

bool UNRLoadoutWidget::CanDropOnCell(const UNRLoadoutCell* Cell, const UNRPartDragOperation* Operation) const
{
	const UNRWeaponPartComponent* PartComponent = Parts.Get();
	if (!Cell || !Operation || !PartComponent)
	{
		return false;
	}
	const UNRLoadoutCell::FConfig& Target = Cell->GetConfig();

	if (!Target.bIsSlot)
	{
		// The grid takes back fitted parts; an inventory part dropped on it goes nowhere.
		return Operation->bFromSlot && PartComponent->CanEditLoadout();
	}

	if (Operation->bFromSlot && Operation->FromWeapon == Target.WeaponId && Operation->FromSlot == Target.SlotIndex)
	{
		return false;
	}
	return PartComponent->CanEquipPart(Operation->PartId, Target.WeaponId, Target.SlotIndex) == ENRWeaponPartEquipResult::Success;
}

bool UNRLoadoutWidget::HandleCellDrop(UNRLoadoutCell* Cell, UNRPartDragOperation* Operation)
{
	if (!Cell || !Operation)
	{
		return false;
	}
	const UNRLoadoutCell::FConfig& Target = Cell->GetConfig();

	if (Target.bIsSlot)
	{
		DropOnSlot(Operation, Target.WeaponId, Target.SlotIndex);
	}
	else if (Operation->bFromSlot)
	{
		DropOnInventory(Operation);
	}
	return true;
}

void UNRLoadoutWidget::DropOnSlot(UNRPartDragOperation* Operation, ENRWeaponId WeaponId, int32 SlotIndex)
{
	UNRWeaponPartComponent* PartComponent = Parts.Get();
	if (!PartComponent)
	{
		return;
	}
	if (Operation->bFromSlot && Operation->FromWeapon == WeaponId && Operation->FromSlot == SlotIndex)
	{
		return;
	}

	const FString Name = DescribePart(Operation->PartId);
	const FString WeaponName = UEnum::GetDisplayValueAsText(WeaponId).ToString();
	const FGuid Displaced = PartComponent->GetEquippedPart(WeaponId, SlotIndex);

	const ENRWeaponPartEquipResult Result = PartComponent->EquipPart(Operation->PartId, WeaponId, SlotIndex);
	if (Result != ENRWeaponPartEquipResult::Success)
	{
		SetStatus(UNRWeaponPartComponent::DescribeEquipResult(Result), true);
		RequestRefresh();
		return;
	}

	// Slot to occupied slot is a swap when the displaced part fits where the dragged one came
	// from; otherwise the displaced part has already gone back to the grid.
	FString Extra;
	if (Operation->bFromSlot && Displaced.IsValid()
		&& PartComponent->CanEquipPart(Displaced, Operation->FromWeapon, Operation->FromSlot) == ENRWeaponPartEquipResult::Success)
	{
		PartComponent->EquipPart(Displaced, Operation->FromWeapon, Operation->FromSlot);
		Extra = FString::Printf(TEXT(", swapped with %s"), *DescribePart(Displaced));
	}
	else if (Displaced.IsValid())
	{
		Extra = FString::Printf(TEXT(", %s returned to the grid"), *DescribePart(Displaced));
	}

	SetStatus(FText::FromString(FString::Printf(TEXT("%s fitted to %s%s"), *Name, *WeaponName, *Extra)), false);
	RequestRefresh();
}

int32 UNRLoadoutWidget::FindAutoSlot(const UNRPartDragOperation* Operation, ENRWeaponId WeaponId) const
{
	const UNRWeaponPartComponent* PartComponent = Parts.Get();
	if (!PartComponent || !Operation)
	{
		return INDEX_NONE;
	}

	// Already on this weapon: dropping it on its own card changes nothing.
	if (Operation->bFromSlot && Operation->FromWeapon == WeaponId)
	{
		return INDEX_NONE;
	}

	// Front to back; the part's slot type decides which of them can take it.
	for (int32 SlotIndex = 0; SlotIndex < FNRWeaponPartLoadout::NumSlots; ++SlotIndex)
	{
		if (!PartComponent->GetEquippedPart(WeaponId, SlotIndex).IsValid()
			&& PartComponent->CanEquipPart(Operation->PartId, WeaponId, SlotIndex) == ENRWeaponPartEquipResult::Success)
		{
			return SlotIndex;
		}
	}
	return INDEX_NONE;
}

void UNRLoadoutWidget::DropOnWeapon(UNRPartDragOperation* Operation, ENRWeaponId WeaponId)
{
	const UNRWeaponPartComponent* PartComponent = Parts.Get();
	if (!PartComponent)
	{
		return;
	}

	const int32 SlotIndex = FindAutoSlot(Operation, WeaponId);
	if (SlotIndex != INDEX_NONE)
	{
		DropOnSlot(Operation, WeaponId, SlotIndex);
		return;
	}

	const FString WeaponName = UEnum::GetDisplayValueAsText(WeaponId).ToString();
	if (Operation->bFromSlot && Operation->FromWeapon == WeaponId)
	{
		// Not an error: it is already there.
		SetStatus(FText::FromString(FString::Printf(TEXT("%s is already on %s"), *DescribePart(Operation->PartId), *WeaponName)), false);
		RequestRefresh();
		return;
	}

	// Say why: a slot of the right type that refuses it means the part does not fit this
	// weapon (or editing is locked); if every such slot would take it, they are all full.
	ENRWeaponPartEquipResult Reason = ENRWeaponPartEquipResult::WrongSlotType;
	for (int32 Index = 0; Index < FNRWeaponPartLoadout::NumSlots; ++Index)
	{
		const ENRWeaponPartEquipResult Result = PartComponent->CanEquipPart(Operation->PartId, WeaponId, Index);
		if (Result != ENRWeaponPartEquipResult::WrongSlotType)
		{
			Reason = Result;
			break;
		}
	}
	SetStatus(Reason == ENRWeaponPartEquipResult::Success
		? FText::FromString(FString::Printf(TEXT("No free slot on %s for that part"), *WeaponName))
		: UNRWeaponPartComponent::DescribeEquipResult(Reason), true);
	RequestRefresh();
}

void UNRLoadoutWidget::DropOnInventory(UNRPartDragOperation* Operation)
{
	UNRWeaponPartComponent* PartComponent = Parts.Get();
	if (!PartComponent || !Operation->bFromSlot)
	{
		return;
	}

	if (PartComponent->UnequipSlot(Operation->FromWeapon, Operation->FromSlot))
	{
		SetStatus(FText::FromString(FString::Printf(TEXT("%s taken off %s"), *DescribePart(Operation->PartId),
			*UEnum::GetDisplayValueAsText(Operation->FromWeapon).ToString())), false);
	}
	else
	{
		SetStatus(UNRWeaponPartComponent::DescribeEquipResult(ENRWeaponPartEquipResult::EditingLocked), true);
	}
	RequestRefresh();
}

// NR.Loadout.Page <parts|stats>: switch the open menu's tab, as clicking the tab does.
namespace NRLoadoutCommands
{
	void Page(const TArray<FString>& Args, UWorld* World)
	{
		const bool bStats = Args.Num() > 0 && Args[0].Equals(TEXT("stats"), ESearchCase::IgnoreCase);
		for (TObjectIterator<UNRLoadoutWidget> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsInViewport())
			{
				It->ShowStatsPage(bStats);
			}
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GNRLoadoutPageCommand(TEXT("NR.Loadout.Page"),
	TEXT("NR.Loadout.Page <parts|stats>: switch the open menu's tab."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NRLoadoutCommands::Page));
