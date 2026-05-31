// BALStyleVoter.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BALTypes.h"

/**
 * Votes on the global direction for placing Pure node groups
 * relative to their Exec consumers.
 *
 * Each (PureGroup, ConsumerExec) pair casts one vote for the best
 * of four directions: West / East / North / South.
 * The direction with the most votes wins and becomes globalPureDir.
 *
 * Scoring per direction:
 *   - Overlap cost  : area of candidate AABB that overlaps existing nodes (weight 3)
 *   - Wire cost     : average Manhattan distance from Pure output pins to Exec input pins
 *   - Cross cost    : how much the group would cross the horizontal exec chain axis (weight 2)
 */
class BLUEPRINTAUTOLAYOUT_API FBALStyleVoter
{
public:
	struct FVoteContext
	{
		/** All node proxies in the graph (for overlap testing). */
		const TMap<UEdGraphNode*, FBALNode>*  Proxies   = nullptr;
		/** Exec roots (to determine exec chain axis). */
		const TArray<FBALExecNode*>*          ExecRoots = nullptr;
		const FBALSettings*                   Settings  = nullptr;
	};

	/**
	 * Run the voting pass and return the winning direction.
	 * If bForceDir is set in Settings, that direction is returned immediately.
	 */
	static EBALPureDir Vote(const FVoteContext& Ctx);

private:
	struct FVoteCandidate
	{
		EBALPureDir Dir;
		float       Score;
	};

	static FVoteCandidate EvaluateDir(EBALPureDir Dir,
	                                  const TArray<FBALNode*>& PureGroup,
	                                  const FBALNode& Consumer,
	                                  const FVoteContext& Ctx);

	/** Compute the AABB that the pure group would occupy at the given dir. */
	static FBox2D ComputeCandidateAABB(EBALPureDir Dir,
	                                   const TArray<FBALNode*>& PureGroup,
	                                   const FBALNode& Consumer,
	                                   const FBALSettings& Settings);

	static float MeasureOverlap(const FBox2D& CandAABB,
	                            const TMap<UEdGraphNode*, FBALNode>& Proxies);

	static float MeasureWireCost(EBALPureDir Dir,
	                             const TArray<FBALNode*>& PureGroup,
	                             const FBALNode& Consumer);

	static float MeasureAxisCross(const FBox2D& CandAABB,
	                              const TArray<FBALExecNode*>& ExecRoots);
};
