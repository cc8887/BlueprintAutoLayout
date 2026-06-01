// DrivenHighlightTypes.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Plain-data types used by the DrivenHighlight subsystem. This header is part
// of the Runtime module and intentionally has no Slate / Editor dependencies
// so it can be referenced from any layer (including hooks living in the main
// BlueprintAutoLayout module).
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UObject;
class UEdGraphNode;

// ─────────────────────────────────────────────────────────────────────────────
//  Anchor type — mirrors ETutorialAnchorIdentifier semantics, but lives in our
//  own module so the runtime header does not depend on IntroTutorials.
// ─────────────────────────────────────────────────────────────────────────────
enum class EDrivenAnchorType : uint8
{
	/** Match a Slate widget by its FTagMetaData::Tag. */
	NamedWidget,
	/** Locate a UEdGraphNode by OuterName + GUIDString and focus its widget. */
	BlueprintNode,
};

// ─────────────────────────────────────────────────────────────────────────────
//  Change classification — comes straight from the DSL import lifecycle so the
//  highlight viewer can color-code adds vs. modifications vs. removals later.
// ─────────────────────────────────────────────────────────────────────────────
enum class EDrivenChangeType : uint8
{
	Added,
	Modified,
	Removed,
};

// ─────────────────────────────────────────────────────────────────────────────
//  FDrivenHighlightSpec — the contract between producers (lifecycle hooks) and
//  the editor-side highlight service.
//
//  Two main flavours, distinguished by AnchorType:
//
//    NamedWidget   → fill WrappedIdentifier (e.g. "DetailRowItem.Location")
//    BlueprintNode → fill OuterName + GUIDString
//
//  The remaining fields are diagnostic / behavioural and optional.
// ─────────────────────────────────────────────────────────────────────────────
struct DRIVENHIGHLIGHT_API FDrivenHighlightSpec
{
	/** Stable, parseable identifier — see DrivenId.h for the grammar. */
	FName DrivenId;

	EDrivenAnchorType AnchorType = EDrivenAnchorType::NamedWidget;

	/** NamedWidget anchor: matches FTagMetaData::Tag exactly. */
	FName WrappedIdentifier;

	/** BlueprintNode anchor: outer object full-name (e.g. "Blueprint /Game/X.X"). */
	FString OuterName;

	/** BlueprintNode anchor: NodeGuid as a string. */
	FString GUIDString;

	/** Diagnostic — what real object did the producer mean. Weak so we never
	 *  prevent GC. */
	TWeakObjectPtr<const UObject> SourceObject;

	/** Human-readable label, surfaces in test reports / logs. */
	FString DebugLabel;

	/** 0 → caller must Hide() explicitly. >0 → service auto-hides after N
	 *  seconds. */
	float DurationSeconds = 0.f;
};
