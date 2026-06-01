// BALHighlightAdapter.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "Highlight/BALHighlightAdapter.h"

#include "IDrivenHighlightService.h"
#include "DrivenHighlightTypes.h"

#include "EdGraph/EdGraphNode.h"
#include "UObject/Object.h"

namespace BALHighlightAdapter
{
	// ── ChangeType mapping ──────────────────────────────────────────────────
	//
	// The DSL plugins each declare their own EImportNodeChangeType /
	// EImportPropertyChangeType enums but the underlying values are identical
	// across all three (Added=0, Modified=1, Removed=2). We still map
	// explicitly to insulate against any future skew.

#if WITH_BLUEPRINTLISP
	template <>
	EDrivenChangeType MapChangeType<BlueprintLispImportLifecycle::EImportNodeChangeType>(
		BlueprintLispImportLifecycle::EImportNodeChangeType In)
	{
		using E = BlueprintLispImportLifecycle::EImportNodeChangeType;
		switch (In)
		{
			case E::Added:    return EDrivenChangeType::Added;
			case E::Modified: return EDrivenChangeType::Modified;
			case E::Removed:  return EDrivenChangeType::Removed;
		}
		return EDrivenChangeType::Modified;
	}

	template <>
	EDrivenChangeType MapChangeType<BlueprintLispImportLifecycle::EImportPropertyChangeType>(
		BlueprintLispImportLifecycle::EImportPropertyChangeType In)
	{
		using E = BlueprintLispImportLifecycle::EImportPropertyChangeType;
		switch (In)
		{
			case E::Added:    return EDrivenChangeType::Added;
			case E::Modified: return EDrivenChangeType::Modified;
			case E::Removed:  return EDrivenChangeType::Removed;
		}
		return EDrivenChangeType::Modified;
	}
#endif

#if WITH_ANIMBP2FP
	template <>
	EDrivenChangeType MapChangeType<AnimBP2FPImportLifecycle::EImportNodeChangeType>(
		AnimBP2FPImportLifecycle::EImportNodeChangeType In)
	{
		using E = AnimBP2FPImportLifecycle::EImportNodeChangeType;
		switch (In)
		{
			case E::Added:    return EDrivenChangeType::Added;
			case E::Modified: return EDrivenChangeType::Modified;
			case E::Removed:  return EDrivenChangeType::Removed;
		}
		return EDrivenChangeType::Modified;
	}

	template <>
	EDrivenChangeType MapChangeType<AnimBP2FPImportLifecycle::EImportPropertyChangeType>(
		AnimBP2FPImportLifecycle::EImportPropertyChangeType In)
	{
		using E = AnimBP2FPImportLifecycle::EImportPropertyChangeType;
		switch (In)
		{
			case E::Added:    return EDrivenChangeType::Added;
			case E::Modified: return EDrivenChangeType::Modified;
			case E::Removed:  return EDrivenChangeType::Removed;
		}
		return EDrivenChangeType::Modified;
	}
#endif

#if WITH_MATBP2FP
	template <>
	EDrivenChangeType MapChangeType<MatBP2FPImportLifecycle::EImportNodeChangeType>(
		MatBP2FPImportLifecycle::EImportNodeChangeType In)
	{
		using E = MatBP2FPImportLifecycle::EImportNodeChangeType;
		switch (In)
		{
			case E::Added:    return EDrivenChangeType::Added;
			case E::Modified: return EDrivenChangeType::Modified;
			case E::Removed:  return EDrivenChangeType::Removed;
		}
		return EDrivenChangeType::Modified;
	}

	template <>
	EDrivenChangeType MapChangeType<MatBP2FPImportLifecycle::EImportPropertyChangeType>(
		MatBP2FPImportLifecycle::EImportPropertyChangeType In)
	{
		using E = MatBP2FPImportLifecycle::EImportPropertyChangeType;
		switch (In)
		{
			case E::Added:    return EDrivenChangeType::Added;
			case E::Modified: return EDrivenChangeType::Modified;
			case E::Removed:  return EDrivenChangeType::Removed;
		}
		return EDrivenChangeType::Modified;
	}
#endif

	// ── Phase fan-out (templated over DSL event types) ──────────────────────
	//
	// Each DSL has its own NodeChange / PropertyChange struct, but they share
	// identical field names (Node / TargetObject / PropertyName / ChangeType).
	// That lets us write the dispatch logic once with a duck-typed template.

	template <typename DSLEvent>
	void DispatchNodePhase(const DSLEvent& Event)
	{
		if (!IDrivenHighlightService::IsAvailable()) return;
		// Highlight is default-on: any node change observed by a lifecycle hook
		// is automatically surfaced. Producers don't have to opt in.

		IDrivenHighlightService& Svc = IDrivenHighlightService::Get();
		for (const auto& Change : Event.Changes)
		{
			if (!Change.Node) continue;
			const EDrivenChangeType T = MapChangeType(Change.ChangeType);
			Svc.ShowNode(Change.Node, T, /*Duration=*/ 3.0f);
		}
	}

	template <typename DSLEvent>
	void DispatchPropertyPhase(const DSLEvent& Event)
	{
		if (!IDrivenHighlightService::IsAvailable()) return;

		IDrivenHighlightService& Svc = IDrivenHighlightService::Get();
		for (const auto& Change : Event.Changes)
		{
			if (!Change.TargetObject || Change.PropertyName.IsNone()) continue;

			// Property targets are typically UEdGraphNode (node properties) or
			// the asset itself (e.g. UMaterial properties). Pick the right
			// service entrypoint accordingly.
			if (const UEdGraphNode* Node = Cast<const UEdGraphNode>(Change.TargetObject))
			{
				Svc.ShowNodeProperty(Node, Change.PropertyName, /*Duration=*/ 3.0f);
			}
			else
			{
				Svc.ShowAssetProperty(Change.TargetObject, Change.PropertyName, /*Duration=*/ 3.0f);
			}
		}
	}

	// ── Explicit instantiations so the .h decls have linkage. ──────────────

#if WITH_BLUEPRINTLISP
	template void DispatchNodePhase<BlueprintLispImportLifecycle::FImportNodePhaseEvent>(
		const BlueprintLispImportLifecycle::FImportNodePhaseEvent&);
	template void DispatchPropertyPhase<BlueprintLispImportLifecycle::FImportPropertyPhaseEvent>(
		const BlueprintLispImportLifecycle::FImportPropertyPhaseEvent&);
#endif
#if WITH_ANIMBP2FP
	template void DispatchNodePhase<AnimBP2FPImportLifecycle::FImportNodePhaseEvent>(
		const AnimBP2FPImportLifecycle::FImportNodePhaseEvent&);
	template void DispatchPropertyPhase<AnimBP2FPImportLifecycle::FImportPropertyPhaseEvent>(
		const AnimBP2FPImportLifecycle::FImportPropertyPhaseEvent&);
#endif
#if WITH_MATBP2FP
	template void DispatchNodePhase<MatBP2FPImportLifecycle::FImportNodePhaseEvent>(
		const MatBP2FPImportLifecycle::FImportNodePhaseEvent&);
	template void DispatchPropertyPhase<MatBP2FPImportLifecycle::FImportPropertyPhaseEvent>(
		const MatBP2FPImportLifecycle::FImportPropertyPhaseEvent&);
#endif
}
