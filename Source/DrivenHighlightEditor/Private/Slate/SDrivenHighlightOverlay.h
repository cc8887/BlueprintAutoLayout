// SDrivenHighlightOverlay.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// A SelfHitTestInvisible covering widget that walks the focused window's
// widget tree once per paint, finds widgets matching the active highlight
// specs (NamedWidget anchor → FTagMetaData::Tag match), and draws a pulsing
// blue border around them.
//
// Inspired by STutorialOverlay but trimmed — no content panel, no multi-window
// machinery, no IntroTutorials dependency. Multi-editor coverage is achieved
// by attaching one of these per editor SWindow at startup time (see
// OverlayWindowInjector).
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FDrivenHighlightRegistry;
class FDrivenNodeRegistry;
class IDrivenHighlightInspector;

class SDrivenHighlightOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDrivenHighlightOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs,
	               FDrivenHighlightRegistry* InRegistry,
	               FDrivenNodeRegistry*      InNodeRegistry);

	// SWidget
	virtual int32 OnPaint(const FPaintArgs&        Args,
	                      const FGeometry&         AllottedGeometry,
	                      const FSlateRect&        MyCullingRect,
	                      FSlateWindowElementList& OutDrawElements,
	                      int32                    LayerId,
	                      const FWidgetStyle&      InWidgetStyle,
	                      bool                     bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D::ZeroVector;
	}

	virtual bool ComputeVolatility() const override { return true; }

private:
	/** Walk the focused window's widget tree, attempting to bind every active
	 *  NamedWidget DrivenId to a real SWidget. Mutates the registry. */
	void ResolveBindings() const;

	/** Trigger BoundWidget hits as inspector events the first time they bind. */
	void FireStartedEventsIfNeeded() const;

	FDrivenHighlightRegistry* Registry     = nullptr;
	FDrivenNodeRegistry*      NodeRegistry = nullptr;
};
