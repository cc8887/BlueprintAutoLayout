// DrivenHighlightRegistry.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// In-memory store of "active highlights": DrivenId → Spec + bound widget +
// timing info. The registry is owned by the editor module and consumed by
// SDrivenHighlightOverlay (writes bound widget) and IDrivenHighlightInspector
// (reads).
#pragma once

#include "CoreMinimal.h"
#include "DrivenHighlightTypes.h"

class SWidget;

struct FDrivenActiveHighlight
{
	FDrivenHighlightSpec Spec;

	/** Widget that the overlay's traversal most recently bound. */
	TWeakPtr<SWidget> BoundWidget;

	/** FPlatformTime::Seconds() at the moment the spec was registered. */
	double StartTime = 0.0;

	/** True after the overlay has confirmed a hit (used to fire OnStarted exactly once). */
	bool bHasFiredStarted = false;
};

class FDrivenHighlightRegistry
{
public:
	/** Add or replace by DrivenId. */
	void Upsert(const FDrivenHighlightSpec& Spec);

	/** Remove a single id; returns true if it existed. */
	bool Remove(FName DrivenId);

	/** Remove everything. */
	void Clear();

	/** Read access. Returns nullptr if absent. */
	const FDrivenActiveHighlight* Find(FName DrivenId) const;
	FDrivenActiveHighlight*       Find(FName DrivenId);

	/** Snapshot of every active DrivenId, useful for tests / debug overlays. */
	TArray<FName> GetAllIds() const;

	/** Whole-map iteration for the Slate overlay. */
	const TMap<FName, FDrivenActiveHighlight>& GetAll() const { return Active; }
	TMap<FName, FDrivenActiveHighlight>&       GetAllMutable() { return Active; }

	/** Apply duration-based expiry. Returns the ids that were removed. */
	TArray<FName> ExpireBefore(double NowSeconds);

private:
	TMap<FName, FDrivenActiveHighlight> Active;
};
