// OverlayWindowInjector.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Mounts an SDrivenHighlightOverlay child onto every regular editor window —
// existing and future — so highlight requests cover the BlueprintEditor,
// MaterialEditor, AnimBlueprintEditor (or any other Slate editor window) with
// no per-toolkit wiring.
#pragma once

#include "CoreMinimal.h"

class SWindow;
class SWidget;
class FDrivenHighlightRegistry;
class FDrivenNodeRegistry;

class FOverlayWindowInjector
{
public:
	void Init(FDrivenHighlightRegistry* InRegistry, FDrivenNodeRegistry* InNodeRegistry);
	void Shutdown();

	/** Manual attach (also used by AttachToWindow on the service). */
	void Attach(TSharedRef<SWindow> Window);

	/** Manual detach. */
	void Detach(TSharedRef<SWindow> Window);

	/** Walk all currently-visible top-level windows and attach to any that
	 *  haven't been attached yet. Called periodically by the editor module's
	 *  ticker so newly-opened editor windows pick up an overlay without us
	 *  having to subscribe to a Slate add-window delegate (which has shifted
	 *  shape across UE versions). */
	void RescanAndAttach();

private:
	/** UE's OnWindowBeingDestroyed delivers `const SWindow&`, not the TSharedRef
	 *  variant — so we accept the bare reference and look it up in `Attached`. */
	void OnWindowDestroyed(const SWindow& Window);

	/** Heuristic: is this a regular editor window worth tracking?
	 *  - Excludes tooltips, menus, popups, native windows. */
	static bool ShouldTrack(const TSharedRef<SWindow>& Window);

	FDrivenHighlightRegistry* Registry     = nullptr;
	FDrivenNodeRegistry*      NodeRegistry = nullptr;

	struct FAttachedWindow
	{
		TWeakPtr<SWindow> Window;
		TWeakPtr<SWidget> Overlay;
	};
	TArray<FAttachedWindow> Attached;

	FDelegateHandle WindowDestroyedHandle;
	bool bInitialized = false;
};
