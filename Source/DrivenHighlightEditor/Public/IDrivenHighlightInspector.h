// IDrivenHighlightInspector.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Test-facing API. Lets automation tests answer: "is this DrivenId currently
// highlighted, and which Widget did the overlay land on?".
#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

class SWidget;

class DRIVENHIGHLIGHTEDITOR_API IDrivenHighlightInspector
{
public:
	virtual ~IDrivenHighlightInspector() = default;

	/** Module-level access. */
	static IDrivenHighlightInspector& Get();
	static bool IsAvailable();

	/** True if Show*() has been called for this id and Hide() has not. */
	virtual bool IsActive(FName DrivenId) const = 0;

	/** The widget the overlay's traversal most recently bound to. May be
	 *  null/expired if traversal has not run yet (request was just enqueued)
	 *  or the widget has been destroyed. */
	virtual TWeakPtr<SWidget> GetHighlightedWidget(FName DrivenId) const = 0;

	/** Snapshot of all currently-active DrivenIds. */
	virtual TArray<FName> GetAllActive() const = 0;

	// ── Lifecycle delegates ─────────────────────────────────────────────────

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnHighlightStarted, FName, TWeakPtr<SWidget>);
	DECLARE_MULTICAST_DELEGATE_OneParam (FOnHighlightEnded,   FName);

	/** Fires when the overlay first binds the DrivenId to a real widget — i.e.
	 *  the request has visually taken effect. Latent automation commands wait
	 *  on this rather than polling. */
	virtual FOnHighlightStarted& OnHighlightStarted() = 0;

	/** Fires when Hide() / HideAll() / auto-expiry removes the DrivenId. */
	virtual FOnHighlightEnded& OnHighlightEnded() = 0;
};
