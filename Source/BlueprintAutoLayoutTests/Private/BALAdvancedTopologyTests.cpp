// BALAdvancedTopologyTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Regression coverage for graph shapes that cannot safely be represented as a
// simple execution tree: data-only chains, execution diamonds, and cycles.

#include "BALTestHelpers.h"
#include "BALGraphAnalyzer.h"
#include "BALLayoutSolver.h"
#include "Misc/AutomationTest.h"

namespace
{
	void SolveAnalysis(FBALGraphAnalyzer::FAnalysisResult& Analysis,
	                   const FBALSettings& Settings,
	                   const TArray<FBALConstraint>& Constraints)
	{
		FBALLayoutSolver::FSolverInput Input;
		Input.Proxies = &Analysis.Proxies;
		Input.ExecRoots = &Analysis.ExecRoots;
		Input.Edges = &Analysis.Edges;
		Input.Components = &Analysis.Components;
		Input.IsolatedPures = &Analysis.IsolatedPures;
		Input.IsolatedNodes = &Analysis.IsolatedNodes;
		Input.Constraints = &Constraints;
		Input.PureDir = EBALPureDir::West;
		Input.Settings = &Settings;
		FBALLayoutSolver::Solve(Input);
	}

	FBALExecNode* FindExecNode(
		FBALGraphAnalyzer::FAnalysisResult& Analysis,
		UEdGraphNode* GraphNode)
	{
		for (TUniquePtr<FBALExecNode>& Candidate : Analysis.ExecNodePool)
		{
			if (Candidate.IsValid() && Candidate->Proxy
				&& Candidate->Proxy->GraphNode == GraphNode)
			{
				return Candidate.Get();
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_PureDataChainLayering,
	"BlueprintAutoLayout.AdvancedRegression.Topology.PureDataChainLayering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_PureDataChainLayering::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Source = MakeNode(Graph, false, 0, 1, 512, 128);
	UEdGraphNode* Transform = MakeNode(Graph, false, 1, 1, -400, 700);
	UEdGraphNode* Sink = MakeNode(Graph, false, 1, 0, 32, -300);
	WireData(Source, 0, Transform, 0);
	WireData(Transform, 0, Sink, 0);

	FBALSettings Settings;
	Settings.bPreserveAnchors = true;
	Settings.NodeMargin = 8.f;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);

	TestEqual(TEXT("Data chain has one connected component"), Analysis.Components.Num(), 1);
	TestEqual(TEXT("Data chain exposes two logical edges"), Analysis.Edges.Num(), 2);
	for (const FBALEdge& Edge : Analysis.Edges)
	{
		TestEqual(TEXT("Logical edge is data"), Edge.Kind, EBALEdgeKind::Data);
		TestFalse(TEXT("Acyclic data edge is not a back edge"), Edge.bBackEdge);
	}

	SolveAnalysis(Analysis, Settings, Constraints);
	const FBALNode& SourceProxy = Analysis.Proxies.FindChecked(Source);
	const FBALNode& TransformProxy = Analysis.Proxies.FindChecked(Transform);
	const FBALNode& SinkProxy = Analysis.Proxies.FindChecked(Sink);

	TestEqual(TEXT("Source is in layer zero"), SourceProxy.Layer, 0);
	TestEqual(TEXT("Transform is in layer one"), TransformProxy.Layer, 1);
	TestEqual(TEXT("Sink is in layer two"), SinkProxy.Layer, 2);
	TestTrue(TEXT("Source is placed before transform"),
		SourceProxy.OutPos.X + SourceProxy.Size.X < TransformProxy.OutPos.X);
	TestTrue(TEXT("Transform is placed before sink"),
		TransformProxy.OutPos.X + TransformProxy.Size.X < SinkProxy.OutPos.X);
	TestTrue(TEXT("Component anchor remains at its original position"),
		SourceProxy.OutPos.Equals(SourceProxy.OriginalPos, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_ExecDiamondConvergence,
	"BlueprintAutoLayout.AdvancedRegression.Topology.ExecDiamondConvergence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_ExecDiamondConvergence::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Root = MakeNode(Graph, true, 0, 0, 320, 160);
	UEdGraphNode* Upper = MakeNode(Graph, true, 0, 0, 0, 0);
	UEdGraphNode* Lower = MakeNode(Graph, true, 0, 0, 0, 300);
	UEdGraphNode* Merge = MakeNode(Graph, true, 0, 0, 900, 100);
	AddExecOutput(Root, FName(TEXT("ExecOutLower")));

	WireExecPins(Root, 0, Upper);
	WireExecPins(Root, 1, Lower);
	WireExecPins(Upper, 0, Merge);
	WireExecPins(Lower, 0, Merge);

	FBALSettings Settings;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);

	int32 MergeIncomingCount = 0;
	int32 MergePrimaryCount = 0;
	for (const FBALEdge& Edge : Analysis.Edges)
	{
		if (Edge.Kind == EBALEdgeKind::Exec && Edge.Target == Merge)
		{
			++MergeIncomingCount;
			if (Edge.bPrimary)
			{
				++MergePrimaryCount;
			}
		}
	}
	TestEqual(TEXT("Both diamond parents remain logical inputs"), MergeIncomingCount, 2);
	TestEqual(TEXT("Exactly one diamond edge is the tree parent"), MergePrimaryCount, 1);
	TestEqual(TEXT("Diamond has a single execution root"), Analysis.ExecRoots.Num(), 1);
	if (Analysis.ExecRoots.Num() == 1)
	{
		TestEqual(TEXT("Diamond root is preserved"),
			Analysis.ExecRoots[0]->Proxy->GraphNode, Root);
	}

	FBALExecNode* MergeExec = FindExecNode(Analysis, Merge);
	TestNotNull(TEXT("Merge has an execution proxy"), MergeExec);
	int32 ParentReferences = 0;
	for (TUniquePtr<FBALExecNode>& ExecNode : Analysis.ExecNodePool)
	{
		if (ExecNode.IsValid() && ExecNode->Children.Contains(MergeExec))
		{
			++ParentReferences;
		}
	}
	TestEqual(TEXT("Merge appears under only one primary parent"), ParentReferences, 1);
	if (MergeExec)
	{
		TestNotNull(TEXT("Merge records its primary parent"), MergeExec->Parent);
	}

	TestEqual(TEXT("Root layer"), Analysis.Proxies.FindChecked(Root).Layer, 0);
	TestEqual(TEXT("Upper branch layer"), Analysis.Proxies.FindChecked(Upper).Layer, 1);
	TestEqual(TEXT("Lower branch layer"), Analysis.Proxies.FindChecked(Lower).Layer, 1);
	TestEqual(TEXT("Merge layer follows both parents"), Analysis.Proxies.FindChecked(Merge).Layer, 2);

	SolveAnalysis(Analysis, Settings, Constraints);
	const FBALNode& RootProxy = Analysis.Proxies.FindChecked(Root);
	const FBALNode& UpperProxy = Analysis.Proxies.FindChecked(Upper);
	const FBALNode& LowerProxy = Analysis.Proxies.FindChecked(Lower);
	const FBALNode& MergeProxy = Analysis.Proxies.FindChecked(Merge);
	TestTrue(TEXT("Both branches are right of root"),
		UpperProxy.OutPos.X > RootProxy.OutPos.X && LowerProxy.OutPos.X > RootProxy.OutPos.X);
	TestTrue(TEXT("Merge is right of both branches"),
		MergeProxy.OutPos.X > UpperProxy.OutPos.X && MergeProxy.OutPos.X > LowerProxy.OutPos.X);
	TestFalse(TEXT("Parallel diamond branches do not overlap"),
		NodesOverlap(UpperProxy, LowerProxy, Settings.NodeMargin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_ExecCycleBackEdge,
	"BlueprintAutoLayout.AdvancedRegression.Topology.ExecCycleBackEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_ExecCycleBackEdge::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* A = MakeNode(Graph, true, 0, 0, 100, 100);
	UEdGraphNode* B = MakeNode(Graph, true, 0, 0, 400, 100);
	UEdGraphNode* C = MakeNode(Graph, true, 0, 0, 700, 100);
	WireExecPins(A, 0, B);
	WireExecPins(B, 0, C);
	WireExecPins(C, 0, A);

	FBALSettings Settings;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);

	int32 BackEdgeCount = 0;
	int32 PrimaryEdgeCount = 0;
	for (const FBALEdge& Edge : Analysis.Edges)
	{
		if (Edge.Kind != EBALEdgeKind::Exec)
		{
			continue;
		}
		if (Edge.bBackEdge)
		{
			++BackEdgeCount;
			TestFalse(TEXT("Back edge cannot be a primary tree edge"), Edge.bPrimary);
			TestTrue(TEXT("Back edge returns to an earlier layer"),
				Analysis.Proxies.FindChecked(Edge.Target).Layer
				<= Analysis.Proxies.FindChecked(Edge.Source).Layer);
		}
		if (Edge.bPrimary)
		{
			++PrimaryEdgeCount;
		}
	}
	TestEqual(TEXT("Three-node cycle has one DFS back edge"), BackEdgeCount, 1);
	TestEqual(TEXT("Remaining two edges form the primary tree"), PrimaryEdgeCount, 2);
	TestEqual(TEXT("Cycle is reduced to one layout root"), Analysis.ExecRoots.Num(), 1);

	// Reaching and completing Solve is the regression guard against recursive
	// execution-tree traversal on the original cyclic edge set.
	SolveAnalysis(Analysis, Settings, Constraints);
	const UEdGraphNode* Nodes[] = { A, B, C };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FBALNode& Proxy = Analysis.Proxies.FindChecked(
			const_cast<UEdGraphNode*>(Nodes[Index]));
		TestTrue(TEXT("Cycle layout X is finite"), FMath::IsFinite(Proxy.OutPos.X));
		TestTrue(TEXT("Cycle layout Y is finite"), FMath::IsFinite(Proxy.OutPos.Y));
	}

	return true;
}
