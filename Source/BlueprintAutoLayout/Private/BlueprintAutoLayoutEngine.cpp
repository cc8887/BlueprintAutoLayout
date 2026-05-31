// BlueprintAutoLayoutEngine.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BlueprintAutoLayoutEngine.h"
#include "BALConstraintCollector.h"
#include "BALGraphAnalyzer.h"
#include "BALStyleVoter.h"
#include "BALLayoutSolver.h"
#include "BALCollisionResolver.h"
#include "BALCommitter.h"
#include "EdGraph/EdGraph.h"

// ─────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────

void FBlueprintAutoLayoutEngine::Layout(UEdGraph* Graph,
                                         const FBALSettings& Settings)
{
	if (!Graph) return;
	TArray<FBALConstraint> Constraints;
	FBALConstraintCollector::Collect(Graph, Constraints);
	RunPipeline(Graph, MoveTemp(Constraints), Settings);
}

void FBlueprintAutoLayoutEngine::LayoutSelection(UEdGraph* Graph,
                                                  const TSet<UEdGraphNode*>& Selection,
                                                  const FBALSettings& Settings)
{
	if (!Graph) return;
	TArray<FBALConstraint> Constraints;
	FBALConstraintCollector::CollectForSelection(Graph, Selection, Constraints);
	RunPipeline(Graph, MoveTemp(Constraints), Settings);
}

void FBlueprintAutoLayoutEngine::LayoutWithConstraints(UEdGraph* Graph,
                                                        const TArray<FBALConstraint>& ExternalConstraints,
                                                        const FBALSettings& Settings)
{
	if (!Graph) return;
	RunPipeline(Graph, ExternalConstraints, Settings);
}

// ─────────────────────────────────────────────────────────────
//  Private: pipeline
// ─────────────────────────────────────────────────────────────

void FBlueprintAutoLayoutEngine::RunPipeline(UEdGraph* Graph,
                                              TArray<FBALConstraint> Constraints,
                                              const FBALSettings& Settings)
{
	// ── Stage 1: Analyze ─────────────────────────────────────
	FBALGraphAnalyzer::FAnalysisResult Analysis =
	    FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);

	if (Analysis.Proxies.IsEmpty()) return;

	// ── Stage 2: Vote for Pure direction ──────────────────────
	FBALStyleVoter::FVoteContext VoteCtx;
	VoteCtx.Proxies    = &Analysis.Proxies;
	VoteCtx.ExecRoots  = &Analysis.ExecRoots;
	VoteCtx.Settings   = &Settings;

	EBALPureDir PureDir = FBALStyleVoter::Vote(VoteCtx);

	// ── Stage 3: Layout ───────────────────────────────────────
	FBALLayoutSolver::FSolverInput SolverIn;
	SolverIn.Proxies        = &Analysis.Proxies;
	SolverIn.ExecRoots      = &Analysis.ExecRoots;
	SolverIn.IsolatedPures  = &Analysis.IsolatedPures;
	SolverIn.IsolatedNodes  = &Analysis.IsolatedNodes;
	SolverIn.Constraints    = &Constraints;
	SolverIn.PureDir        = PureDir;
	SolverIn.Settings       = &Settings;

	FBALLayoutSolver::Solve(SolverIn);

	// ── Stage 4: Collision resolution ─────────────────────────
	FBALCollisionResolver::FResolveInput ResolveIn;
	ResolveIn.Proxies     = &Analysis.Proxies;
	ResolveIn.Constraints = &Constraints;
	ResolveIn.Settings    = &Settings;

	FBALCollisionResolver::Resolve(ResolveIn);

	// ── Stage 5: Commit ───────────────────────────────────────
	int32 Written = FBALCommitter::Commit(Graph, Analysis.Proxies, Constraints, Settings);

	UE_LOG(LogTemp, Log, TEXT("BlueprintAutoLayout: Arranged %d nodes in '%s'"),
	       Written, *Graph->GetName());
}
