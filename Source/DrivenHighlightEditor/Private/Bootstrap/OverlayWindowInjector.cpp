// OverlayWindowInjector.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "OverlayWindowInjector.h"
#include "Slate/SDrivenHighlightOverlay.h"
#include "DrivenHighlightLog.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWidget.h"
#include "Runtime/Launch/Resources/Version.h"

void FOverlayWindowInjector::Init(FDrivenHighlightRegistry* InRegistry,
                                  FDrivenNodeRegistry*      InNodeRegistry)
{
	if (bInitialized) return;
	Registry     = InRegistry;
	NodeRegistry = InNodeRegistry;

	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogDrivenHighlight, Verbose,
			TEXT("Slate not initialized; OverlayWindowInjector inert."));
		return;
	}

	FSlateApplication& App = FSlateApplication::Get();

	WindowDestroyedHandle = App.OnWindowBeingDestroyed().AddRaw(
		this, &FOverlayWindowInjector::OnWindowDestroyed);

	// Slate has no public OnWindowAdded multicast in every UE version, so we
	// instead poll once at init for already-open windows AND piggy-back on
	// individual SWindow::OnWindowClosed callbacks for cleanup. New windows
	// get caught by FOverlayWindowInjector::RescanAndAttach() being called
	// every editor-module ticker turn (see DrivenHighlightService.cpp).
	RescanAndAttach();

	bInitialized = true;
	UE_LOG(LogDrivenHighlight, Log,
		TEXT("OverlayWindowInjector ready (attached to %d existing windows)"),
		Attached.Num());
}

void FOverlayWindowInjector::RescanAndAttach()
{
	if (!FSlateApplication::IsInitialized()) return;
	TArray<TSharedRef<SWindow>> ExistingWindows;
	FSlateApplication::Get().GetAllVisibleWindowsOrdered(ExistingWindows);
	for (const TSharedRef<SWindow>& W : ExistingWindows)
	{
		Attach(W);
	}
}

void FOverlayWindowInjector::Shutdown()
{
	if (!bInitialized) return;

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnWindowBeingDestroyed().Remove(WindowDestroyedHandle);
	}

	for (FAttachedWindow& AW : Attached)
	{
		// We cannot remove the SOverlay slot we appended to the window's content
		// (SWindow does not expose a simple "remove child" API for arbitrary
		// children). Letting the widget pin go is enough; the overlay simply
		// stops drawing because Registry is now null.
		AW.Overlay.Reset();
		AW.Window.Reset();
	}
	Attached.Empty();
	Registry     = nullptr;
	NodeRegistry = nullptr;
	bInitialized = false;
}

bool FOverlayWindowInjector::ShouldTrack(const TSharedRef<SWindow>& Window)
{
	if (!Window->IsRegularWindow()) return false;
	if (Window->IsModalWindow())    return false;
	return true;
}

void FOverlayWindowInjector::Attach(TSharedRef<SWindow> Window)
{
	if (!ShouldTrack(Window)) return;
	if (!Registry) return;

	// Skip if already attached.
	for (const FAttachedWindow& AW : Attached)
	{
		if (AW.Window.Pin() == Window) return;
	}

	TSharedRef<SDrivenHighlightOverlay> Overlay =
		SNew(SDrivenHighlightOverlay, Registry, NodeRegistry);

	// Wrap the existing content in an SOverlay so the highlight overlay sits
	// above whatever the editor put there. SWindow::SetContent replaces the
	// previous content, which we put back into the SOverlay's first slot.
#if ENGINE_MAJOR_VERSION >= 5
	TSharedRef<SWidget> CurrentContent = Window->GetContent();
#else
	TSharedRef<SWidget> CurrentContent = ConstCastSharedRef<SWidget>(Window->GetContent());
#endif

	TSharedRef<SOverlay> Wrapper = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			CurrentContent
		]
		+ SOverlay::Slot()
		[
			Overlay
		];

	Window->SetContent(Wrapper);

	FAttachedWindow Record;
	Record.Window  = Window;
	Record.Overlay = Overlay;
	Attached.Add(Record);

	UE_LOG(LogDrivenHighlight, Verbose,
		TEXT("Attached SDrivenHighlightOverlay to window '%s'"),
		*Window->GetTitle().ToString());
}

void FOverlayWindowInjector::Detach(TSharedRef<SWindow> Window)
{
	for (int32 i = Attached.Num() - 1; i >= 0; --i)
	{
		if (Attached[i].Window.Pin() == Window)
		{
			Attached.RemoveAt(i);
			// We do not surgically remove the SOverlay we wrapped; the next
			// SetContent by the editor itself would do so. The overlay simply
			// goes inert without a registry pointer.
			break;
		}
	}
}

void FOverlayWindowInjector::OnWindowDestroyed(const SWindow& Window)
{
	for (int32 i = Attached.Num() - 1; i >= 0; --i)
	{
		TSharedPtr<SWindow> Pinned = Attached[i].Window.Pin();
		if (!Pinned.IsValid() || Pinned.Get() == &Window)
		{
			Attached.RemoveAt(i);
		}
	}
}
