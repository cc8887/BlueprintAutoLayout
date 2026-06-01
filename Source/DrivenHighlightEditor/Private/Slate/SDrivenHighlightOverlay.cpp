// SDrivenHighlightOverlay.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "SDrivenHighlightOverlay.h"

#include "DrivenHighlightRegistry.h"
#include "NodeRegistry.h"
#include "DrivenHighlightLog.h"
#include "DrivenHighlightEditorModule.h"
#include "IDrivenHighlightInspector.h"

#include "Widgets/SWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/SlateRect.h"
#include "Rendering/DrawElements.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Layout/Geometry.h"
#include "Layout/PaintGeometry.h"
#include "Types/ISlateMetaData.h"

namespace
{
	/** Pulse period in seconds, mirrors STutorialOverlay's BorderPulseAnimation cadence. */
	constexpr float kPulsePeriod   = 1.0f;
	/** Border thickness in slate units. */
	constexpr float kBorderThick   = 3.0f;
	/** Base color (alpha is overwritten with the pulse value). */
	const FLinearColor kBaseColor  = FLinearColor(0.10f, 0.45f, 1.00f, 1.0f);

	/** Find a Widget's FTagMetaData::Tag (returns NAME_None if absent). */
	FName GetWidgetTag(const TSharedRef<const SWidget>& Widget)
	{
		const auto Meta = Widget->GetMetaData<FTagMetaData>();
		return Meta.IsValid() ? Meta->Tag : NAME_None;
	}

	/** Recursive descent from a root widget; calls Visitor for every widget. */
	void WalkWidgetTree(const TSharedRef<const SWidget>& Root,
	                    const TFunctionRef<void(const TSharedRef<const SWidget>&)>& Visitor)
	{
		Visitor(Root);
		FChildren* Kids = const_cast<SWidget&>(Root.Get()).GetChildren();
		if (!Kids) return;
		for (int32 i = 0; i < Kids->Num(); ++i)
		{
			TSharedRef<SWidget> Child = Kids->GetChildAt(i);
			WalkWidgetTree(Child, Visitor);
		}
	}

	/** Generate a 0..1 value pulsing with a sine wave at the configured period. */
	float CurrentPulseAlpha()
	{
		const double Now = FPlatformTime::Seconds();
		const float  T   = static_cast<float>(FMath::Fmod(Now, (double)kPulsePeriod)) / kPulsePeriod;
		return 0.4f + 0.5f * FMath::Sin(T * 2.f * PI);
	}
}

void SDrivenHighlightOverlay::Construct(const FArguments& /*InArgs*/,
                                        FDrivenHighlightRegistry* InRegistry,
                                        FDrivenNodeRegistry*      InNodeRegistry)
{
	Registry     = InRegistry;
	NodeRegistry = InNodeRegistry;

	SetCanTick(false);
	// Hit-test invisible: never block clicks to the underlying editor UI.
	SetVisibility(EVisibility::SelfHitTestInvisible);
}

void SDrivenHighlightOverlay::ResolveBindings() const
{
	if (!Registry) return;
	auto& All = Registry->GetAllMutable();
	if (All.IsEmpty()) return;

	// Build a quick lookup: WidgetTag → DrivenId(s) for active NamedWidget specs.
	TMultiMap<FName, FName> TagToIds;
	int32 NeedsBinding = 0;
	for (auto& Pair : All)
	{
		FDrivenActiveHighlight& H = Pair.Value;
		if (H.Spec.AnchorType != EDrivenAnchorType::NamedWidget) continue;
		if (H.Spec.WrappedIdentifier.IsNone()) continue;
		// Already resolved & still alive → skip.
		if (H.BoundWidget.IsValid()) continue;
		TagToIds.Add(H.Spec.WrappedIdentifier, Pair.Key);
		++NeedsBinding;
	}
	if (NeedsBinding == 0) return;

	// Walk the focused window only. STutorialOverlay uses the *user-focused*
	// widget's owning window — that is precisely the editor window the user
	// is in. Falls back to ActiveTopLevelWindow if no widget is focused.
	TSharedPtr<SWindow> Window;
	{
		const TWeakPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetUserFocusedWidget(0);
		if (TSharedPtr<SWidget> Pinned = FocusedWidget.Pin())
		{
			Window = FSlateApplication::Get().FindWidgetWindow(Pinned.ToSharedRef());
		}
		if (!Window.IsValid())
		{
			Window = FSlateApplication::Get().GetActiveTopLevelWindow();
		}
	}
	if (!Window.IsValid()) return;

	WalkWidgetTree(Window.ToSharedRef(),
			[&TagToIds, &All](const TSharedRef<const SWidget>& W)
			{
				const FName Tag = GetWidgetTag(W);
				if (Tag.IsNone()) return;

				TArray<FName> Ids;
				TagToIds.MultiFind(Tag, Ids);
				for (FName Id : Ids)
				{
					if (FDrivenActiveHighlight* H = All.Find(Id))
					{
						// Cast away const for storage; the widget is borrowed
						// non-mutating only.
						H->BoundWidget = ConstCastSharedRef<SWidget>(W);
					}
				}
			});
}

void SDrivenHighlightOverlay::FireStartedEventsIfNeeded() const
{
	if (!FDrivenHighlightEditorModule::IsAvailable() || !Registry) return;
	auto& Inspector = FDrivenHighlightEditorModule::Get().GetInspector();

	for (auto& Pair : Registry->GetAllMutable())
	{
		FDrivenActiveHighlight& H = Pair.Value;
		if (H.bHasFiredStarted) continue;
		if (!H.BoundWidget.IsValid()) continue;
		H.bHasFiredStarted = true;
		Inspector.OnHighlightStarted().Broadcast(Pair.Key, H.BoundWidget);
	}
}

int32 SDrivenHighlightOverlay::OnPaint(const FPaintArgs&        Args,
                                       const FGeometry&         AllottedGeometry,
                                       const FSlateRect&        MyCullingRect,
                                       FSlateWindowElementList& OutDrawElements,
                                       int32                    LayerId,
                                       const FWidgetStyle&      InWidgetStyle,
                                       bool                     bParentEnabled) const
{
	if (!Registry)
	{
		return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect,
			OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	// 1. Bind any unbound NamedWidget specs.
	ResolveBindings();

	// 2. Fire OnHighlightStarted for newly-bound ids (visible to tests).
	FireStartedEventsIfNeeded();

	// 3. Draw a pulsing border for every bound spec.
	const float Alpha = CurrentPulseAlpha();
	FLinearColor Color = kBaseColor;
	Color.A = Alpha;

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));

	for (const auto& Pair : Registry->GetAll())
	{
		const FDrivenActiveHighlight& H = Pair.Value;
		TSharedPtr<SWidget> Bound = H.BoundWidget.Pin();
		if (!Bound.IsValid()) continue;

		const FGeometry& BoundGeo = Bound->GetCachedGeometry();
		const FVector2D Pos  = BoundGeo.GetAbsolutePosition() - AllottedGeometry.GetAbsolutePosition();
		const FVector2D Size = BoundGeo.GetLocalSize() * BoundGeo.Scale;

		// Skip degenerate / zero-area cached geometries.
		if (Size.X <= 0.f || Size.Y <= 0.f) continue;

		// Top
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, kBorderThick), FSlateLayoutTransform(Pos)),
			WhiteBrush, ESlateDrawEffect::None, Color);
		// Bottom
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, kBorderThick),
				FSlateLayoutTransform(Pos + FVector2D(0.f, Size.Y - kBorderThick))),
			WhiteBrush, ESlateDrawEffect::None, Color);
		// Left
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(kBorderThick, Size.Y), FSlateLayoutTransform(Pos)),
			WhiteBrush, ESlateDrawEffect::None, Color);
		// Right
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(kBorderThick, Size.Y),
				FSlateLayoutTransform(Pos + FVector2D(Size.X - kBorderThick, 0.f))),
			WhiteBrush, ESlateDrawEffect::None, Color);
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}
