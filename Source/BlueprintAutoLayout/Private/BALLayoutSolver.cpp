// BALLayoutSolver.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALLayoutSolver.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "BALGraphAnalyzer.h"

// ─────────────────────────────────────────────────────────────
//  Public: main solver
// ─────────────────────────────────────────────────────────────

void FBALLayoutSolver::Solve(const FSolverInput& Input)
{
	if (!Input.Proxies || !Input.ExecRoots || !Input.Settings) return;

	const FBALSettings& S = *Input.Settings;

	// ── Pass 1: Exec chain ───────────────────────────────────

	// Compute subtree heights bottom-up
	for (FBALExecNode* Root : *Input.ExecRoots)
	{
		ComputeSubtreeHeight(Root, Input.PureDir, S);
	}

	// Assign coordinates top-down
	// Stack multiple roots vertically
	float CurY = 0.f;
	for (FBALExecNode* Root : *Input.ExecRoots)
	{
		// Skip locked roots — use their original position as the anchor
		if (Root->Proxy->bLocked)
		{
			CurY = FMath::Max(CurY,
			                  Root->Proxy->OriginalPos.Y + Root->SubtreeH + S.GapY);
			AssignExecCoords(Root, Root->Proxy->OriginalPos.X,
			                 Root->Proxy->OriginalPos.Y, Input.PureDir, S);
		}
		else
		{
			AssignExecCoords(Root, 0.f, CurY, Input.PureDir, S);
			CurY += Root->SubtreeH + S.GapY * 2.f;
		}
	}

	// ── Pass 2: Pure groups ──────────────────────────────────
	PlacePureGroups(*Input.Proxies, *Input.ExecRoots, Input.PureDir, S);

	// ── Pass 3: Isolated nodes ───────────────────────────────
	if (Input.IsolatedPures && Input.IsolatedNodes)
	{
		TArray<FBALNode*> AllIsolated;
		AllIsolated.Append(*Input.IsolatedPures);
		AllIsolated.Append(*Input.IsolatedNodes);
		PackIsolatedNodes(AllIsolated, *Input.Proxies, S);
	}
}

// ─────────────────────────────────────────────────────────────
//  Pass 1 helpers: subtree height (bottom-up)
// ─────────────────────────────────────────────────────────────

float FBALLayoutSolver::ComputeSubtreeHeight(FBALExecNode* Node,
                                              EBALPureDir PureDir,
                                              const FBALSettings& S)
{
	float OwnHeight = Node->Proxy->Size.Y + PureReservedHeight(Node, PureDir, S);

	if (Node->Children.IsEmpty())
	{
		Node->SubtreeH = OwnHeight;
		return Node->SubtreeH;
	}

	if (Node->Children.Num() == 1)
	{
		// Linear — height is max of own and child
		float ChildH = ComputeSubtreeHeight(Node->Children[0], PureDir, S);
		Node->SubtreeH = FMath::Max(OwnHeight, ChildH);
		return Node->SubtreeH;
	}

	// Fork — sum children heights + gaps
	float TotalChildH = 0.f;
	for (FBALExecNode* Child : Node->Children)
	{
		TotalChildH += ComputeSubtreeHeight(Child, PureDir, S);
	}
	TotalChildH += S.GapY * (Node->Children.Num() - 1);

	Node->SubtreeH = FMath::Max(OwnHeight, TotalChildH);
	return Node->SubtreeH;
}

float FBALLayoutSolver::PureReservedHeight(const FBALExecNode* Node,
                                            EBALPureDir PureDir,
                                            const FBALSettings& S)
{
	// Only North/South Pure placement adds to the vertical footprint
	if (PureDir != EBALPureDir::North && PureDir != EBALPureDir::South)
		return 0.f;

	return PureGroupHeight(Node->PureGroup) + S.GapY;
}

// ─────────────────────────────────────────────────────────────
//  Pass 1 helpers: coordinate assignment (top-down)
// ─────────────────────────────────────────────────────────────

void FBALLayoutSolver::AssignExecCoords(FBALExecNode* Node,
                                         float X, float Y,
                                         EBALPureDir PureDir,
                                         const FBALSettings& S)
{
	if (!Node || !Node->Proxy) return;

	// Locked nodes keep their original position but we still recurse children
	if (!Node->Proxy->bLocked)
	{
		// Vertically center the node within its allocated SubtreeH band
		float CenterY = Y + (Node->SubtreeH - Node->Proxy->Size.Y) * 0.5f;
		Node->Proxy->OutPos = FVector2D(X, CenterY);
	}

	float NextX = Node->Proxy->OutPos.X + Node->Proxy->Size.X + S.GapX;

	if (Node->Children.Num() == 0)
	{
		// Leaf — nothing more
	}
	else if (Node->Children.Num() == 1)
	{
		// Linear segment — same Y band start
		AssignExecCoords(Node->Children[0], NextX, Y, PureDir, S);
	}
	else
	{
		// Fork — distribute children into vertical bands
		float CurY = Y;
		for (FBALExecNode* Child : Node->Children)
		{
			AssignExecCoords(Child, NextX, CurY, PureDir, S);
			CurY += Child->SubtreeH + S.GapY;
		}
	}
}

// ─────────────────────────────────────────────────────────────
//  Pass 2: Pure group stacking
// ─────────────────────────────────────────────────────────────

void FBALLayoutSolver::PlacePureGroups(TMap<UEdGraphNode*, FBALNode>& Proxies,
                                        TArray<FBALExecNode*>& ExecRoots,
                                        EBALPureDir PureDir,
                                        const FBALSettings& S)
{
	TFunction<void(FBALExecNode*)> Traverse = [&](FBALExecNode* EN)
	{
		if (!EN) return;

		if (EN->PureGroup.Num() > 0)
		{
			SortPureGroup(EN->PureGroup, *EN->Proxy);
			StackPureGroup(EN->PureGroup, *EN->Proxy, PureDir, S);
		}

		for (FBALExecNode* Child : EN->Children) Traverse(Child);
	};

	for (FBALExecNode* Root : ExecRoots) Traverse(Root);
}

void FBALLayoutSolver::SortPureGroup(TArray<FBALNode*>& Group,
                                      const FBALNode& Consumer)
{
	// Sort by:
	//   1. PureDepth ascending (shallowest / most-direct first)
	//   2. Within same depth: consumer input pin Y position (minimises crossings)
	//      Approximate using OriginalPos.Y of the Pure node
	//   3. Width descending (wider nodes at the bottom to avoid blocking wires)
	Group.Sort([](const FBALNode& A, const FBALNode& B)
	{
		if (A.PureDepth != B.PureDepth)
			return A.PureDepth < B.PureDepth;
		// proxy original Y as consumer pin position approximation
		if (!FMath::IsNearlyEqual(A.OriginalPos.Y, B.OriginalPos.Y, 1.0))
			return A.OriginalPos.Y < B.OriginalPos.Y;
		return A.Size.X > B.Size.X; // wider first at bottom => sort descending = put smaller on top
	});
}

void FBALLayoutSolver::StackPureGroup(const TArray<FBALNode*>& Group,
                                       const FBALNode& Consumer,
                                       EBALPureDir Dir,
                                       const FBALSettings& S)
{
	if (Group.IsEmpty()) return;

	// Compute total group dimensions grouped by PureDepth layer
	// depth 0 = closest to consumer, depth 1 = next layer out, etc.
	int32 MaxDepth = 0;
	for (const FBALNode* P : Group)
		MaxDepth = FMath::Max(MaxDepth, P->PureDepth);

	// For each depth layer compute the column width (widest node in that layer)
	TArray<float> LayerWidth;
	LayerWidth.SetNumZeroed(MaxDepth + 1);
	TArray<float> LayerHeight;
	LayerHeight.SetNumZeroed(MaxDepth + 1);

	for (const FBALNode* P : Group)
	{
		LayerWidth[P->PureDepth]  = FMath::Max(LayerWidth[P->PureDepth],  P->Size.X);
		LayerHeight[P->PureDepth] += P->Size.Y; // stacked tightly, no gap
	}

	// Place each layer
	// For West direction: depth 0 is rightmost (adjacent to consumer), deeper = further left
	// For East direction: depth 0 is leftmost (adjacent to consumer), deeper = further right
	// For North/South:  depth 0 is closest (largest Y-wise offset), layers stack outward

	// Compute total height of depth-0 layer (the tallest is what we center on)
	float TotalH0 = LayerHeight[0];
	float ConsumerCenterY = Consumer.OutPos.Y + Consumer.Size.Y * 0.5f;

	switch (Dir)
	{
	case EBALPureDir::West:
	{
		// Depth-0 column is at Consumer.X - LayerWidth[0] - GapX
		// Each deeper layer goes further left
		float LayerX = Consumer.OutPos.X - LayerWidth[0] - S.GapX;
		for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
		{
			float LayerStartY = ConsumerCenterY - LayerHeight[Depth] * 0.5f;
			float CurY = LayerStartY;

			for (FBALNode* P : Group)
			{
				if (P->PureDepth != Depth) continue;
				if (P->bLocked) continue; // never move locked nodes

				// Right-align within the column so output pins touch the next layer
				float NodeX = LayerX + LayerWidth[Depth] - P->Size.X;
				P->OutPos = FVector2D(NodeX, CurY);
				CurY += P->Size.Y; // zero gap — stacked tightly
			}

			if (Depth < MaxDepth)
				LayerX -= LayerWidth[Depth + 1] + S.PureGapX;
		}
		break;
	}

	case EBALPureDir::East:
	{
		float LayerX = Consumer.OutPos.X + Consumer.Size.X + S.GapX;
		for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
		{
			float LayerStartY = ConsumerCenterY - LayerHeight[Depth] * 0.5f;
			float CurY = LayerStartY;

			for (FBALNode* P : Group)
			{
				if (P->PureDepth != Depth) continue;
				if (P->bLocked) continue;
				P->OutPos = FVector2D(LayerX, CurY);
				CurY += P->Size.Y;
			}

			if (Depth < MaxDepth)
				LayerX += LayerWidth[Depth] + S.PureGapX;
		}
		break;
	}

	case EBALPureDir::North:
	{
		// Stack vertically above consumer; depth 0 is closest (highest Y = just above)
		float LayerY = Consumer.OutPos.Y - LayerHeight[0] - S.GapY;
		float ConsumerCenterX = Consumer.OutPos.X + Consumer.Size.X * 0.5f;

		for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
		{
			float TotalW = 0.f;
			for (const FBALNode* P : Group)
				if (P->PureDepth == Depth) TotalW += P->Size.X;

			float CurX = ConsumerCenterX - TotalW * 0.5f;

			for (FBALNode* P : Group)
			{
				if (P->PureDepth != Depth) continue;
				if (P->bLocked) continue;
				P->OutPos = FVector2D(CurX, LayerY);
				CurX += P->Size.X;
			}

			if (Depth < MaxDepth)
				LayerY -= LayerHeight[Depth + 1] + S.GapY;
		}
		break;
	}

	case EBALPureDir::South:
	{
		float LayerY = Consumer.OutPos.Y + Consumer.Size.Y + S.GapY;
		float ConsumerCenterX = Consumer.OutPos.X + Consumer.Size.X * 0.5f;

		for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
		{
			float TotalW = 0.f;
			for (const FBALNode* P : Group)
				if (P->PureDepth == Depth) TotalW += P->Size.X;

			float CurX = ConsumerCenterX - TotalW * 0.5f;

			for (FBALNode* P : Group)
			{
				if (P->PureDepth != Depth) continue;
				if (P->bLocked) continue;
				P->OutPos = FVector2D(CurX, LayerY);
				CurX += P->Size.X;
			}

			if (Depth < MaxDepth)
				LayerY += LayerHeight[Depth] + S.GapY;
		}
		break;
	}
	}
}

// ─────────────────────────────────────────────────────────────
//  Pass 3: isolated node packing
// ─────────────────────────────────────────────────────────────

void FBALLayoutSolver::PackIsolatedNodes(TArray<FBALNode*>& Isolateds,
                                          const TMap<UEdGraphNode*, FBALNode>& Proxies,
                                          const FBALSettings& S)
{
	if (Isolateds.IsEmpty()) return;

	FBox2D GraphBounds = ComputeGraphBounds(Proxies);

	// Place the isolated block in the bottom-right corner
	float StartX = GraphBounds.Max.X + S.GapX * 4.f;
	float StartY = GraphBounds.Max.Y + S.GapY * 4.f;

	// Simple row-wrapping layout with a max row width of 1200px
	const float MaxRowWidth = 1200.f;
	float CurX = StartX;
	float CurY = StartY;
	float RowMaxH = 0.f;

	for (FBALNode* N : Isolateds)
	{
		if (N->bLocked) continue;

		if (CurX + N->Size.X > StartX + MaxRowWidth && CurX > StartX)
		{
			// Wrap to next row
			CurX = StartX;
			CurY += RowMaxH + S.GapY;
			RowMaxH = 0.f;
		}

		N->OutPos = FVector2D(CurX, CurY);
		CurX += N->Size.X + S.GapX;
		RowMaxH = FMath::Max(RowMaxH, N->Size.Y);
	}
}

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────

FBox2D FBALLayoutSolver::ComputeGraphBounds(const TMap<UEdGraphNode*, FBALNode>& Proxies)
{
	FBox2D Bounds(EForceInit::ForceInit);
	bool bFirst = true;

	for (const auto& KV : Proxies)
	{
		const FBALNode& N = KV.Value;
		if (N.Role == EBALNodeRole::Isolated || N.Role == EBALNodeRole::Comment) continue;

		FBox2D NodeBox(N.OutPos, N.OutPos + N.Size);
		if (bFirst)
		{
			Bounds = NodeBox;
			bFirst = false;
		}
		else
		{
			Bounds = Bounds + NodeBox;
		}
	}

	if (bFirst) Bounds = FBox2D(FVector2D::ZeroVector, FVector2D(800.f, 600.f));
	return Bounds;
}

float FBALLayoutSolver::PureGroupHeight(const TArray<FBALNode*>& Group)
{
	float H = 0.f;
	for (const FBALNode* P : Group) H += P->Size.Y;
	return H;
}

float FBALLayoutSolver::PureGroupWidth(const TArray<FBALNode*>& Group)
{
	float W = 0.f;
	for (const FBALNode* P : Group) W = FMath::Max(W, P->Size.X);
	return W;
}
