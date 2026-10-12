// Copyright Epic Games, Inc. All Rights Reserved.

#include "NRStatsPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"

namespace NRStatsStyle
{
	// Same palette as the parts page (NRLoadoutWidget.cpp), so the two tabs read as one menu.
	const FLinearColor Panel(0.015f, 0.06f, 0.07f, 0.94f);
	const FLinearColor PanelEdge(0.1f, 0.55f, 0.6f, 1.0f);
	const FLinearColor Row(0.02f, 0.09f, 0.1f, 1.0f);
	const FLinearColor RowHover(0.05f, 0.2f, 0.22f, 1.0f);
	const FLinearColor RowEdge(0.1f, 0.3f, 0.32f, 1.0f);
	const FLinearColor Selected(0.1f, 0.85f, 0.9f, 1.0f);
	const FLinearColor TextColor(0.85f, 0.97f, 1.0f, 1.0f);
	const FLinearColor Dim(0.45f, 0.62f, 0.65f, 1.0f);
	const FLinearColor Gold(1.0f, 0.78f, 0.1f, 1.0f);
	const FLinearColor GoldFill(0.22f, 0.16f, 0.02f, 1.0f);
	const FLinearColor GoldHover(0.38f, 0.28f, 0.04f, 1.0f);
	const FLinearColor Gain(0.35f, 1.0f, 0.6f, 1.0f);
	const FLinearColor Disabled(0.25f, 0.35f, 0.37f, 1.0f);

	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, FLinearColor Color, bool bBold = true)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Block->SetText(Text);
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
		return Block;
	}

	USizeBox* MakeSized(UWidgetTree* Tree, UWidget* Content, float Width, float Height = 0.0f, bool bCentered = false)
	{
		USizeBox* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		if (Width > 0.0f)
		{
			Size->SetWidthOverride(Width);
		}
		if (Height > 0.0f)
		{
			Size->SetHeightOverride(Height);
		}
		UPanelSlot* ContentSlot = Size->SetContent(Content);
		if (USizeBoxSlot* SizeSlot = Cast<USizeBoxSlot>(ContentSlot); SizeSlot && bCentered)
		{
			SizeSlot->SetHorizontalAlignment(HAlign_Center);
			SizeSlot->SetVerticalAlignment(VAlign_Center);
		}
		return Size;
	}

	UBorder* MakeBox(UWidgetTree* Tree, FLinearColor Edge, FLinearColor Fill, UWidget* Content, FMargin Padding, float EdgeWidth = 2.0f)
	{
		UBorder* Outer = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Outer->SetBrushColor(Edge);
		Outer->SetPadding(FMargin(EdgeWidth));
		UBorder* Inner = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Inner->SetBrushColor(Fill);
		Inner->SetPadding(Padding);
		Inner->SetContent(Content);
		Outer->SetContent(Inner);
		return Outer;
	}
}

// --- UNRClickBox ---------------------------------------------------------------------

void UNRClickBox::Configure(TFunction<UWidget*(UWidgetTree*)> InBuildContent, const FStyle& InStyle, bool bInEnabled)
{
	BuildContent = MoveTemp(InBuildContent);
	Style = InStyle;
	bEnabled = bInEnabled;
	// User widgets are self-hit-test-invisible by default; this one must take the clicks.
	SetVisibility(ESlateVisibility::Visible);
}

TSharedRef<SWidget> UNRClickBox::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Outline = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Outline->SetBrushColor(Style.Edge);
		Outline->SetPadding(FMargin(Style.EdgeWidth));

		FillBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		FillBorder->SetBrushColor(Style.Fill);
		FillBorder->SetPadding(Style.Padding);
		if (BuildContent)
		{
			FillBorder->SetContent(BuildContent(WidgetTree));
		}
		Outline->SetContent(FillBorder);
		WidgetTree->RootWidget = Outline;
	}
	return Super::RebuildWidget();
}

FReply UNRClickBox::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bPressed = true;
	}
	return FReply::Handled();
}

FReply UNRClickBox::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bPressed)
	{
		bPressed = false;
		if (bEnabled)
		{
			OnClicked.ExecuteIfBound();
		}
	}
	return FReply::Handled();
}

void UNRClickBox::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	if (bEnabled && FillBorder)
	{
		FillBorder->SetBrushColor(Style.HoverFill);
	}
}

void UNRClickBox::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bPressed = false;
	if (FillBorder)
	{
		FillBorder->SetBrushColor(Style.Fill);
	}
}

// --- UNRStatsPanel -------------------------------------------------------------------

void UNRStatsPanel::NativeConstruct()
{
	Super::NativeConstruct();

	if (Staged.Num() != NRPlayerStatCount)
	{
		Staged.Init(0, NRPlayerStatCount);
	}
	BindToStats();
	RequestRefresh();
}

void UNRStatsPanel::ResetStaged()
{
	Staged.Init(0, NRPlayerStatCount);
	RequestRefresh();
}

void UNRStatsPanel::NativeDestruct()
{
	UnbindFromStats();
	Super::NativeDestruct();
}

TSharedRef<SWidget> UNRStatsPanel::RebuildWidget()
{
	// No widget asset: the tree is built here, the first time Slate asks for it.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StatsRoot"));
		RootBorder->SetBrushColor(FLinearColor::Transparent);
		RootBorder->SetPadding(FMargin(0.0f));
		WidgetTree->RootWidget = RootBorder;
		if (Staged.Num() != NRPlayerStatCount)
		{
			Staged.Init(0, NRPlayerStatCount);
		}
		BindToStats();
		Refresh();
	}
	return Super::RebuildWidget();
}

void UNRStatsPanel::BindToStats()
{
	const APawn* Pawn = GetOwningPlayerPawn();
	UNRStatsComponent* Found = Pawn ? Pawn->FindComponentByClass<UNRStatsComponent>() : nullptr;
	if (Stats.Get() == Found && StatsChangedHandle.IsValid())
	{
		return;
	}

	UnbindFromStats();
	Stats = Found;
	if (Found)
	{
		StatsChangedHandle = Found->OnStatsChanged.AddUObject(this, &UNRStatsPanel::HandleStatsChanged);
	}
}

void UNRStatsPanel::UnbindFromStats()
{
	if (UNRStatsComponent* Old = Stats.Get())
	{
		Old->OnStatsChanged.Remove(StatsChangedHandle);
	}
	StatsChangedHandle.Reset();
	Stats = nullptr;
}

void UNRStatsPanel::HandleStatsChanged()
{
	RequestRefresh();
}

void UNRStatsPanel::RequestRefresh()
{
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

int32 UNRStatsPanel::GetStagedTotal() const
{
	int32 Total = 0;
	for (int32 Count : Staged)
	{
		Total += Count;
	}
	return Total;
}

int32 UNRStatsPanel::GetRemainingPoints() const
{
	const UNRStatsComponent* Component = Stats.Get();
	return Component ? FMath::Max(Component->GetUnspentPoints() - GetStagedTotal(), 0) : 0;
}

void UNRStatsPanel::ChangeStaged(ENRPlayerStat Stat, int32 Delta)
{
	const int32 Index = static_cast<int32>(Stat);
	if (!Staged.IsValidIndex(Index))
	{
		return;
	}

	SelectedStat = Stat;
	if (Delta > 0 && GetRemainingPoints() >= Delta)
	{
		Staged[Index] += Delta;
	}
	else if (Delta < 0)
	{
		Staged[Index] = FMath::Max(Staged[Index] + Delta, 0);
	}
	RequestRefresh();
}

void UNRStatsPanel::Apply()
{
	UNRStatsComponent* Component = Stats.Get();
	if (Component && GetStagedTotal() > 0 && Component->SpendPoints(Staged))
	{
		Staged.Init(0, NRPlayerStatCount);
	}
	RequestRefresh();
}

void UNRStatsPanel::Cancel()
{
	Staged.Init(0, NRPlayerStatCount);
	RequestRefresh();
}

UTextBlock* UNRStatsPanel::MakeText(const FText& Text, int32 Size, FLinearColor Color, bool bBold)
{
	return NRStatsStyle::MakeText(WidgetTree, Text, Size, Color, bBold);
}

UNRClickBox* UNRStatsPanel::MakeButton(const FText& Label, int32 Size, bool bEnabled, bool bPrimary, float Width, float Height)
{
	UNRClickBox* Button = CreateWidget<UNRClickBox>(this, UNRClickBox::StaticClass());
	UNRClickBox::FStyle Style;
	Style.Edge = !bEnabled ? NRStatsStyle::RowEdge : bPrimary ? NRStatsStyle::Gold : NRStatsStyle::PanelEdge;
	Style.Fill = bPrimary && bEnabled ? NRStatsStyle::GoldFill : NRStatsStyle::Row;
	Style.HoverFill = bPrimary ? NRStatsStyle::GoldHover : NRStatsStyle::RowHover;
	Style.Padding = FMargin(0.0f);
	const FLinearColor TextColor = !bEnabled ? NRStatsStyle::Disabled : bPrimary ? NRStatsStyle::Gold : NRStatsStyle::TextColor;
	Button->Configure([Label, Size, TextColor, Width, Height](UWidgetTree* Tree)
	{
		UTextBlock* Text = NRStatsStyle::MakeText(Tree, Label, Size, TextColor);
		Text->SetJustification(ETextJustify::Center);
		return NRStatsStyle::MakeSized(Tree, Text, Width, Height, true);
	}, Style, bEnabled);
	return Button;
}

UWidget* UNRStatsPanel::MakeHeader()
{
	const UNRStatsComponent* Component = Stats.Get();
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	// Level and experience.
	UHorizontalBox* LevelLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	LevelLine->AddChildToHorizontalBox(MakeText(NSLOCTEXT("NRStatsPanel", "Lv", "Lv."), 20, NRStatsStyle::TextColor))
		->SetVerticalAlignment(VAlign_Bottom);
	LevelLine->AddChildToHorizontalBox(MakeText(FText::AsNumber(Component->GetLevel()), 32, NRStatsStyle::TextColor))
		->SetPadding(FMargin(6.0f, 0.0f, 18.0f, 0.0f));

	const int32 Needed = Component->GetExperienceToNextLevel();
	UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
	Bar->SetPercent(Needed > 0 ? static_cast<float>(Component->GetExperience()) / Needed : 0.0f);
	Bar->SetFillColorAndOpacity(NRStatsStyle::Selected);
	UHorizontalBoxSlot* BarSlot = LevelLine->AddChildToHorizontalBox(NRStatsStyle::MakeSized(WidgetTree, Bar, 260.0f, 8.0f));
	BarSlot->SetVerticalAlignment(VAlign_Center);

	LevelLine->AddChildToHorizontalBox(MakeText(FText::Format(NSLOCTEXT("NRStatsPanel", "Xp", "{0} / {1}"),
		FText::AsNumber(Component->GetExperience()), FText::AsNumber(Needed)), 14, NRStatsStyle::TextColor, false))
		->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
	for (UPanelSlot* LineSlot : LevelLine->GetSlots())
	{
		if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(LineSlot))
		{
			HSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	UHorizontalBoxSlot* LevelSlot = Header->AddChildToHorizontalBox(
		NRStatsStyle::MakeBox(WidgetTree, NRStatsStyle::RowEdge, NRStatsStyle::Row, LevelLine, FMargin(16.0f, 8.0f)));
	LevelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LevelSlot->SetPadding(FMargin(0.0f, 0.0f, 16.0f, 0.0f));

	// Unspent points, less what is staged.
	UHorizontalBox* PointsLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	PointsLine->AddChildToHorizontalBox(MakeText(NSLOCTEXT("NRStatsPanel", "Points", "사용 가능 포인트"), 16, NRStatsStyle::TextColor))
		->SetVerticalAlignment(VAlign_Center);
	PointsLine->AddChildToHorizontalBox(MakeText(FText::AsNumber(GetRemainingPoints()), 32,
		GetRemainingPoints() > 0 ? NRStatsStyle::Gold : NRStatsStyle::Dim))
		->SetPadding(FMargin(28.0f, 0.0f, 0.0f, 0.0f));
	Header->AddChildToHorizontalBox(NRStatsStyle::MakeSized(WidgetTree,
		NRStatsStyle::MakeBox(WidgetTree, NRStatsStyle::PanelEdge, NRStatsStyle::Row, PointsLine, FMargin(20.0f, 8.0f)), 330.0f));
	return Header;
}

UWidget* UNRStatsPanel::MakeStatRow(ENRPlayerStat Stat)
{
	const UNRStatsComponent* Component = Stats.Get();
	const int32 Index = static_cast<int32>(Stat);
	const FNRStatDefinition& Definition = Component->GetDefinition(Stat);
	const int32 Invested = Component->GetInvestedPoints(Stat);
	const int32 Pending = Staged[Index];
	const float Current = Component->GetStatValue(Stat);
	const float Next = Component->GetStatValueWithPoints(Stat, Invested + Pending);
	const bool bSelected = Stat == SelectedStat;

	UNRClickBox* Row = CreateWidget<UNRClickBox>(this, UNRClickBox::StaticClass());
	UNRClickBox::FStyle Style;
	Style.Edge = bSelected ? NRStatsStyle::Selected : NRStatsStyle::RowEdge;
	Style.Fill = NRStatsStyle::Row;
	Style.HoverFill = NRStatsStyle::RowHover;
	Style.Padding = FMargin(16.0f, 8.0f);

	const bool bCanRemove = Pending > 0;
	const bool bCanAdd = GetRemainingPoints() > 0;

	const FText Name = Definition.DisplayName;
	const FText Description = Definition.Description;
	const FText CurrentText = Component->FormatValue(Stat, Current);
	const FText NextText = Component->FormatValue(Stat, Next);
	const FText DeltaText = Pending > 0
		? FText::Format(NSLOCTEXT("NRStatsPanel", "Delta", "(+{0})"), Component->FormatValue(Stat, Next - Current))
		: FText::GetEmpty();
	const FText CountText = FText::AsNumber(Pending);

	// The row's content is built when Slate first asks for the row, which for a page that is not
	// on screen can be much later. The -/+ buttons are therefore made in here, at that moment,
	// and slotted in straight away: made earlier, nothing would hold them until then and garbage
	// collection could take them (the crash on reopening the menu, 2026-10-07).
	TWeakObjectPtr<UNRStatsPanel> WeakPanel(this);
	Row->Configure([WeakPanel, Stat, Name, Description, CurrentText, NextText, DeltaText, CountText, Pending, bCanRemove, bCanAdd](UWidgetTree* Tree)
	{
		UNRStatsPanel* Panel = WeakPanel.Get();
		UNRClickBox* Minus = nullptr;
		UNRClickBox* Plus = nullptr;
		if (Panel)
		{
			Minus = Panel->MakeButton(NSLOCTEXT("NRStatsPanel", "Minus", "-"), 18, bCanRemove, false, 36.0f, 36.0f);
			Minus->OnClicked.BindWeakLambda(Panel, [Panel, Stat]() { Panel->ChangeStaged(Stat, -1); });
			Plus = Panel->MakeButton(NSLOCTEXT("NRStatsPanel", "Plus", "+"), 18, bCanAdd, true, 36.0f, 36.0f);
			Plus->OnClicked.BindWeakLambda(Panel, [Panel, Stat]() { Panel->ChangeStaged(Stat, 1); });
		}

		UHorizontalBox* Line = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		UVerticalBox* NameBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		NameBox->AddChildToVerticalBox(NRStatsStyle::MakeText(Tree, Name, 17, NRStatsStyle::TextColor));
		NameBox->AddChildToVerticalBox(NRStatsStyle::MakeText(Tree, Description, 11, NRStatsStyle::Dim, false))
			->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
		UHorizontalBoxSlot* NameSlot = Line->AddChildToHorizontalBox(NameBox);
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		UVerticalBox* CurrentBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		CurrentBox->AddChildToVerticalBox(NRStatsStyle::MakeText(Tree, NSLOCTEXT("NRStatsPanel", "Current", "현재"), 10, NRStatsStyle::Dim, false));
		CurrentBox->AddChildToVerticalBox(NRStatsStyle::MakeText(Tree, CurrentText, 18, NRStatsStyle::TextColor));
		Line->AddChildToHorizontalBox(NRStatsStyle::MakeSized(Tree, CurrentBox, 80.0f));

		Line->AddChildToHorizontalBox(NRStatsStyle::MakeText(Tree, NSLOCTEXT("NRStatsPanel", "Arrow", ">"), 18, NRStatsStyle::Dim))
			->SetPadding(FMargin(4.0f, 10.0f, 16.0f, 0.0f));

		UVerticalBox* NextBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		NextBox->AddChildToVerticalBox(NRStatsStyle::MakeText(Tree, NSLOCTEXT("NRStatsPanel", "Next", "다음"), 10, NRStatsStyle::Dim, false));
		UHorizontalBox* NextLine = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		NextLine->AddChildToHorizontalBox(NRStatsStyle::MakeText(Tree, NextText, 18, Pending > 0 ? NRStatsStyle::Gain : NRStatsStyle::TextColor));
		if (!DeltaText.IsEmpty())
		{
			NextLine->AddChildToHorizontalBox(NRStatsStyle::MakeText(Tree, DeltaText, 12, NRStatsStyle::Gain, false))
				->SetPadding(FMargin(6.0f, 5.0f, 0.0f, 0.0f));
		}
		NextBox->AddChildToVerticalBox(NextLine);
		Line->AddChildToHorizontalBox(NRStatsStyle::MakeSized(Tree, NextBox, 130.0f));

		if (Minus)
		{
			UHorizontalBoxSlot* MinusSlot = Line->AddChildToHorizontalBox(Minus);
			MinusSlot->SetPadding(FMargin(8.0f, 0.0f, 6.0f, 0.0f));
		}

		UTextBlock* Count = NRStatsStyle::MakeText(Tree, CountText, 16, NRStatsStyle::TextColor);
		Count->SetJustification(ETextJustify::Center);
		USizeBox* CountSize = NRStatsStyle::MakeSized(Tree, Count, 40.0f, 36.0f, true);
		UHorizontalBoxSlot* CountSlot = Line->AddChildToHorizontalBox(
			NRStatsStyle::MakeBox(Tree, NRStatsStyle::RowEdge, NRStatsStyle::Panel, CountSize, FMargin(0.0f)));
		CountSlot->SetVerticalAlignment(VAlign_Center);

		if (Plus)
		{
			UHorizontalBoxSlot* PlusSlot = Line->AddChildToHorizontalBox(Plus);
			PlusSlot->SetPadding(FMargin(6.0f, 0.0f, 0.0f, 0.0f));
		}

		for (UPanelSlot* LineSlot : Line->GetSlots())
		{
			if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(LineSlot))
			{
				HSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
		return Line;
	}, Style);
	Row->OnClicked.BindWeakLambda(this, [this, Stat]()
	{
		SelectedStat = Stat;
		RequestRefresh();
	});
	return Row;
}

UWidget* UNRStatsPanel::MakeSummary()
{
	const UNRStatsComponent* Component = Stats.Get();
	UVerticalBox* Summary = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Summary->AddChildToVerticalBox(MakeText(NSLOCTEXT("NRStatsPanel", "Preview", "예상 변화"), 17, NRStatsStyle::TextColor))
		->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

	for (int32 Index = 0; Index < NRPlayerStatCount; ++Index)
	{
		const ENRPlayerStat Stat = static_cast<ENRPlayerStat>(Index);
		const float Current = Component->GetStatValue(Stat);
		const float Next = Component->GetStatValueWithPoints(Stat, Component->GetInvestedPoints(Stat) + Staged[Index]);

		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UHorizontalBoxSlot* NameSlot = Line->AddChildToHorizontalBox(MakeText(Component->GetDefinition(Stat).DisplayName, 13, NRStatsStyle::TextColor, false));
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Line->AddChildToHorizontalBox(NRStatsStyle::MakeSized(WidgetTree, MakeText(Component->FormatValue(Stat, Current), 14, NRStatsStyle::TextColor), 56.0f));
		Line->AddChildToHorizontalBox(MakeText(NSLOCTEXT("NRStatsPanel", "SummaryArrow", ">"), 13, NRStatsStyle::Dim))
			->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		Line->AddChildToHorizontalBox(NRStatsStyle::MakeSized(WidgetTree, MakeText(Component->FormatValue(Stat, Next), 14,
			Next != Current ? NRStatsStyle::Gain : NRStatsStyle::TextColor), 56.0f));
		Summary->AddChildToVerticalBox(Line)->SetPadding(FMargin(0.0f, 4.0f));
	}
	return NRStatsStyle::MakeBox(WidgetTree, NRStatsStyle::RowEdge, NRStatsStyle::Row, Summary, FMargin(16.0f, 12.0f));
}

UWidget* UNRStatsPanel::MakeDetail()
{
	const UNRStatsComponent* Component = Stats.Get();
	const FNRStatDefinition& Definition = Component->GetDefinition(SelectedStat);

	UVerticalBox* Detail = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Detail->AddChildToVerticalBox(MakeText(Definition.DisplayName, 17, NRStatsStyle::Selected));
	UTextBlock* Description = MakeText(Definition.Description, 12, NRStatsStyle::Dim, false);
	Description->SetAutoWrapText(true);
	Detail->AddChildToVerticalBox(Description)->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
	Detail->AddChildToVerticalBox(MakeText(FText::Format(NSLOCTEXT("NRStatsPanel", "PerPoint", "포인트당 +{0}   (투자 {1})"),
		Component->FormatValue(SelectedStat, Definition.PerPoint), FText::AsNumber(Component->GetInvestedPoints(SelectedStat))),
		12, NRStatsStyle::TextColor, false))->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
	return NRStatsStyle::MakeBox(WidgetTree, NRStatsStyle::PanelEdge, NRStatsStyle::Row, Detail, FMargin(16.0f, 12.0f));
}

UWidget* UNRStatsPanel::MakeFooter()
{
	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	const bool bAnyStaged = GetStagedTotal() > 0;

	UNRClickBox* ApplyButton = MakeButton(NSLOCTEXT("NRStatsPanel", "Apply", "적용"), 18, bAnyStaged, true, 220.0f, 44.0f);
	ApplyButton->OnClicked.BindWeakLambda(this, [this]() { Apply(); });
	Footer->AddChildToHorizontalBox(ApplyButton)->SetPadding(FMargin(0.0f, 0.0f, 16.0f, 0.0f));

	UNRClickBox* CancelButton = MakeButton(NSLOCTEXT("NRStatsPanel", "Cancel", "취소"), 18, bAnyStaged, false, 220.0f, 44.0f);
	CancelButton->OnClicked.BindWeakLambda(this, [this]() { Cancel(); });
	Footer->AddChildToHorizontalBox(CancelButton);
	return Footer;
}

void UNRStatsPanel::Refresh()
{
	if (!RootBorder || !WidgetTree)
	{
		return;
	}

	BindToStats();
	const UNRStatsComponent* Component = Stats.Get();
	if (!Component)
	{
		RootBorder->SetContent(MakeText(NSLOCTEXT("NRStatsPanel", "NoStats", "이 캐릭터에는 스탯 컴포넌트가 없습니다."), 14, NRStatsStyle::Dim));
		return;
	}

	// More staged than there are points can only follow a points change from outside.
	while (GetStagedTotal() > Component->GetUnspentPoints())
	{
		for (int32& Count : Staged)
		{
			if (Count > 0)
			{
				--Count;
				break;
			}
		}
	}

	UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	UTextBlock* Title = MakeText(NSLOCTEXT("NRStatsPanel", "Title", "스탯 강화"), 30, NRStatsStyle::TextColor);
	Page->AddChildToVerticalBox(Title)->SetHorizontalAlignment(HAlign_Center);
	Page->AddChildToVerticalBox(MakeText(NSLOCTEXT("NRStatsPanel", "Subtitle", "몬스터를 처치해 경험치를 모으고, 레벨이 오를 때마다 포인트를 얻습니다."), 12, NRStatsStyle::Dim, false))
		->SetHorizontalAlignment(HAlign_Center);

	Page->AddChildToVerticalBox(MakeHeader())->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 12.0f));

	UHorizontalBox* Main = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UVerticalBox* Rows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	for (int32 Index = 0; Index < NRPlayerStatCount; ++Index)
	{
		Rows->AddChildToVerticalBox(MakeStatRow(static_cast<ENRPlayerStat>(Index)))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}
	UHorizontalBoxSlot* RowsSlot = Main->AddChildToHorizontalBox(NRStatsStyle::MakeSized(WidgetTree, Rows, 720.0f));
	RowsSlot->SetPadding(FMargin(0.0f, 0.0f, 16.0f, 0.0f));

	UVerticalBox* Side = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Side->AddChildToVerticalBox(MakeSummary())->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	Side->AddChildToVerticalBox(MakeDetail());
	Main->AddChildToHorizontalBox(NRStatsStyle::MakeSized(WidgetTree, Side, 330.0f));
	Page->AddChildToVerticalBox(Main);

	Page->AddChildToVerticalBox(MakeFooter())->SetHorizontalAlignment(HAlign_Center);
	Page->AddChildToVerticalBox(MakeText(NSLOCTEXT("NRStatsPanel", "Hint", "[+] / [-]로 포인트를 배분하고 [적용]을 누르세요.   [Tab] 닫기"), 11, NRStatsStyle::Dim, false))
		->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));

	RootBorder->SetContent(Page);
}
