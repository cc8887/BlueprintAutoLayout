// IDrivenHighlightService.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Service-facing API. Consumers (e.g. lifecycle hooks in BlueprintAutoLayout)
// call Show*/Hide* to drive highlights without knowing anything about Slate
// or the underlying Widget tree.
#pragma once

#include "CoreMinimal.h"
#include "DrivenHighlightTypes.h"

class UEdGraphNode;
class UObject;
class SWindow;

class DRIVENHIGHLIGHTEDITOR_API IDrivenHighlightService
{
public:
	virtual ~IDrivenHighlightService() = default;

	/** Module-level access. Equivalent to FDrivenHighlightEditorModule::Get().GetService()
	 *  but spelt as a single static call so call sites don't need to include
	 *  the module header. Safe to call only after PostEngineInit. */
	static IDrivenHighlightService& Get();
	static bool IsAvailable();

	// ── High-level helpers ──────────────────────────────────────────────────
	// These build the DrivenId + FDrivenHighlightSpec internally.

	/** Highlight a graph node using its NodeGuid. Returns the DrivenId that
	 *  was assigned (NAME_None on failure). */
	virtual FName ShowNode(const UEdGraphNode* Node,
	                       EDrivenChangeType ChangeType = EDrivenChangeType::Modified,
	                       float DurationSeconds = 0.f) = 0;

	/** Highlight a property row in the details panel for a given graph node. */
	virtual FName ShowNodeProperty(const UEdGraphNode* Node,
	                               FName PropertyDisplayName,
	                               float DurationSeconds = 0.f) = 0;

	/** Highlight a property row for an asset-level property (e.g. on a UMaterial). */
	virtual FName ShowAssetProperty(const UObject* Asset,
	                                FName PropertyDisplayName,
	                                float DurationSeconds = 0.f) = 0;

	// ── Low-level escape hatch ──────────────────────────────────────────────

	/** Caller provides a fully-formed Spec (DrivenId + Anchor). */
	virtual void Show(const FDrivenHighlightSpec& Spec) = 0;

	/** Stop a single highlight. */
	virtual void Hide(FName DrivenId) = 0;

	/** Stop all highlights. */
	virtual void HideAll() = 0;

	// ── Window mounting (multi-editor coverage) ─────────────────────────────
	// Auto-injection happens via OverlayWindowInjector at startup, but a
	// manual API is exposed so embedded test harnesses / unit tests can attach
	// to a specific SWindow without going through FSlateApplication's add-window
	// stream.

	virtual void AttachToWindow(TSharedRef<SWindow> Window) = 0;
	virtual void DetachFromWindow(TSharedRef<SWindow> Window) = 0;
};
