// BALConstraintCollector.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BALTypes.h"

class UEdGraph;

/**
 * Scans an EdGraph and collects position constraints.
 *
 * Constraint priority (highest first):
 *  1. User-explicit pin  — metadata key "BAL_Pin" on the node
 *  2. Outside-selection  — when LayoutSelection() is called, unselected nodes are Hard
 *  3. Comment box members — treated as RigidGroup
 *  4. Knot nodes          — Soft with small drift (they follow their wire)
 */
class BLUEPRINTAUTOLAYOUT_API FBALConstraintCollector
{
public:
	/**
	 * Collect constraints for the full graph.
	 * @param Graph         Target graph (any type).
	 * @param OutConstraints Populated with all discovered constraints.
	 */
	static void Collect(UEdGraph* Graph,
	                    TArray<FBALConstraint>& OutConstraints);

	/**
	 * Collect constraints, treating every node OUTSIDE Selection as Hard.
	 */
	static void CollectForSelection(UEdGraph* Graph,
	                                const TSet<UEdGraphNode*>& Selection,
	                                TArray<FBALConstraint>& OutConstraints);

	// ── Metadata helpers ──────────────────────────────────────

	/** Returns true if the node carries an explicit pin tag. */
	static bool IsPinned(const UEdGraphNode* Node);

	/** Set / clear the pin tag on a node (writes to NodeComment metadata). */
	static void SetPinned(UEdGraphNode* Node, bool bPinned);

private:
	static void CollectCommentGroups(UEdGraph* Graph,
	                                 TArray<FBALConstraint>& OutConstraints,
	                                 TMap<UEdGraphNode*, int32>& OutGroupMap);

	static const TCHAR* GetPinMetaKey() { return TEXT("[BAL:pin]"); }
};
