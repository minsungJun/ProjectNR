// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/UserWidget.h"
#include "NRWeaponPartTypes.h"
#include "NRLoadoutWidget.generated.h"

class UBorder;
class UNRLoadoutWidget;
class UNRWeaponPartComponent;
class UTextBlock;
class UTexture2D;
class UWidget;

/** A part being dragged, and where it came from. */
UCLASS()
class PROJECTNR_API UNRPartDragOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	FGuid PartId;

	/** True when dragged out of a weapon slot, false when dragged from the inventory grid. */
	bool bFromSlot = false;
	ENRWeaponId FromWeapon = ENRWeaponId::Rifle;
	int32 FromSlot = INDEX_NONE;
};

/**
 * One square of the loadout screen: a weapon slot or an inventory cell.
 *
 * A user widget rather than a UButton because a button takes the mouse press for itself and
 * never lets a drag be detected. This handles both: a press and release on the same cell is a
 * click, a press and move is a drag. The decisions are all the owning UNRLoadoutWidget's.
 */
UCLASS()
class PROJECTNR_API UNRLoadoutCell : public UUserWidget
{
	GENERATED_BODY()

public:
	struct FConfig
	{
		/** The part shown, invalid for an empty cell. */
		FGuid PartId;
		FText Label;
		FText Level;
		TWeakObjectPtr<UTexture2D> Icon;
		FText Tooltip;
		FLinearColor Outline = FLinearColor::White;
		FLinearColor LabelColor = FLinearColor::White;

		/** True for a weapon slot, false for an inventory cell. */
		bool bIsSlot = false;
		ENRWeaponId WeaponId = ENRWeaponId::Rifle;
		int32 SlotIndex = INDEX_NONE;
	};

	/** Call before the cell is added to a panel; the tree is built from this on first use. */
	void Configure(UNRLoadoutWidget* InOwner, const FConfig& InConfig);

	const FConfig& GetConfig() const { return Config; }

	/** Drag feedback: green or red while a part hovers over the cell, back to normal after. */
	void SetOutlineColor(const FLinearColor& Color);
	void RestoreOutlineColor() { SetOutlineColor(Config.Outline); }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	/** Handled so the drag over a cell is not also reported to the card around it. */
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	TWeakObjectPtr<UNRLoadoutWidget> Owner;
	FConfig Config;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> OutlineBorder;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> InnerBorder;

	/** Set by a press on this cell, so a release that follows it counts as a click. */
	bool bPressed = false;
};

/**
 * Temporary part loadout screen, built entirely in C++ so there is no widget asset to keep
 * in step with the code.
 *
 * Layout follows docs/WEAPON_ATTACHMENTS.md: Rifle, Shotgun and Melee cards on the left,
 * each with its exclusive slot (yellow) and five common slots (cyan); owned, unfitted parts
 * in a five-column grid on the right.
 *
 * Two ways to move parts, both through UNRWeaponPartComponent's rules:
 *  - drag: grid -> slot fits, slot -> slot moves (swapping with whatever was there when it
 *    fits back), slot -> grid takes it off. A slot turns green or red under a dragged part.
 *  - click: select a part in the grid, then click a slot. Clicking a fitted part takes it off
 *    and keeps it selected, so the next slot clicked moves it there.
 *
 * Redrawn only when the component reports a change, on the next frame so a cell is never
 * destroyed inside its own event.
 */
UCLASS()
class PROJECTNR_API UNRLoadoutWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPartComponent(UNRWeaponPartComponent* InParts);

	/** Tab or Escape while the widget has focus. The HUD owns opening and closing. */
	FSimpleDelegate OnCloseRequested;

	// Called by UNRLoadoutCell.
	void HandleCellClicked(UNRLoadoutCell* Cell);
	/** Right-click on a fitted slot takes the part off, straight back to the grid. */
	void HandleCellRightClicked(UNRLoadoutCell* Cell);
	UNRPartDragOperation* HandleCellDragStarted(UNRLoadoutCell* Cell);
	bool HandleCellDrop(UNRLoadoutCell* Cell, UNRPartDragOperation* Operation);

	/** Whether dropping Operation's part on Cell would do something. */
	bool CanDropOnCell(const UNRLoadoutCell* Cell, const UNRPartDragOperation* Operation) const;

	/** Drops the weapon card highlight; a cell under the cursor gives its own feedback. */
	void ClearCardHighlight() { SetHighlightedCard(INDEX_NONE, false); }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	/** Reaches here over a weapon card but not over one of its slots: lights the card. */
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	UFUNCTION()
	void HandleInventoryChanged();

	UFUNCTION()
	void HandleLoadoutChanged(ENRWeaponId WeaponId);

	UFUNCTION()
	void HandleRuntimeStatsChanged(ENRWeaponId WeaponId, const FNRWeaponRuntimeStats& Stats);

	void UnbindFromParts();
	void RequestRefresh();
	void Refresh();

	/** Fits the part into the slot; a part displaced from there goes to the drag's source slot if it fits. */
	void DropOnSlot(UNRPartDragOperation* Operation, ENRWeaponId WeaponId, int32 SlotIndex);
	void DropOnInventory(UNRPartDragOperation* Operation);

	/** Dropped on a weapon card rather than a slot: the first empty slot, front to back, it fits. */
	void DropOnWeapon(UNRPartDragOperation* Operation, ENRWeaponId WeaponId);

	/** That first empty slot, or INDEX_NONE - also when the part is already on this weapon. */
	int32 FindAutoSlot(const UNRPartDragOperation* Operation, ENRWeaponId WeaponId) const;

	/** Index into CardEdges (= ENRWeaponId) of the card under a screen point, or INDEX_NONE. */
	int32 FindCardUnder(const FVector2D& ScreenPosition) const;

	void SetHighlightedCard(int32 WeaponIndex, bool bCanFit);

	UWidget* MakeWeaponCard(ENRWeaponId WeaponId);
	UWidget* MakeInventoryPanel();
	/** Label, level, icon and tooltip for a part; an empty config for null. */
	UNRLoadoutCell::FConfig MakePartConfig(const FNRWeaponPartInstance* Part, FLinearColor OutlineColor) const;
	UNRLoadoutCell* CreateCell(const UNRLoadoutCell::FConfig& Config);
	UTextBlock* MakeText(const FText& Text, int32 Size, FLinearColor Color, bool bBold = true);

	FString DescribePart(const FGuid& PartId) const;
	void SetStatus(const FText& Text, bool bError);

	TWeakObjectPtr<UNRWeaponPartComponent> Parts;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> RootBorder;

	/** The grid's panel: a slot's part dropped anywhere on it, gaps included, comes off. */
	UPROPERTY(Transient)
	TObjectPtr<UWidget> InventoryPanel;

	/** Outer border of each weapon card, indexed by ENRWeaponId. Drop target and highlight. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> CardEdges;

	int32 HighlightedCard = INDEX_NONE;
	bool bHighlightCanFit = false;

	FGuid SelectedPart;
	FText StatusText;
	bool bStatusIsError = false;
	bool bRefreshPending = false;
};
