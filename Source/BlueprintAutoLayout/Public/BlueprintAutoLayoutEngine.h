// BlueprintAutoLayoutEngine.h — Public entry point
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BALTypes.h"

class UEdGraph;

/**
 * Main facade.  Orchestrates the five-stage pipeline:
 *   ConstraintCollector → GraphAnalyzer → StyleVoter → LayoutSolver
 *     → CollisionResolver → Committer
 *
 * Supports any UEdGraph subclass (EventGraph, MaterialGraph, AnimGraph, etc.)
 */
class BLUEPRINTAUTOLAYOUT_API FBlueprintAutoLayoutEngine
{
public:
	/** Auto-layout the entire graph. */
	static void Layout(UEdGraph* Graph,
	                   const FBALSettings& Settings = FBALSettings());

	/** Auto-layout only the selected nodes; unselected nodes become Hard constraints. */
	static void LayoutSelection(UEdGraph* Graph,
	                            const TSet<UEdGraphNode*>& Selection,
	                            const FBALSettings& Settings = FBALSettings());

	/** Layout with externally supplied constraints (useful for scripting). */
	static void LayoutWithConstraints(UEdGraph* Graph,
	                                  const TArray<FBALConstraint>& ExternalConstraints,
	                                  const FBALSettings& Settings = FBALSettings());

private:
	static void RunPipeline(UEdGraph* Graph,
	                        TArray<FBALConstraint> Constraints,
	                        const FBALSettings& Settings);
};
