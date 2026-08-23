// BALDeepRegressionTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Focused regressions for margin-aware collision discovery, deterministic data
// cycles, comment containment, and shared-pure ownership tie breaking.

#include "BALTestHelpers.h"
#include "BALCollisionResolver.h"
#include "BALGraphAnalyzer.h"
#include "BALLayoutSolver.h"
#include "Misc/AutomationTest.h"

namespace
{
	void ResolveDeepRegressionProxies(
		TMap<UEdGraphNode*, FBALNode>& Proxies,
		const FBALSettings& Settings)
	{
		TArray<FBALConstraint> Constraints;
		FBALCollisionResolver::FResolveInput Input;
		Input.Proxies = &Proxies;
		Input.Constraints = &Constraints;
		Input.Settings = &Settings;
		FBALCollisionResolver::Resolve(Input);
	}

	void SolveDeepRegressionAnalysis(
		FBALGraphAnalyzer::FAnalysisResult& Analysis,
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

	struct FDataCycleFixture
	{
		UEdGraph* Graph;
		TArray<UEdGraphNode*> Nodes;

		FDataCycleFixture()
			: Graph(nullptr)
		{
		}
	};

	FDataCycleFixture MakeDataCycleFixture(bool bReverseInsertion)
	{
		using namespace BALTest;

		FDataCycleFixture Fixture;
		Fixture.Graph = MakeGraph();
		UEdGraphNode* A = MakeNode(Fixture.Graph, false, 1, 1, 100, 200);
		UEdGraphNode* B = MakeNode(Fixture.Graph, false, 1, 1, 400, 0);
		UEdGraphNode* C = MakeNode(Fixture.Graph, false, 1, 1, 400, 400);
		UEdGraphNode* D = MakeNode(Fixture.Graph, false, 2, 1, 700, 200);
		Fixture.Nodes.Add(A);
		Fixture.Nodes.Add(B);
		Fixture.Nodes.Add(C);
		Fixture.Nodes.Add(D);

		if (bReverseInsertion)
		{
			WireData(A, 0, C, 0);
			WireData(A, 0, B, 0);
			WireData(C, 0, D, 1);
			WireData(B, 0, D, 0);
		}
		else
		{
			WireData(A, 0, B, 0);
			WireData(A, 0, C, 0);
			WireData(B, 0, D, 0);
			WireData(C, 0, D, 1);
		}
		WireData(D, 0, A, 0);
		return Fixture;
	}

	const FBALEdge* FindDataEdge(
		const FBALGraphAnalyzer::FAnalysisResult& Analysis,
		UEdGraphNode* Source,
		UEdGraphNode* Target)
	{
		return Analysis.Edges.FindByPredicate(
			[Source, Target](const FBALEdge& Edge)
			{
				return Edge.Kind == EBALEdgeKind::Data
					&& Edge.Source == Source
					&& Edge.Target == Target;
			});
	}

	const FBALCommentGroup* FindDeepRegressionCommentGroup(
		const TArray<FBALCommentGroup>& Groups,
		const UEdGraphNode* Comment)
	{
		return Groups.FindByPredicate(
			[Comment](const FBALCommentGroup& Group)
			{
				return Group.Comment == Comment;
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Deep_LargeMarginNeighborDiscovery,
	"BlueprintAutoLayout.DeepRegression.Collision.LargeMarginNeighborDiscovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Deep_LargeMarginNeighborDiscovery::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* LeftNode = MakeNode(Graph, false, 0, 0);
	UEdGraphNode* RightNode = MakeNode(Graph, false, 0, 0);
	TMap<UEdGraphNode*, FBALNode> Proxies;
	FBALNode LeftProxy = MakeProxy(LeftNode, EBALNodeRole::Exec,
		FVector2D(100.f, 100.f), FVector2D(0.f, 0.f));
	FBALNode RightProxy = MakeProxy(RightNode, EBALNodeRole::Exec,
		FVector2D(100.f, 100.f), FVector2D(350.f, 0.f));
	LeftProxy.StableIndex = 0;
	RightProxy.StableIndex = 1;
	Proxies.Add(LeftNode, LeftProxy);
	Proxies.Add(RightNode, RightProxy);

	FBALSettings Settings;
	Settings.NodeMargin = 200.f;
	Settings.CollisionIter = 12;
	const float InitialRawGap = RightProxy.OutPos.X
		- (LeftProxy.OutPos.X + LeftProxy.Size.X);
	TestTrue(TEXT("Raw node boxes begin about 250 pixels apart"),
		FMath::IsNearlyEqual(InitialRawGap, 250.f, 0.01f));
	TestFalse(TEXT("Raw node boxes do not initially overlap"),
		NodesOverlap(LeftProxy, RightProxy, 0.f));
	TestTrue(TEXT("Large margins make the initially distant pair collide"),
		NodesOverlap(LeftProxy, RightProxy, Settings.NodeMargin));

	ResolveDeepRegressionProxies(Proxies, Settings);
	const FBALNode& ResolvedLeft = Proxies.FindChecked(LeftNode);
	const FBALNode& ResolvedRight = Proxies.FindChecked(RightNode);
	TestFalse(TEXT("Resolver discovers and separates the large-margin pair"),
		NodesOverlap(ResolvedLeft, ResolvedRight, Settings.NodeMargin));
	TestTrue(TEXT("At least one node moved despite the raw AABB gap"),
		!ResolvedLeft.OutPos.Equals(LeftProxy.OriginalPos, 0.01f)
		|| !ResolvedRight.OutPos.Equals(RightProxy.OriginalPos, 0.01f));
	const float FinalRawGap = ResolvedRight.OutPos.X
		- (ResolvedLeft.OutPos.X + ResolvedLeft.Size.X);
	TestTrue(TEXT("Final raw gap includes both node margins"),
		FinalRawGap + 0.01f >= Settings.NodeMargin * 2.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Deep_DataCycleInsertionOrderDeterminism,
	"BlueprintAutoLayout.DeepRegression.DataCycle.InsertionOrderDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Deep_DataCycleInsertionOrderDeterminism::RunTest(const FString& /*Params*/)
{
	FDataCycleFixture Forward = MakeDataCycleFixture(false);
	FDataCycleFixture Reverse = MakeDataCycleFixture(true);
	FBALSettings Settings;
	Settings.bPreserveAnchors = true;
	Settings.NodeMargin = 8.f;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult ForwardAnalysis =
		FBALGraphAnalyzer::Analyze(Forward.Graph, Settings, Constraints);
	FBALGraphAnalyzer::FAnalysisResult ReverseAnalysis =
		FBALGraphAnalyzer::Analyze(Reverse.Graph, Settings, Constraints);

	TestEqual(TEXT("Forward cycle edge count"), ForwardAnalysis.Edges.Num(), 5);
	TestEqual(TEXT("Reverse cycle edge count"), ReverseAnalysis.Edges.Num(), 5);
	if (ForwardAnalysis.Edges.Num() == 5 && ReverseAnalysis.Edges.Num() == 5)
	{
		TestEqual(TEXT("Forward fixture inserts A-to-B before A-to-C"),
			ForwardAnalysis.Edges[0].Target, Forward.Nodes[1]);
		TestEqual(TEXT("Reverse fixture inserts A-to-C before A-to-B"),
			ReverseAnalysis.Edges[0].Target, Reverse.Nodes[2]);
	}

	TestEqual(TEXT("Forward fixture has one component"),
		ForwardAnalysis.Components.Num(), 1);
	TestEqual(TEXT("Reverse fixture has one component"),
		ReverseAnalysis.Components.Num(), 1);
	for (int32 Index = 0; Index < Forward.Nodes.Num(); ++Index)
	{
		const FBALNode& ForwardProxy = ForwardAnalysis.Proxies.FindChecked(Forward.Nodes[Index]);
		const FBALNode& ReverseProxy = ReverseAnalysis.Proxies.FindChecked(Reverse.Nodes[Index]);
		TestEqual(TEXT("Equivalent node role is deterministic"),
			ForwardProxy.Role, ReverseProxy.Role);
		TestEqual(TEXT("Equivalent stable index is deterministic"),
			ForwardProxy.StableIndex, ReverseProxy.StableIndex);
		TestEqual(TEXT("Equivalent component id is deterministic"),
			ForwardProxy.ComponentId, ReverseProxy.ComponentId);
	}

	SolveDeepRegressionAnalysis(ForwardAnalysis, Settings, Constraints);
	SolveDeepRegressionAnalysis(ReverseAnalysis, Settings, Constraints);
	const int32 ExpectedLayers[] = { 0, 1, 1, 2 };
	for (int32 Index = 0; Index < Forward.Nodes.Num(); ++Index)
	{
		const FBALNode& ForwardProxy = ForwardAnalysis.Proxies.FindChecked(Forward.Nodes[Index]);
		const FBALNode& ReverseProxy = ReverseAnalysis.Proxies.FindChecked(Reverse.Nodes[Index]);
		TestEqual(TEXT("Cycle node receives expected deterministic layer"),
			ForwardProxy.Layer, ExpectedLayers[Index]);
		TestEqual(TEXT("Cycle layer is insertion-order independent"),
			ForwardProxy.Layer, ReverseProxy.Layer);
		TestEqual(TEXT("Cycle layer order is insertion-order independent"),
			ForwardProxy.LayerOrder, ReverseProxy.LayerOrder);
		TestTrue(TEXT("Cycle position is insertion-order independent"),
			ForwardProxy.OutPos.Equals(ReverseProxy.OutPos, 0.01f));
	}

	const int32 EdgeSources[] = { 0, 0, 1, 2, 3 };
	const int32 EdgeTargets[] = { 1, 2, 3, 3, 0 };
	int32 ForwardBackEdges = 0;
	int32 ReverseBackEdges = 0;
	for (int32 EdgeIndex = 0; EdgeIndex < 5; ++EdgeIndex)
	{
		const int32 SourceIndex = EdgeSources[EdgeIndex];
		const int32 TargetIndex = EdgeTargets[EdgeIndex];
		const FBALEdge* ForwardEdge = FindDataEdge(
			ForwardAnalysis, Forward.Nodes[SourceIndex], Forward.Nodes[TargetIndex]);
		const FBALEdge* ReverseEdge = FindDataEdge(
			ReverseAnalysis, Reverse.Nodes[SourceIndex], Reverse.Nodes[TargetIndex]);
		TestNotNull(TEXT("Forward semantic edge exists"), ForwardEdge);
		TestNotNull(TEXT("Reverse semantic edge exists"), ReverseEdge);
		if (ForwardEdge && ReverseEdge)
		{
			const bool bExpectedBackEdge = SourceIndex == 3 && TargetIndex == 0;
			TestEqual(TEXT("Stable traversal selects D-to-A as the back edge"),
				ForwardEdge->bBackEdge, bExpectedBackEdge);
			TestEqual(TEXT("Back-edge classification is deterministic"),
				ForwardEdge->bBackEdge, ReverseEdge->bBackEdge);
			TestEqual(TEXT("Source pin index is deterministic"),
				ForwardEdge->SourcePinIndex, ReverseEdge->SourcePinIndex);
			TestEqual(TEXT("Target pin index is deterministic"),
				ForwardEdge->TargetPinIndex, ReverseEdge->TargetPinIndex);
			if (ForwardEdge->bBackEdge) ++ForwardBackEdges;
			if (ReverseEdge->bBackEdge) ++ReverseBackEdges;
		}
	}
	TestEqual(TEXT("Forward cycle has one back edge"), ForwardBackEdges, 1);
	TestEqual(TEXT("Reverse cycle has one back edge"), ReverseBackEdges, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Deep_PartialCommentIntersectionNoHierarchy,
	"BlueprintAutoLayout.DeepRegression.Comments.PartialIntersectionNoHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Deep_PartialCommentIntersectionNoHierarchy::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode_Comment* Larger = MakeComment(Graph, 0, 0, 500, 400);
	UEdGraphNode_Comment* Smaller = MakeComment(Graph, 300, 200, 300, 300);
	FBALSettings Settings;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);

	const FBALCommentGroup* LargerGroup = FindDeepRegressionCommentGroup(
		Analysis.CommentGroups, Larger);
	const FBALCommentGroup* SmallerGroup = FindDeepRegressionCommentGroup(
		Analysis.CommentGroups, Smaller);
	TestNotNull(TEXT("Larger comment group exists"), LargerGroup);
	TestNotNull(TEXT("Smaller comment group exists"), SmallerGroup);
	TestNull(TEXT("Partially intersecting larger comment has no parent"),
		Analysis.Proxies.FindChecked(Larger).DirectComment);
	TestNull(TEXT("Partially intersecting smaller comment has no parent"),
		Analysis.Proxies.FindChecked(Smaller).DirectComment);
	if (LargerGroup && SmallerGroup)
	{
		TestNull(TEXT("Larger group does not parent under smaller"),
			LargerGroup->ParentComment);
		TestNull(TEXT("Smaller group is not parented by partial intersection"),
			SmallerGroup->ParentComment);
		TestEqual(TEXT("Larger comment depth remains zero"), LargerGroup->Depth, 0);
		TestEqual(TEXT("Smaller comment depth remains zero"), SmallerGroup->Depth, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Deep_EqualAreaCommentStableOwnership,
	"BlueprintAutoLayout.DeepRegression.Comments.EqualAreaStableOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Deep_EqualAreaCommentStableOwnership::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode_Comment* Earlier = MakeComment(Graph, 0, 0, 400, 300);
	UEdGraphNode_Comment* Later = MakeComment(Graph, 0, 0, 400, 300);
	UEdGraphNode* Member = MakeNode(Graph, false, 0, 0, 100, 100);
	Member->NodeWidth = 100;
	Member->NodeHeight = 60;
	FBALSettings Settings;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);

	const FBALNode& EarlierProxy = Analysis.Proxies.FindChecked(Earlier);
	const FBALNode& LaterProxy = Analysis.Proxies.FindChecked(Later);
	const FBALNode& MemberProxy = Analysis.Proxies.FindChecked(Member);
	TestTrue(TEXT("Earlier comment has the lower StableIndex"),
		EarlierProxy.StableIndex < LaterProxy.StableIndex);
	TestEqual(TEXT("Equal-area ownership selects lower StableIndex comment"),
		MemberProxy.DirectComment, static_cast<UEdGraphNode*>(Earlier));

	const FBALCommentGroup* EarlierGroup = FindDeepRegressionCommentGroup(
		Analysis.CommentGroups, Earlier);
	const FBALCommentGroup* LaterGroup = FindDeepRegressionCommentGroup(
		Analysis.CommentGroups, Later);
	TestNotNull(TEXT("Earlier equal-area group exists"), EarlierGroup);
	TestNotNull(TEXT("Later equal-area group exists"), LaterGroup);
	if (EarlierGroup && LaterGroup)
	{
		TestTrue(TEXT("Lower StableIndex comment directly owns member"),
			EarlierGroup->DirectMembers.Contains(Member));
		TestFalse(TEXT("Higher StableIndex comment does not own member"),
			LaterGroup->DirectMembers.Contains(Member));
		TestNull(TEXT("Equal-area comments do not parent each other"),
			EarlierGroup->ParentComment);
		TestNull(TEXT("Equal-area comments remain peers"),
			LaterGroup->ParentComment);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Deep_SharedPurePrefersShallowExecDepth,
	"BlueprintAutoLayout.DeepRegression.Pure.SharedHopPrefersShallowExecDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Deep_SharedPurePrefersShallowExecDepth::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* ShallowExec = MakeNode(Graph, true, 1, 0, 0, 0);
	UEdGraphNode* DeepExec = MakeNode(Graph, true, 1, 0, 1000, 0);
	UEdGraphNode* SharedPure = MakeNode(Graph, false, 0, 2, 990, 0);
	WireExecPins(ShallowExec, 0, DeepExec);
	WireData(SharedPure, 0, ShallowExec, 0);
	WireData(SharedPure, 1, DeepExec, 0);

	FBALSettings Settings;
	Settings.NodeMargin = 0.f;
	Settings.bPreserveAnchors = true;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);
	const FBALNode& ShallowProxy = Analysis.Proxies.FindChecked(ShallowExec);
	const FBALNode& DeepProxy = Analysis.Proxies.FindChecked(DeepExec);
	const FBALNode& PureProxy = Analysis.Proxies.FindChecked(SharedPure);

	TestEqual(TEXT("Shallow consumer exec depth"), ShallowProxy.ExecDepth, 0);
	TestEqual(TEXT("Deep consumer exec depth"), DeepProxy.ExecDepth, 1);
	const float DistanceToShallow = (PureProxy.OriginalPos - ShallowProxy.OriginalPos).Size();
	const float DistanceToDeep = (PureProxy.OriginalPos - DeepProxy.OriginalPos).Size();
	TestTrue(TEXT("Fixture places shared pure much closer to deep consumer"),
		DistanceToDeep < DistanceToShallow);
	TestEqual(TEXT("Equal-hop shared pure chooses shallower exec depth"),
		PureProxy.PureOwner, ShallowExec);

	SolveDeepRegressionAnalysis(Analysis, Settings, Constraints);
	const FBALNode& SolvedShallow = Analysis.Proxies.FindChecked(ShallowExec);
	const FBALNode& SolvedPure = Analysis.Proxies.FindChecked(SharedPure);
	TestTrue(TEXT("Owned shared pure is placed west of shallow consumer"),
		SolvedPure.OutPos.X + SolvedPure.Size.X < SolvedShallow.OutPos.X);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Deep_LongDataChainUsesExplicitStack,
	"BlueprintAutoLayout.DeepRegression.Scale.LongDataChainUsesExplicitStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Deep_LongDataChainUsesExplicitStack::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	const int32 NodeCount = 4096;
	UEdGraph* Graph = MakeGraph();
	TArray<UEdGraphNode*> Nodes;
	Nodes.Reserve(NodeCount);
	for (int32 Index = 0; Index < NodeCount; ++Index)
	{
		Nodes.Add(MakeNode(
			Graph,
			false,
			Index == 0 ? 0 : 1,
			Index + 1 == NodeCount ? 0 : 1,
			Index * 4,
			0));
		if (Index > 0)
		{
			WireData(Nodes[Index - 1], 0, Nodes[Index], 0);
		}
	}

	FBALSettings Settings;
	Settings.bPreserveAnchors = false;
	Settings.CrossingReductionPasses = 0;
	Settings.AlignmentPasses = 0;
	Settings.NodeMargin = 0.f;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);
	SolveDeepRegressionAnalysis(Analysis, Settings, Constraints);

	TestEqual(TEXT("Long chain remains one component"), Analysis.Components.Num(), 1);
	TestEqual(TEXT("Long chain exposes every logical edge"), Analysis.Edges.Num(), NodeCount - 1);
	TestEqual(TEXT("Long chain starts at layer zero"),
		Analysis.Proxies.FindChecked(Nodes[0]).Layer, 0);
	TestEqual(TEXT("Long chain reaches its full iterative depth"),
		Analysis.Proxies.FindChecked(Nodes.Last()).Layer, NodeCount - 1);
	int32 BackEdgeCount = 0;
	for (const FBALEdge& Edge : Analysis.Edges)
	{
		if (Edge.bBackEdge) ++BackEdgeCount;
	}
	TestEqual(TEXT("Acyclic long chain has no back edge"), BackEdgeCount, 0);
	return true;
}
