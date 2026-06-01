// BALHighlightAdapter.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Bridges DSL import-lifecycle events to the IDrivenHighlightService. One
// hook class per DSL (BlueprintLisp / AnimBP2FP / MatBP2FP), all of which
// share the same translation logic via the templates below.
//
// All declarations are gated by WITH_<PLUGIN> so this header is safe to
// include unconditionally — same pattern as the AutoLayout hooks already
// living in BlueprintAutoLayoutModule.cpp.
#pragma once

#include "CoreMinimal.h"

#if WITH_BLUEPRINTLISP
#include "BlueprintLispModule.h"
#endif
#if WITH_ANIMBP2FP
#include "AnimBP2FPModule.h"
#endif
#if WITH_MATBP2FP
#include "MatBP2FPModule.h"
#endif

#include "DrivenHighlightTypes.h"

class UEdGraphNode;
class UObject;

namespace BALHighlightAdapter
{
	/** Behavior token consumed by the highlight adapter. Producers must add
	 *  this to FImportLifecycleContext::RequestedBehaviors to enable highlight
	 *  on a given import session. */
	inline const FName HighlightBehaviorName = FName(TEXT("Highlight"));

	/** Map a DSL-namespaced change-type enum to the runtime-side enum. */
	template <typename DSLChangeTypeEnum>
	EDrivenChangeType MapChangeType(DSLChangeTypeEnum In);

	/** Fan out a node-phase event to ShowNode calls. Templated over DSL namespace. */
	template <typename DSLEvent>
	void DispatchNodePhase(const DSLEvent& Event);

	/** Fan out a property-phase event to ShowNodeProperty / ShowAssetProperty calls. */
	template <typename DSLEvent>
	void DispatchPropertyPhase(const DSLEvent& Event);
}

// ── Concrete hook classes ──────────────────────────────────────────────────

#if WITH_BLUEPRINTLISP
class FBALBlueprintLispHighlightHook : public BlueprintLispImportLifecycle::IImportLifecycleHook
{
public:
	virtual int32 GetPriority(BlueprintLispImportLifecycle::EImportLifecyclePhase Phase) const override
	{
		// Run after AutoLayout (priority 100) so we highlight final positions.
		return -10;
	}

	virtual void OnNodePhase(const BlueprintLispImportLifecycle::FImportNodePhaseEvent& Event) override
	{
		if (Event.Phase != BlueprintLispImportLifecycle::EImportLifecyclePhase::PostNodeChanges) return;
		BALHighlightAdapter::DispatchNodePhase(Event);
	}

	virtual void OnPropertyPhase(const BlueprintLispImportLifecycle::FImportPropertyPhaseEvent& Event) override
	{
		if (Event.Phase != BlueprintLispImportLifecycle::EImportLifecyclePhase::PostPropertyChanges) return;
		BALHighlightAdapter::DispatchPropertyPhase(Event);
	}
};
#endif

#if WITH_ANIMBP2FP
class FBALAnimBP2FPHighlightHook : public AnimBP2FPImportLifecycle::IImportLifecycleHook
{
public:
	virtual int32 GetPriority(AnimBP2FPImportLifecycle::EImportLifecyclePhase) const override { return -10; }

	virtual void OnNodePhase(const AnimBP2FPImportLifecycle::FImportNodePhaseEvent& Event) override
	{
		if (Event.Phase != AnimBP2FPImportLifecycle::EImportLifecyclePhase::PostNodeChanges) return;
		BALHighlightAdapter::DispatchNodePhase(Event);
	}

	virtual void OnPropertyPhase(const AnimBP2FPImportLifecycle::FImportPropertyPhaseEvent& Event) override
	{
		if (Event.Phase != AnimBP2FPImportLifecycle::EImportLifecyclePhase::PostPropertyChanges) return;
		BALHighlightAdapter::DispatchPropertyPhase(Event);
	}
};
#endif

#if WITH_MATBP2FP
class FBALMatBP2FPHighlightHook : public MatBP2FPImportLifecycle::IImportLifecycleHook
{
public:
	virtual int32 GetPriority(MatBP2FPImportLifecycle::EImportLifecyclePhase) const override { return -10; }

	virtual void OnNodePhase(const MatBP2FPImportLifecycle::FImportNodePhaseEvent& Event) override
	{
		if (Event.Phase != MatBP2FPImportLifecycle::EImportLifecyclePhase::PostNodeChanges) return;
		BALHighlightAdapter::DispatchNodePhase(Event);
	}

	virtual void OnPropertyPhase(const MatBP2FPImportLifecycle::FImportPropertyPhaseEvent& Event) override
	{
		if (Event.Phase != MatBP2FPImportLifecycle::EImportLifecyclePhase::PostPropertyChanges) return;
		BALHighlightAdapter::DispatchPropertyPhase(Event);
	}
};
#endif
