// BALGraphAnalyzer.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BALTypes.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

/**
 * Analyses an EdGraph and produces:
 *   - A flat list of FBALNode proxies (one per node)
 *   - An ExecTree rooted at each entry-point exec node
 *   - Pure subtree assignments per Exec consumer
 *
 * Works for ALL graph types (Logic BP, Material, AnimBP) because it
 * operates purely on UEdGraphNode / UEdGraphPin — no K2/Material casts needed
 * for the structural analysis.
 */
class BLUEPRINTAUTOLAYOUT_API FBALGraphAnalyzer
{
public:
	struct FAnalysisResult
	{
		/** All node proxies, keyed by UEdGraphNode* */
		TMap<UEdGraphNode*, FBALNode>   Proxies;

		/** Roots of exec trees (one per entry-point: Event, FunctionEntry, etc.) */
		TArray<FBALExecNode*>           ExecRoots;

		/** Pure nodes that are not reachable from any exec node (true data islands). */
		TArray<FBALNode*>               IsolatedPures;

		/** All isolated nodes (no connections at all). */
		TArray<FBALNode*>               IsolatedNodes;

		/** Allocator for FBALExecNode objects (owns lifetime). */
		TArray<TUniquePtr<FBALExecNode>> ExecNodePool;

		FAnalysisResult()  = default;
		~FAnalysisResult() = default;

		// TUniquePtr is move-only → explicitly delete copy, allow move
		FAnalysisResult(const FAnalysisResult&)            = delete;
		FAnalysisResult& operator=(const FAnalysisResult&) = delete;
		FAnalysisResult(FAnalysisResult&&)                 = default;
		FAnalysisResult& operator=(FAnalysisResult&&)      = default;
	};

	/**
	 * Run full analysis on the given graph.
	 * @param Graph    Any UEdGraph (Blueprint event graph, material graph, anim graph …)
	 * @param Settings Layout settings (used for size estimation).
	 * @param Constraints Already-collected constraints (locked nodes skipped from reordering).
	 */
	static FAnalysisResult Analyze(UEdGraph* Graph,
	                               const FBALSettings& Settings,
	                               const TArray<FBALConstraint>& Constraints);

	// ── Utility ──────────────────────────────────────────────

	/** True if the node has at least one exec-type pin. */
	static bool HasExecPin(const UEdGraphNode* Node);

	/** True if the node is a pure function (no exec pins, bIsPure or material-style). */
	static bool IsPureNode(const UEdGraphNode* Node);

	/** True if node is a wire reroute / knot. */
	static bool IsKnotNode(const UEdGraphNode* Node);

	/** True if node is a comment box. */
	static bool IsCommentNode(const UEdGraphNode* Node);

	/** Estimate node size from pin count (used before Slate measures it). */
	static FVector2D EstimateNodeSize(const UEdGraphNode* Node);

	/** True if a pin carries execution flow (white arrow pins). */
	static bool IsExecPin(const UEdGraphPin* Pin);

private:
	static void ClassifyNodes(UEdGraph* Graph,
	                          const TMap<UEdGraphNode*, int32>& ConstraintMap,
	                          TMap<UEdGraphNode*, FBALNode>& OutProxies);

	static void BuildExecTree(TMap<UEdGraphNode*, FBALNode>& Proxies,
	                          TArray<FBALExecNode*>& OutRoots,
	                          TArray<TUniquePtr<FBALExecNode>>& Pool);

	static void AssignPureSubtrees(TMap<UEdGraphNode*, FBALNode>& Proxies,
	                               TArray<FBALExecNode*>& ExecRoots);

	/** DFS to collect all Pure ancestors of an Exec node. */
	static void CollectPureAncestors(UEdGraphNode* ExecNode,
	                                 TMap<UEdGraphNode*, FBALNode>& Proxies,
	                                 TArray<FBALNode*>& OutPures,
	                                 TSet<UEdGraphNode*>& Visited);

	/** Find the deepest exec node that is a common ancestor of all nodes in the set. */
	static FBALExecNode* FindLCA(const TArray<UEdGraphNode*>& Nodes,
	                             TArray<FBALExecNode*>& ExecRoots);
};
