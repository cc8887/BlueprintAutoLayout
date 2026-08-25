// BALAdvancedTopologyTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Regression coverage for graph shapes that cannot safely be represented as a
// simple execution tree: data-only chains, execution diamonds, and cycles.

#include "BALTestHelpers.h"
#include "BALGraphAnalyzer.h"
#include "BALLayoutSolver.h"
#include "BlueprintAutoLayoutEngine.h"
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
	TestTrue(TEXT("Data sink remains the component anchor"),
		SinkProxy.OutPos.Equals(SinkProxy.OriginalPos, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_PureDataBranchesUseSeparateLanes,
	"BlueprintAutoLayout.AdvancedRegression.Topology.PureDataBranchesUseSeparateLanes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_PureDataBranchesUseSeparateLanes::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* MainLeaf = MakeNode(Graph, false, 0, 1, -800, 0);
	UEdGraphNode* Main = MakeNode(Graph, false, 1, 1, -400, 0);
	UEdGraphNode* DetailLeaf = MakeNode(Graph, false, 0, 1, -800, 0);
	UEdGraphNode* Detail = MakeNode(Graph, false, 1, 1, -400, 0);
	UEdGraphNode* DirectLeaf = MakeNode(Graph, false, 0, 1, -800, 0);
	UEdGraphNode* Sink = MakeNode(Graph, false, 3, 0, 0, 0);
	WireData(MainLeaf, 0, Main, 0);
	WireData(Main, 0, Sink, 0);
	WireData(DetailLeaf, 0, Detail, 0);
	WireData(Detail, 0, Sink, 1);
	WireData(DirectLeaf, 0, Sink, 2);

	FBALSettings Settings;
	Settings.bPreserveAnchors = false;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);
	SolveAnalysis(Analysis, Settings, Constraints);

	const FBALNode& MainLeafProxy = Analysis.Proxies.FindChecked(MainLeaf);
	const FBALNode& MainProxy = Analysis.Proxies.FindChecked(Main);
	const FBALNode& DetailLeafProxy = Analysis.Proxies.FindChecked(DetailLeaf);
	const FBALNode& DetailProxy = Analysis.Proxies.FindChecked(Detail);
	const FBALNode& DirectProxy = Analysis.Proxies.FindChecked(DirectLeaf);
	const FBALNode& SinkProxy = Analysis.Proxies.FindChecked(Sink);

	TestEqual(TEXT("Direct sink input uses the adjacent layer"), DirectProxy.Layer, SinkProxy.Layer - 1);
	TestEqual(TEXT("Main branch terminal uses the adjacent layer"), MainProxy.Layer, SinkProxy.Layer - 1);
	TestEqual(TEXT("Detail branch terminal uses the adjacent layer"), DetailProxy.Layer, SinkProxy.Layer - 1);

	int32 SinkPrimaryRows = 0;
	for (const FBALEdge& Edge : Analysis.Edges)
	{
		if (Edge.Kind == EBALEdgeKind::Data && Edge.Target == Sink && Edge.bPrimary)
		{
			++SinkPrimaryRows;
		}
	}
	TestEqual(TEXT("Sink has exactly one same-row input"), SinkPrimaryRows, 1);

	const float MainTop = FMath::Min(MainLeafProxy.OutPos.Y, MainProxy.OutPos.Y);
	const float MainBottom = FMath::Max(
		MainLeafProxy.OutPos.Y + MainLeafProxy.Size.Y,
		MainProxy.OutPos.Y + MainProxy.Size.Y);
	const float DetailTop = FMath::Min(DetailLeafProxy.OutPos.Y, DetailProxy.OutPos.Y);
	const float DetailBottom = FMath::Max(
		DetailLeafProxy.OutPos.Y + DetailLeafProxy.Size.Y,
		DetailProxy.OutPos.Y + DetailProxy.Size.Y);
	TestTrue(TEXT("Secondary branch starts below the primary branch envelope"),
		DetailTop >= MainBottom + Settings.GapY - 0.01f);
	TestTrue(TEXT("Third branch starts below the secondary branch envelope"),
		DirectProxy.OutPos.Y >= DetailBottom + Settings.GapY - 0.01f);
	TestFalse(TEXT("Primary and secondary terminals do not overlap"),
		NodesOverlap(MainProxy, DetailProxy, Settings.NodeMargin));
	TestFalse(TEXT("Secondary and direct terminals do not overlap"),
		NodesOverlap(DetailProxy, DirectProxy, Settings.NodeMargin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_PureDataSharedNodePrefersNearestConsumer,
	"BlueprintAutoLayout.AdvancedRegression.Topology.PureDataSharedNodePrefersNearestConsumer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_PureDataSharedNodePrefersNearestConsumer::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Shared = MakeNode(Graph, false, 0, 1, -900, 0);
	UEdGraphNode* Mid = MakeNode(Graph, false, 1, 1, -600, 0);
	UEdGraphNode* Next = MakeNode(Graph, false, 1, 1, -300, 0);
	UEdGraphNode* Sink = MakeNode(Graph, false, 2, 0, 0, 0);
	WireData(Shared, 0, Mid, 0);
	WireData(Mid, 0, Next, 0);
	WireData(Next, 0, Sink, 0);
	WireData(Shared, 0, Sink, 1);

	FBALSettings Settings;
	Settings.bPreserveAnchors = false;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);
	SolveAnalysis(Analysis, Settings, Constraints);

	const FBALEdge* SharedToMid = Analysis.Edges.FindByPredicate(
		[Shared, Mid](const FBALEdge& Edge)
		{
			return Edge.Kind == EBALEdgeKind::Data
				&& Edge.Source == Shared && Edge.Target == Mid;
		});
	const FBALEdge* SharedToSink = Analysis.Edges.FindByPredicate(
		[Shared, Sink](const FBALEdge& Edge)
		{
			return Edge.Kind == EBALEdgeKind::Data
				&& Edge.Source == Shared && Edge.Target == Sink;
		});
	TestNotNull(TEXT("Shared-to-nearest edge exists"), SharedToMid);
	TestNotNull(TEXT("Shared-to-sink edge exists"), SharedToSink);
	if (SharedToMid && SharedToSink)
	{
		TestTrue(TEXT("Shared node belongs to its nearest downstream consumer"), SharedToMid->bPrimary);
		TestFalse(TEXT("Long edge to sink remains a non-tree edge"), SharedToSink->bPrimary);
	}
	TestTrue(TEXT("Nearest-consumer branch stays on the same row"),
		FMath::IsNearlyEqual(
			Analysis.Proxies.FindChecked(Shared).OutPos.Y,
			Analysis.Proxies.FindChecked(Mid).OutPos.Y,
			0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_PureDataSharedDagIdempotence,
	"BlueprintAutoLayout.AdvancedRegression.Topology.PureDataSharedDagIdempotence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_PureDataSharedDagIdempotence::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Shared = MakeNode(Graph, false, 0, 1, -600, 0);
	UEdGraphNode* LowerStable = MakeNode(Graph, false, 1, 1, -300, 0);
	UEdGraphNode* UpperByPin = MakeNode(Graph, false, 1, 1, -300, 0);
	UEdGraphNode* Sink = MakeNode(Graph, false, 2, 0, 0, 0);
	WireData(Shared, 0, LowerStable, 0);
	WireData(Shared, 0, UpperByPin, 0);
	WireData(UpperByPin, 0, Sink, 0);
	WireData(LowerStable, 0, Sink, 1);

	FBALSettings Settings;
	Settings.bPreserveAnchors = true;
	Settings.GridSnap = 8.f;
	FBlueprintAutoLayoutEngine::Layout(Graph, Settings);

	TMap<UEdGraphNode*, FIntPoint> FirstPositions;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			FirstPositions.Add(Node, FIntPoint(Node->NodePosX, Node->NodePosY));
		}
	}

	FBlueprintAutoLayoutEngine::Layout(Graph, Settings);
	bool bAllPositionsStable = true;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		const FIntPoint* First = FirstPositions.Find(Node);
		if (Node && First
			&& (Node->NodePosX != First->X || Node->NodePosY != First->Y))
		{
			bAllPositionsStable = false;
			AddError(FString::Printf(
				TEXT("Repeated shared-DAG layout moved node %d from (%d,%d) to (%d,%d)"),
				Graph->Nodes.IndexOfByKey(Node), First->X, First->Y,
				Node->NodePosX, Node->NodePosY));
		}
	}
	TestTrue(TEXT("Shared data DAG is exactly position-idempotent"), bAllPositionsStable);

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
