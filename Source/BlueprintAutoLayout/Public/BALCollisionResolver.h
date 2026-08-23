// BALCollisionResolver.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BALTypes.h"

/**
 * Iterative SAT (Separating Axis Theorem) collision resolver.
 *
 * Rules:
 *  - Hard-constrained nodes never move.
 *  - Soft-constrained nodes may move up to MaxDrift.
 *  - Free nodes share the displacement equally (half-push each).
 *  - Displacement is biased toward the minor axis (Y for horizontal graphs)
 *    to preserve exec-chain horizontality.
 *
 * After the push loop, a Compaction pass pulls free nodes back toward their
 * ideal (pre-collision) positions, keeping the layout tight.
 */
class BLUEPRINTAUTOLAYOUT_API FBALCollisionResolver
{
public:
	struct FResolveInput
	{
		TMap<UEdGraphNode*, FBALNode>*  Proxies     = nullptr;
		const TArray<FBALConstraint>*   Constraints = nullptr;
		const FBALSettings*             Settings    = nullptr;
	};

	static void Resolve(const FResolveInput& Input);

private:
	typedef TMap<int32, TArray<FBALNode*>> FRigidGroups;

	// ── Spatial hash ─────────────────────────────────────────

	/**
	 * Lightweight uniform grid for O(1) neighbor queries.
	 * Cell size is set to ~1.5× the average node width.
	 */
	struct FSpatialHash
	{
		float     CellSize = 1.f;
		float     QueryPadding = 0.f;
		TMap<TPair<int32,int32>, TArray<FBALNode*>> Cells;

		void Build(TArray<FBALNode*>& Nodes, float InCellSize, float PairMargin);
		void QueryNeighbors(const FBALNode& Node, TArray<FBALNode*>& Out) const;

	private:
		TPair<int32,int32> ToCell(FVector2D Pos) const;
		FBox2D             NodeAABB(const FBALNode& N, float Margin) const;
	};

	// ── Push loop ─────────────────────────────────────────────

	static bool RunPushIteration(TArray<FBALNode*>& Nodes,
	                             FSpatialHash& Hash,
	                             const FRigidGroups& RigidGroups,
	                             const FBALSettings& Settings);

	/**
	 * Compute the Minimum Translation Vector to separate A from B.
	 * Returns zero vector if they don't overlap.
	 */
	static FVector2D ComputeMTV(const FBALNode& A,
	                            const FBALNode& B,
	                            float Margin);

	/** Apply a movement while honoring Hard and Soft constraints. */
	static bool MoveNode(FBALNode& Node, FVector2D Delta,
	                     const FRigidGroups& RigidGroups);

	// ── Compaction ────────────────────────────────────────────

	static void RunCompaction(TArray<FBALNode*>& Nodes,
	                          const TMap<UEdGraphNode*, FVector2D>& IdealPositions,
	                          FSpatialHash& Hash,
	                          const FRigidGroups& RigidGroups,
	                          const FBALSettings& Settings);
};
