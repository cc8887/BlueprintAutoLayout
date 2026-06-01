// DrivenId.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Encoding / decoding of DrivenIds.
//
// Grammar (deterministic, lossless within these forms):
//
//   Driven.Node.<NodeGuidDigits>
//   Driven.Node.<NodeGuidDigits>.<ChangeType>
//   Driven.Prop.<NodeGuidDigits>.<PropertyDisplayName>
//   Driven.Prop.Asset.<AssetPathHash>.<PropertyDisplayName>
//
//   <NodeGuidDigits>      = 32 lowercase hex chars (EGuidFormats::Digits)
//   <ChangeType>          = "Added" | "Modified" | "Removed"
//   <PropertyDisplayName> = the same string the engine uses for
//                           "DetailRowItem.<X>" — i.e. UProperty::GetDisplayName()
//   <AssetPathHash>       = 32 lowercase hex chars (FCrc32 of the full path,
//                           padded to 8 hex chars × 4 = 32 to keep parsing
//                           uniform with the Node form). Caller may also pass
//                           an opaque token via MakeAssetProp(FName Token, …).
//
// The grammar avoids embedding the asset full-name verbatim because '.' and
// '/' would clash with our separator. Hashing keeps IDs compact and FName-safe.
#pragma once

#include "CoreMinimal.h"
#include "DrivenHighlightTypes.h"

class UEdGraphNode;
class UObject;

namespace DrivenId
{
	// ── Construction ────────────────────────────────────────────────────────

	/** "Driven.Node.<guid>"  — node-level highlight, no change-type qualifier. */
	DRIVENHIGHLIGHT_API FName MakeNode(const FGuid& NodeGuid);

	/** "Driven.Node.<guid>.<ChangeType>" — convenient overload with semantic. */
	DRIVENHIGHLIGHT_API FName MakeNode(const FGuid& NodeGuid, EDrivenChangeType ChangeType);

	/** Same as above; pulls NodeGuid from the node. Returns NAME_None if Node is null. */
	DRIVENHIGHLIGHT_API FName MakeNode(const UEdGraphNode* Node);
	DRIVENHIGHLIGHT_API FName MakeNode(const UEdGraphNode* Node, EDrivenChangeType ChangeType);

	/** "Driven.Prop.<guid>.<PropertyDisplayName>" — node-property highlight. */
	DRIVENHIGHLIGHT_API FName MakeNodeProp(const FGuid& NodeGuid, FName PropertyDisplayName);
	DRIVENHIGHLIGHT_API FName MakeNodeProp(const UEdGraphNode* Node, FName PropertyDisplayName);

	/** "Driven.Prop.Asset.<hash>.<PropertyDisplayName>" — asset-level property
	 *  highlight (e.g. a property on a UMaterial / UAnimBlueprint asset itself). */
	DRIVENHIGHLIGHT_API FName MakeAssetProp(const UObject* Asset, FName PropertyDisplayName);

	// ── Decoding ────────────────────────────────────────────────────────────

	/** Logical kind a DrivenId belongs to (after parsing). */
	enum class EKind : uint8 { Unknown, Node, NodeProp, AssetProp };

	struct FParts
	{
		EKind Kind = EKind::Unknown;
		FGuid NodeGuid;                       // Node / NodeProp
		EDrivenChangeType ChangeType = EDrivenChangeType::Modified; // Node only
		bool bHasChangeType = false;
		FName PropertyDisplayName;            // NodeProp / AssetProp
		FString AssetHash;                    // AssetProp
	};

	/** Parse a DrivenId. Returns false on malformed input. */
	DRIVENHIGHLIGHT_API bool Parse(FName DrivenId, FParts& Out);

	/** Convenience: derive the engine-default detail-panel widget tag for a
	 *  property DrivenId. Returns NAME_None if the id isn't a property. */
	DRIVENHIGHLIGHT_API FName GetDetailRowTagForPropertyId(FName DrivenId);

	// ── Helpers ─────────────────────────────────────────────────────────────

	DRIVENHIGHLIGHT_API const TCHAR* ToString(EDrivenChangeType ChangeType);
	DRIVENHIGHLIGHT_API bool TryParseChangeType(const FString& Token, EDrivenChangeType& Out);
}
