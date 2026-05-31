// BALCommitter.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BALTypes.h"

class UEdGraph;

/**
 * Writes computed positions back to UEdGraphNode objects via a single
 * FScopedTransaction.  Only nodes whose position actually changed beyond
 * MoveThreshold (and which are not Hard-constrained) are marked dirty.
 *
 * All coordinates are snapped to the grid before comparison / writing.
 */
class BLUEPRINTAUTOLAYOUT_API FBALCommitter
{
public:
	/**
	 * @param Graph        The owning graph (gets Modify() called).
	 * @param Proxies      All layout proxies after collision resolution.
	 * @param Constraints  Used to skip Hard-constrained nodes.
	 * @param Settings     Grid snap + move threshold.
	 * @return             Number of nodes actually written.
	 */
	static int32 Commit(UEdGraph* Graph,
	                    const TMap<UEdGraphNode*, FBALNode>& Proxies,
	                    const TArray<FBALConstraint>&        Constraints,
	                    const FBALSettings&                  Settings);

private:
	static FVector2D SnapToGrid(FVector2D Pos, float GridSnap);
};
