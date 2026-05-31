// BALCommitter.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALCommitter.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "BlueprintAutoLayout"

int32 FBALCommitter::Commit(UEdGraph* Graph,
                             const TMap<UEdGraphNode*, FBALNode>& Proxies,
                             const TArray<FBALConstraint>&        Constraints,
                             const FBALSettings&                  Settings)
{
	if (!Graph) return 0;

	// Build fast Hard-constraint lookup
	TSet<UEdGraphNode*> HardLocked;
	for (const FBALConstraint& C : Constraints)
	{
		if (C.Type == EBALConstraintType::Hard || C.Type == EBALConstraintType::RigidGroup)
			HardLocked.Add(C.Node);
	}

	// Pre-filter: determine which nodes actually need updating
	TArray<TPair<UEdGraphNode*, FVector2D>> ToWrite;
	for (const auto& KV : Proxies)
	{
		UEdGraphNode* Node = KV.Key;
		const FBALNode& Proxy = KV.Value;

		if (!Node) continue;
		if (HardLocked.Contains(Node)) continue;

		FVector2D Snapped = SnapToGrid(Proxy.OutPos, Settings.GridSnap);
		FVector2D Old     = FVector2D((float)Node->NodePosX, (float)Node->NodePosY);

		if ((Snapped - Old).Size() < Settings.MoveThreshold) continue;

		ToWrite.Add({ Node, Snapped });
	}

	if (ToWrite.IsEmpty()) return 0;

	// Post-snap de-collision: ensure snapped positions don't re-introduce overlaps.
	// We do a fast O(n^2) scan and push any still-overlapping pair apart by GridSnap.
	if (Settings.GridSnap > 0.f)
	{
		// Build a mutable list of (node, snappedPos, size) for collision check
		TArray<TPair<int32, FVector2D>> Snap; // index → snappedPos
		for (int32 i = 0; i < ToWrite.Num(); ++i)
			Snap.Add({i, ToWrite[i].Value});

		// Fetch node sizes (from the original proxies)
		auto GetSize = [&](int32 i) -> FVector2D {
			const FBALNode* P = Proxies.Find(ToWrite[i].Key);
			return P ? P->Size : FVector2D(200.f, 80.f);
		};

		// Iterative correction: push overlapping pairs apart by GridSnap until settled
		// (needed because one push may not be enough for large overlaps)
		const int32 MaxSnapIter = 100;
		for (int32 Iter = 0; Iter < MaxSnapIter; ++Iter)
		{
			bool bAnyOverlap = false;
			for (int32 i = 0; i < Snap.Num(); ++i)
			for (int32 j = i+1; j < Snap.Num(); ++j)
			{
				FVector2D PosI = Snap[i].Value, PosJ = Snap[j].Value;
				FVector2D SzI  = GetSize(i),    SzJ  = GetSize(j);

				float OverlapX = FMath::Min(PosI.X + SzI.X, PosJ.X + SzJ.X)
				               - FMath::Max(PosI.X, PosJ.X);
				float OverlapY = FMath::Min(PosI.Y + SzI.Y, PosJ.Y + SzJ.Y)
				               - FMath::Max(PosI.Y, PosJ.Y);

				if (OverlapX > 0.f && OverlapY > 0.f)
				{
					bAnyOverlap = true;
					// Push j away from i by one grid step on the smaller-overlap axis
					if (OverlapX <= OverlapY)
					{
						float Sign = (PosI.X <= PosJ.X) ? 1.f : -1.f;
						Snap[j].Value.X += Sign * Settings.GridSnap;
					}
					else
					{
						float Sign = (PosI.Y <= PosJ.Y) ? 1.f : -1.f;
						Snap[j].Value.Y += Sign * Settings.GridSnap;
					}
				}
			}
			if (!bAnyOverlap) break;
		}

		// Write corrected positions back
		for (int32 i = 0; i < ToWrite.Num(); ++i)
			ToWrite[i].Value = Snap[i].Value;
	}

	// Single transaction for all writes
	FScopedTransaction Transaction(LOCTEXT("AutoLayout", "Auto Layout Blueprint Graph"));
	Graph->Modify();

	for (auto& [Node, NewPos] : ToWrite)
	{
		Node->Modify();
		Node->NodePosX = FMath::RoundToInt(NewPos.X);
		Node->NodePosY = FMath::RoundToInt(NewPos.Y);
	}

	// Notify graph of change
	Graph->NotifyGraphChanged();

	return ToWrite.Num();
}

FVector2D FBALCommitter::SnapToGrid(FVector2D Pos, float GridSnap)
{
	if (GridSnap <= 0.f) return Pos;
	return FVector2D(
		FMath::RoundToFloat(Pos.X / GridSnap) * GridSnap,
		FMath::RoundToFloat(Pos.Y / GridSnap) * GridSnap
	);
}

#undef LOCTEXT_NAMESPACE