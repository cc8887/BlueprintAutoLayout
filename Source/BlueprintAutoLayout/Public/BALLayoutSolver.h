// BALLayoutSolver.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BALTypes.h"

/**
 * Computes ideal (pre-collision) positions for all nodes.
 *
 * Pass 1 — Exec chain layout:
 *   Bottom-up subtree height calculation, then top-down coordinate assignment.
 *   Linear segments stay on the same Y (horizontal).
 *   Fork nodes split the Y band among children.
 *
 * Pass 2 — Pure "stacked tower" layout:
 *   Each Pure group is stacked tightly (zero Y gap) along the axis perpendicular
 *   to globalPureDir, placed adjacent to its consumer Exec node.
 *   Multi-depth Pure chains are layered outward from the consumer.
 *
 * Pass 3 — Isolated node packing:
 *   All isolated nodes are gathered into a block placed below-right of the main graph.
 */
class BLUEPRINTAUTOLAYOUT_API FBALLayoutSolver
{
public:
	struct FSolverInput
	{
		TMap<UEdGraphNode*, FBALNode>*  Proxies   = nullptr;
		TArray<FBALExecNode*>*          ExecRoots = nullptr;
		TArray<FBALNode*>*              IsolatedPures  = nullptr;
		TArray<FBALNode*>*              IsolatedNodes  = nullptr;
		const TArray<FBALConstraint>*   Constraints    = nullptr;
		EBALPureDir                     PureDir        = EBALPureDir::West;
		const FBALSettings*             Settings       = nullptr;
	};

	static void Solve(const FSolverInput& Input);

private:
	// ── Exec tree passes ─────────────────────────────────────

	/** Recursively compute subtree height (bottom-up). */
	static float ComputeSubtreeHeight(FBALExecNode* Node,
	                                  EBALPureDir PureDir,
	                                  const FBALSettings& Settings);

	/**
	 * Compute the vertical space reserved for the Pure group attached to an exec node.
	 * This is added to the exec node's own height when PureDir is North or South.
	 */
	static float PureReservedHeight(const FBALExecNode* Node,
	                                EBALPureDir PureDir,
	                                const FBALSettings& Settings);

	/** Recursively assign X/Y coordinates (top-down). */
	static void AssignExecCoords(FBALExecNode* Node,
	                             float X, float Y,
	                             EBALPureDir PureDir,
	                             const FBALSettings& Settings);

	// ── Pure stacking ─────────────────────────────────────────

	/** Place all Pure groups for every exec node. */
	static void PlacePureGroups(TMap<UEdGraphNode*, FBALNode>& Proxies,
	                            TArray<FBALExecNode*>& ExecRoots,
	                            EBALPureDir PureDir,
	                            const FBALSettings& Settings);

	/** Sort a Pure group to minimize wire crossings (by consumer pin Y, then depth). */
	static void SortPureGroup(TArray<FBALNode*>& Group,
	                          const FBALNode& Consumer);

	/** Place one Pure group relative to its consumer at the given direction. */
	static void StackPureGroup(const TArray<FBALNode*>& Group,
	                           const FBALNode& Consumer,
	                           EBALPureDir Dir,
	                           const FBALSettings& Settings);

	// ── Constraint avoidance ──────────────────────────────────

	/** Nudge a proposed position away from hard-constraint AABBs. */
	static FVector2D AvoidConstraintRegions(FVector2D Proposed,
	                                        FVector2D NodeSize,
	                                        const TArray<FBALConstraint>& Constraints,
	                                        const TMap<UEdGraphNode*, FBALNode>& Proxies,
	                                        EBALPureDir PureDir,
	                                        const FBALSettings& Settings);

	// ── Isolated node packing ─────────────────────────────────

	static void PackIsolatedNodes(TArray<FBALNode*>& Isolateds,
	                              const TMap<UEdGraphNode*, FBALNode>& Proxies,
	                              const FBALSettings& Settings);

	// ── Helpers ───────────────────────────────────────────────

	/** Compute the bounding box of all currently-placed exec nodes. */
	static FBox2D ComputeGraphBounds(const TMap<UEdGraphNode*, FBALNode>& Proxies);

	/** Total height of a Pure group when stacked with zero gap. */
	static float PureGroupHeight(const TArray<FBALNode*>& Group);
	static float PureGroupWidth (const TArray<FBALNode*>& Group);
};
