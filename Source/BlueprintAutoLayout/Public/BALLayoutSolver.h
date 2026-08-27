// BALLayoutSolver.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BALTypes.h"

/**
 * Computes ideal (pre-collision) positions for all nodes.
 *
 * Pass 1 - Layered graph layout:
 *   Execution and data-only components are assigned deterministic layers.
 *   Barycentric sweeps reduce crossings and median pin alignment straightens wires.
 *
 * Pass 2 - Pure cluster layout:
 *   Each owned Pure group is arranged outward from its consumer along PureDir.
 *   West/East parameter depths are helixed into bounded-height columns.
 *
 * Pass 3 - Stable anchoring and component packing:
 *   Locked boundaries and component anchors stay stable while disconnected regions
 *   are packed without overlap.
 */
class BLUEPRINTAUTOLAYOUT_API FBALLayoutSolver
{
public:
	struct FSolverInput
	{
		TMap<UEdGraphNode*, FBALNode>*  Proxies   = nullptr;
		TArray<FBALExecNode*>*          ExecRoots = nullptr;
		TArray<FBALEdge>*               Edges     = nullptr;
		TArray<FBALComponent>*          Components = nullptr;
		TArray<FBALNode*>*              IsolatedPures  = nullptr;
		TArray<FBALNode*>*              IsolatedNodes  = nullptr;
		const TArray<FBALConstraint>*   Constraints    = nullptr;
		EBALPureDir                     PureDir        = EBALPureDir::West;
		const FBALSettings*             Settings       = nullptr;
	};

	static void Solve(const FSolverInput& Input);

private:
	static void LayoutExecComponent(const FBALComponent& Component,
	                                TMap<UEdGraphNode*, FBALNode>& Proxies,
	                                TArray<FBALEdge>& Edges,
	                                const TMap<UEdGraphNode*, TArray<FBALNode*>>& OwnedPuresByConsumer,
	                                EBALPureDir PureDir,
	                                const FBALSettings& Settings);

	static void LayoutDataComponent(const FBALComponent& Component,
	                                TMap<UEdGraphNode*, FBALNode>& Proxies,
	                                TArray<FBALEdge>& Edges,
	                                const FBALSettings& Settings);

	static void ReduceCrossings(TArray<TArray<FBALNode*>>& Layers,
	                            const TArray<FBALEdge>& Edges,
	                            const FBALSettings& Settings);

	static void AlignLayers(TArray<TArray<FBALNode*>>& Layers,
	                        const TArray<FBALEdge>& Edges,
	                        const TMap<UEdGraphNode*, float>& ClusterHeights,
	                        const TMap<UEdGraphNode*, float>& ClusterTopOffsets,
	                        const FBALSettings& Settings);

	static void PlacePureGroups(TMap<UEdGraphNode*, FBALNode>& Proxies,
	                            const TArray<FBALEdge>& Edges,
	                            TMap<UEdGraphNode*, TArray<FBALNode*>>& OwnedPuresByConsumer,
	                            EBALPureDir PureDir,
	                            const FBALSettings& Settings);

	static void PlaceKnotNodes(TMap<UEdGraphNode*, FBALNode>& Proxies,
	                           const FBALSettings& Settings);

	static void PreserveComponentAnchors(TArray<FBALComponent>& Components,
	                                     TMap<UEdGraphNode*, FBALNode>& Proxies,
	                                     const FBALSettings& Settings);

	static void PackOverlappingComponents(TArray<FBALComponent>& Components,
	                                      TMap<UEdGraphNode*, FBALNode>& Proxies,
	                                      const FBALSettings& Settings);

	static float EstimatePinOffset(const FBALNode& Node, const UEdGraphPin* Pin);
	static float PureClusterHeight(const FBALNode& Consumer,
	                               const TArray<FBALNode*>& OwnedPures,
	                               EBALPureDir PureDir,
	                               const FBALSettings& Settings);
	static float PureLeftExtension(const TArray<FBALNode*>& OwnedPures,
	                               const FBALSettings& Settings);
	static void ClampSoftConstraint(FBALNode& Node);
	static FBox2D ComponentBounds(const FBALComponent& Component,
	                              const TMap<UEdGraphNode*, FBALNode>& Proxies);
};
