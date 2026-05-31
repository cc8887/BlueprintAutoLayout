// BALCollisionResolver.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALCollisionResolver.h"
#include "EdGraph/EdGraphNode.h"

// ─────────────────────────────────────────────────────────────
//  Spatial Hash
// ─────────────────────────────────────────────────────────────

void FBALCollisionResolver::FSpatialHash::Build(TArray<FBALNode*>& Nodes, float InCellSize)
{
	CellSize = FMath::Max(InCellSize, 1.f);
	Cells.Empty();

	for (FBALNode* Node : Nodes)
	{
		if (!Node) continue;
		// Insert the node into every cell it overlaps (with margin)
		FBox2D Box = NodeAABB(*Node, 0.f);
		TPair<int32,int32> MinCell = ToCell(Box.Min);
		TPair<int32,int32> MaxCell = ToCell(Box.Max);

		for (int32 CX = MinCell.Key;   CX <= MaxCell.Key;   ++CX)
		for (int32 CY = MinCell.Value; CY <= MaxCell.Value; ++CY)
		{
			Cells.FindOrAdd({CX, CY}).Add(Node);
		}
	}
}

void FBALCollisionResolver::FSpatialHash::QueryNeighbors(const FBALNode& Node,
                                                          TArray<FBALNode*>& Out) const
{
	FBox2D Box = NodeAABB(Node, 0.f);
	// Expand by one cell to catch near-misses
	FVector2D Expand(CellSize, CellSize);
	TPair<int32,int32> MinCell = ToCell(Box.Min - Expand);
	TPair<int32,int32> MaxCell = ToCell(Box.Max + Expand);

	TSet<FBALNode*> Seen;
	for (int32 CX = MinCell.Key;   CX <= MaxCell.Key;   ++CX)
	for (int32 CY = MinCell.Value; CY <= MaxCell.Value; ++CY)
	{
		const TArray<FBALNode*>* Cell = Cells.Find({CX, CY});
		if (!Cell) continue;
		for (FBALNode* N : *Cell)
		{
			if (N != &Node && !Seen.Contains(N))
			{
				Seen.Add(N);
				Out.Add(N);
			}
		}
	}
}

TPair<int32,int32> FBALCollisionResolver::FSpatialHash::ToCell(FVector2D Pos) const
{
	return { FMath::FloorToInt(Pos.X / CellSize),
	         FMath::FloorToInt(Pos.Y / CellSize) };
}

FBox2D FBALCollisionResolver::FSpatialHash::NodeAABB(const FBALNode& N, float Margin) const
{
	return FBox2D(N.OutPos - FVector2D(Margin, Margin),
	              N.OutPos + N.Size + FVector2D(Margin, Margin));
}

// ─────────────────────────────────────────────────────────────
//  Public: Resolve
// ─────────────────────────────────────────────────────────────

void FBALCollisionResolver::Resolve(const FResolveInput& Input)
{
	if (!Input.Proxies || !Input.Settings) return;

	const FBALSettings& S = *Input.Settings;

	// Build flat node list
	TArray<FBALNode*> Nodes;
	for (auto& KV : *Input.Proxies)
	{
		FBALNode* N = &KV.Value;
		if (N->Role == EBALNodeRole::Comment) continue; // comments handled as rigid bodies separately
		Nodes.Add(N);
	}

	if (Nodes.IsEmpty()) return;

	// Estimate average node size for cell size
	float AvgWidth = 0.f;
	for (const FBALNode* N : Nodes) AvgWidth += N->Size.X;
	AvgWidth = Nodes.IsEmpty() ? 200.f : AvgWidth / Nodes.Num();

	// Save ideal positions (before collision resolution)
	TMap<UEdGraphNode*, FVector2D> IdealPositions;
	for (const FBALNode* N : Nodes)
		IdealPositions.Add(N->GraphNode, N->OutPos);

	// Build spatial hash
	FSpatialHash Hash;
	Hash.Build(Nodes, AvgWidth * 1.5f);

	// Iterative push loop
	bool bFullyResolved = false;
	for (int32 Iter = 0; Iter < S.CollisionIter; ++Iter)
	{
		bool bAnyOverlap = RunPushIteration(Nodes, Hash, S);

		// Rebuild hash after each iteration (positions changed)
		Hash.Build(Nodes, AvgWidth * 1.5f);

		if (!bAnyOverlap)
		{
			bFullyResolved = true;
			break;
		}
	}

	// Compaction pass: only run when push has converged (no more overlaps).
	// Skipping compaction when overlaps still remain avoids corrupting the
	// partially-resolved layout with misguided pull-back moves.
	if (bFullyResolved)
	{
		RunCompaction(Nodes, IdealPositions, Hash, S);
	}
}

// ─────────────────────────────────────────────────────────────
//  Private: push iteration
// ─────────────────────────────────────────────────────────────

bool FBALCollisionResolver::RunPushIteration(TArray<FBALNode*>& Nodes,
                                              FSpatialHash& Hash,
                                              const FBALSettings& S)
{
	bool bAnyOverlap = false;

	// Build index map once for O(1) pair de-duplication
	TMap<FBALNode*, int32> IndexMap;
	IndexMap.Reserve(Nodes.Num());
	for (int32 k = 0; k < Nodes.Num(); ++k)
		IndexMap.Add(Nodes[k], k);

	for (int32 iA = 0; iA < Nodes.Num(); ++iA)
	{
		FBALNode* A = Nodes[iA];
		TArray<FBALNode*> Neighbors;
		Hash.QueryNeighbors(*A, Neighbors);

		for (FBALNode* B : Neighbors)
		{
			// Process each pair once using stable array-index ordering
			const int32* pIB = IndexMap.Find(B);
			if (!pIB || *pIB <= iA) continue;

			FVector2D MTV = ComputeMTV(*A, *B, S.NodeMargin);
			if (MTV.IsNearlyZero(0.01)) continue;

			bAnyOverlap = true;

			// Determine lock status
			const bool bALocked = A->bLocked;
			const bool bBLocked = B->bLocked;

			if (bALocked && bBLocked)
			{
				// Both hard — can't do anything
				continue;
			}
			else if (bALocked && !bBLocked)
			{
				// A is fixed — push B the full MTV
				// Bias: try to push along Y first to preserve horizontal flow
				FVector2D Push = MTV;
				if (FMath::Abs(MTV.X) < FMath::Abs(MTV.Y))
					Push = FVector2D(0.f, MTV.Y); // push only vertically if Y is major
				B->OutPos += Push;
			}
			else if (!bALocked && bBLocked)
			{
				FVector2D Push = MTV;
				if (FMath::Abs(MTV.X) < FMath::Abs(MTV.Y))
					Push = FVector2D(0.f, MTV.Y);
				A->OutPos -= Push;
			}
			else
			{
				// Both free — share the push
				FVector2D Push = MTV;
				if (FMath::Abs(MTV.X) < FMath::Abs(MTV.Y))
					Push = FVector2D(0.f, MTV.Y);

				A->OutPos -= Push * 0.5f;
				B->OutPos += Push * 0.5f;
			}
		}
	}

	return bAnyOverlap;
}

FVector2D FBALCollisionResolver::ComputeMTV(const FBALNode& A,
                                             const FBALNode& B,
                                             float Margin)
{
	// AABB with margin
	FBox2D BoxA(A.OutPos - FVector2D(Margin, Margin),
	             A.OutPos + A.Size + FVector2D(Margin, Margin));
	FBox2D BoxB(B.OutPos - FVector2D(Margin, Margin),
	             B.OutPos + B.Size + FVector2D(Margin, Margin));

	// Check overlap on each axis
	float OverlapX = FMath::Min(BoxA.Max.X, BoxB.Max.X) - FMath::Max(BoxA.Min.X, BoxB.Min.X);
	float OverlapY = FMath::Min(BoxA.Max.Y, BoxB.Max.Y) - FMath::Max(BoxA.Min.Y, BoxB.Min.Y);

	if (OverlapX <= 0.f || OverlapY <= 0.f)
		return FVector2D::ZeroVector; // no overlap

	// Choose the axis with smaller overlap (SAT minimum translation)
	FVector2D CenterA = BoxA.GetCenter();
	FVector2D CenterB = BoxB.GetCenter();

	if (OverlapX < OverlapY)
	{
		// Separate along X: push A left if it is to the left of B, else right
		// When centers are exactly equal, use GraphNode pointer for stable tie-breaking
		float Sign;
		if (!FMath::IsNearlyZero(CenterA.X - CenterB.X, 0.01f))
			Sign = (CenterA.X < CenterB.X) ? -1.f : 1.f;
		else
			Sign = (A.GraphNode < B.GraphNode) ? -1.f : 1.f;
		return FVector2D(Sign * OverlapX, 0.f);
	}
	else
	{
		// Separate along Y
		float Sign;
		if (!FMath::IsNearlyZero(CenterA.Y - CenterB.Y, 0.01f))
			Sign = (CenterA.Y < CenterB.Y) ? -1.f : 1.f;
		else
			Sign = (A.GraphNode < B.GraphNode) ? -1.f : 1.f;
		return FVector2D(0.f, Sign * OverlapY);
	}
}

// ─────────────────────────────────────────────────────────────
//  Private: compaction
// ─────────────────────────────────────────────────────────────

void FBALCollisionResolver::RunCompaction(TArray<FBALNode*>& Nodes,
                                           const TMap<UEdGraphNode*, FVector2D>& IdealPositions,
                                           FSpatialHash& Hash,
                                           const FBALSettings& S)
{
	// Estimate average width for hash cell size
	float AvgWidth = 0.f;
	for (const FBALNode* N : Nodes) AvgWidth += N->Size.X;
	AvgWidth = Nodes.IsEmpty() ? 200.f : AvgWidth / Nodes.Num();

	// Multiple passes: pull each free node toward its ideal position
	// without re-creating overlaps.  Rebuild hash after each node move
	// so subsequent checks see accurate positions.
	for (FBALNode* Node : Nodes)
	{
		if (Node->bLocked) continue;

		const FVector2D* IdealPtr = IdealPositions.Find(Node->GraphNode);
		if (!IdealPtr) continue;

		FVector2D Ideal   = *IdealPtr;
		FVector2D Current = Node->OutPos;
		FVector2D Delta   = Ideal - Current;

		if (Delta.IsNearlyZero(1.0)) continue;

		// Try moving halfway toward ideal
		FVector2D Candidate = Current + Delta * 0.5f;

		// Check if candidate overlaps any neighbor
		FBALNode TempNode = *Node;
		TempNode.OutPos = Candidate;

		// Query neighbors at the CANDIDATE position for an accurate collision check
		TArray<FBALNode*> Neighbors;
		Hash.QueryNeighbors(TempNode, Neighbors);

		bool bOverlaps = false;
		for (const FBALNode* N : Neighbors)
		{
			if (ComputeMTV(TempNode, *N, S.NodeMargin).SizeSquared() > 0.01)
			{
				bOverlaps = true;
				break;
			}
		}

		if (!bOverlaps)
		{
			Node->OutPos = Candidate;
			// Rebuild hash so the next node's neighbor query reflects this move
			Hash.Build(Nodes, AvgWidth * 1.5f);
		}
	}
}
