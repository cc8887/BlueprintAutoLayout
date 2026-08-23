// BALFinalSolverRegressionTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALTestHelpers.h"
#include "BALLayoutSolver.h"
#include "Misc/AutomationTest.h"

namespace
{
	FBALEdge MakeFinalSolverDataEdge(
		UEdGraphNode* Source,
		UEdGraphNode* Target,
		int32 TargetPinIndex = 0)
	{
		FBALEdge Edge;
		Edge.Source = Source;
		Edge.Target = Target;
		Edge.Kind = EBALEdgeKind::Data;
		Edge.TargetPinIndex = TargetPinIndex;
		return Edge;
	}

	void SolveFinalSolverRegression(
		TMap<UEdGraphNode*, FBALNode>& Proxies,
		TArray<FBALEdge>& Edges,
		TArray<FBALComponent>& Components,
		const FBALSettings& Settings,
		EBALPureDir PureDir = EBALPureDir::West)
	{
		TArray<FBALExecNode*> ExecRoots;
		TArray<FBALNode*> IsolatedPures;
		TArray<FBALNode*> IsolatedNodes;
		TArray<FBALConstraint> Constraints;
		FBALLayoutSolver::FSolverInput Input;
		Input.Proxies = &Proxies;
		Input.ExecRoots = &ExecRoots;
		Input.Edges = &Edges;
		Input.Components = &Components;
		Input.IsolatedPures = &IsolatedPures;
		Input.IsolatedNodes = &IsolatedNodes;
		Input.Constraints = &Constraints;
		Input.PureDir = PureDir;
		Input.Settings = &Settings;
		FBALLayoutSolver::Solve(Input);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_FinalSolver_UnownedMergeLongestPath,
	"BlueprintAutoLayout.FinalSolver.UnownedData.MergeLongestPathWithReachableCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_FinalSolver_UnownedMergeLongestPath::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Producer = MakeNode(Graph, false, 0, 0);
	UEdGraphNode* A = MakeNode(Graph, false, 0, 0);
	UEdGraphNode* Merge = MakeNode(Graph, false, 0, 0);
	UEdGraphNode* B = MakeNode(Graph, false, 0, 0);

	TMap<UEdGraphNode*, FBALNode> Proxies;
	FBALNode ProducerProxy = MakeProxy(
		Producer, EBALNodeRole::Exec, FVector2D(100.f, 60.f));
	FBALNode AProxy = MakeProxy(
		A, EBALNodeRole::Pure, FVector2D(100.f, 60.f));
	FBALNode MergeProxy = MakeProxy(
		Merge, EBALNodeRole::Pure, FVector2D(100.f, 60.f));
	FBALNode BProxy = MakeProxy(
		B, EBALNodeRole::Pure, FVector2D(100.f, 60.f));
	ProducerProxy.StableIndex = 0;
	// The non-seed cycle node deliberately sorts before the external entry.
	BProxy.StableIndex = 1;
	MergeProxy.StableIndex = 2;
	AProxy.StableIndex = 3;
	Proxies.Add(Producer, ProducerProxy);
	Proxies.Add(A, AProxy);
	Proxies.Add(Merge, MergeProxy);
	Proxies.Add(B, BProxy);

	TArray<FBALEdge> Edges;
	Edges.Add(MakeFinalSolverDataEdge(Producer, A));
	// Insert the shortcut before the deep branch to reproduce first-discovery BFS.
	Edges.Add(MakeFinalSolverDataEdge(A, Merge));
	Edges.Add(MakeFinalSolverDataEdge(A, B));
	Edges.Add(MakeFinalSolverDataEdge(B, A));
	Edges.Add(MakeFinalSolverDataEdge(B, Merge));

	FBALComponent Component;
	Component.Id = 0;
	Component.Anchor = Producer;
	Component.Nodes = { Producer, A, Merge, B };
	TArray<FBALComponent> Components;
	Components.Add(Component);

	FBALSettings Settings;
	Settings.bPreserveAnchors = false;
	Settings.NodeMargin = 0.f;
	Settings.CrossingReductionPasses = 0;
	Settings.AlignmentPasses = 0;
	SolveFinalSolverRegression(Proxies, Edges, Components, Settings);

	const FBALNode& SolvedA = Proxies.FindChecked(A);
	const FBALNode& SolvedB = Proxies.FindChecked(B);
	const FBALNode& SolvedMerge = Proxies.FindChecked(Merge);
	const float Gap = FMath::Max(Settings.GapX, Settings.NodeMargin * 2.f);
	TestTrue(TEXT("Deep predecessor is placed after the cycle entry"),
		SolvedB.OutPos.X + 0.01f >= SolvedA.OutPos.X + SolvedA.Size.X + Gap);
	TestTrue(TEXT("Merge follows the longest non-back-edge path"),
		SolvedMerge.OutPos.X + 0.01f >= SolvedB.OutPos.X + SolvedB.Size.X + Gap);
	TestTrue(TEXT("Reachable data cycle produces finite entry coordinates"),
		FMath::IsFinite(SolvedA.OutPos.X) && FMath::IsFinite(SolvedA.OutPos.Y));
	TestTrue(TEXT("Reachable data cycle produces finite branch coordinates"),
		FMath::IsFinite(SolvedB.OutPos.X) && FMath::IsFinite(SolvedB.OutPos.Y));
	TestTrue(TEXT("Reachable data cycle produces finite merge coordinates"),
		FMath::IsFinite(SolvedMerge.OutPos.X) && FMath::IsFinite(SolvedMerge.OutPos.Y));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_FinalSolver_UnownedWeakUpstreamRegions,
	"BlueprintAutoLayout.FinalSolver.UnownedData.WeakUpstreamRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_FinalSolver_UnownedWeakUpstreamRegions::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* ProducerA = MakeNode(Graph, false, 0, 1);
	UEdGraphNode* SourceA = MakeNode(Graph, false, 0, 1);
	UEdGraphNode* TransformA = MakeNode(Graph, false, 1, 1);
	UEdGraphNode* MergeA = MakeNode(Graph, false, 2, 0);
	UEdGraphNode* ProducerB = MakeNode(Graph, false, 0, 1);
	UEdGraphNode* SourceB = MakeNode(Graph, false, 0, 1);
	UEdGraphNode* TransformB = MakeNode(Graph, false, 1, 1);
	UEdGraphNode* MergeB = MakeNode(Graph, false, 2, 0);

	const FVector2D ProducerAStart(-1000.f, 20.f);
	const FVector2D ProducerBStart(2000.f, 320.f);
	const FVector2D SourceAStart(8000.f, 40.f);
	const FVector2D SourceBStart(12000.f, 340.f);
	const FVector2D NodeSize(100.f, 60.f);

	TMap<UEdGraphNode*, FBALNode> Proxies;
	FBALNode ProducerAProxy = MakeProxy(
		ProducerA, EBALNodeRole::Exec, NodeSize, ProducerAStart, true);
	FBALNode SourceAProxy = MakeProxy(
		SourceA, EBALNodeRole::Pure, NodeSize, SourceAStart);
	FBALNode TransformAProxy = MakeProxy(
		TransformA, EBALNodeRole::Pure, NodeSize, FVector2D(-4000.f, 40.f));
	FBALNode MergeAProxy = MakeProxy(
		MergeA, EBALNodeRole::Pure, NodeSize, FVector2D(-6000.f, 40.f));
	FBALNode ProducerBProxy = MakeProxy(
		ProducerB, EBALNodeRole::Exec, NodeSize, ProducerBStart, true);
	FBALNode SourceBProxy = MakeProxy(
		SourceB, EBALNodeRole::Pure, NodeSize, SourceBStart);
	FBALNode TransformBProxy = MakeProxy(
		TransformB, EBALNodeRole::Pure, NodeSize, FVector2D(-8000.f, 340.f));
	FBALNode MergeBProxy = MakeProxy(
		MergeB, EBALNodeRole::Pure, NodeSize, FVector2D(-10000.f, 340.f));
	FBALNode* OrderedProxies[] = {
		&ProducerAProxy, &SourceAProxy, &TransformAProxy, &MergeAProxy,
		&ProducerBProxy, &SourceBProxy, &TransformBProxy, &MergeBProxy
	};
	for (int32 Index = 0; Index < 8; ++Index)
	{
		OrderedProxies[Index]->StableIndex = Index;
	}
	ProducerAProxy.bConstrained = true;
	ProducerAProxy.ConstraintType = EBALConstraintType::Hard;
	ProducerBProxy.bConstrained = true;
	ProducerBProxy.ConstraintType = EBALConstraintType::Hard;
	Proxies.Add(ProducerA, ProducerAProxy);
	Proxies.Add(SourceA, SourceAProxy);
	Proxies.Add(TransformA, TransformAProxy);
	Proxies.Add(MergeA, MergeAProxy);
	Proxies.Add(ProducerB, ProducerBProxy);
	Proxies.Add(SourceB, SourceBProxy);
	Proxies.Add(TransformB, TransformBProxy);
	Proxies.Add(MergeB, MergeBProxy);

	TArray<FBALEdge> Edges;
	Edges.Add(MakeFinalSolverDataEdge(ProducerA, MergeA));
	Edges.Add(MakeFinalSolverDataEdge(SourceA, TransformA));
	Edges.Add(MakeFinalSolverDataEdge(TransformA, MergeA));
	Edges.Add(MakeFinalSolverDataEdge(ProducerB, MergeB));
	Edges.Add(MakeFinalSolverDataEdge(SourceB, TransformB));
	Edges.Add(MakeFinalSolverDataEdge(TransformB, MergeB));
	WireData(ProducerA, 0, MergeA, 0);
	WireData(SourceA, 0, TransformA, 0);
	WireData(TransformA, 0, MergeA, 1);
	WireData(ProducerB, 0, MergeB, 0);
	WireData(SourceB, 0, TransformB, 0);
	WireData(TransformB, 0, MergeB, 1);

	FBALComponent ComponentA;
	ComponentA.Id = 0;
	ComponentA.Nodes = { ProducerA, SourceA, TransformA, MergeA };
	ComponentA.Anchor = ProducerA;
	ComponentA.OriginalAnchor = ProducerAStart;
	ComponentA.bHasHardAnchor = true;
	FBALComponent ComponentB;
	ComponentB.Id = 1;
	ComponentB.Nodes = { ProducerB, SourceB, TransformB, MergeB };
	ComponentB.Anchor = ProducerB;
	ComponentB.OriginalAnchor = ProducerBStart;
	ComponentB.bHasHardAnchor = true;
	TArray<FBALComponent> Components;
	Components.Add(ComponentA);
	Components.Add(ComponentB);

	FBALSettings Settings;
	Settings.bPreserveAnchors = false;
	Settings.NodeMargin = 0.f;
	Settings.CrossingReductionPasses = 0;
	Settings.AlignmentPasses = 0;
	SolveFinalSolverRegression(Proxies, Edges, Components, Settings);

	const float Gap = FMath::Max(Settings.GapX, Settings.NodeMargin * 2.f);
	bool bAllForward = true;
	for (const FBALEdge& Edge : Edges)
	{
		const FBALNode& Source = Proxies.FindChecked(Edge.Source);
		const FBALNode& Target = Proxies.FindChecked(Edge.Target);
		bAllForward = bAllForward
			&& Target.OutPos.X + 0.01f >= Source.OutPos.X + Source.Size.X + Gap;
	}
	bool bAllFinite = true;
	for (const TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		bAllFinite = bAllFinite
			&& FMath::IsFinite(Pair.Value.OutPos.X)
			&& FMath::IsFinite(Pair.Value.OutPos.Y);
	}
	TestTrue(TEXT("Both disconnected regions receive finite coordinates"), bAllFinite);
	TestTrue(TEXT("Every non-back-edge remains horizontally monotonic"), bAllForward);
	TestTrue(TEXT("First upstream root is anchored to its own external producer"),
		FMath::IsNearlyEqual(
			Proxies.FindChecked(SourceA).OutPos.X,
			ProducerAStart.X + NodeSize.X + Gap,
			0.01f));
	TestTrue(TEXT("Second disconnected root uses its independent producer boundary"),
		FMath::IsNearlyEqual(
			Proxies.FindChecked(SourceB).OutPos.X,
			ProducerBStart.X + NodeSize.X + Gap,
			0.01f));
	TestTrue(TEXT("First upstream root does not retain its far-right old coordinate"),
		Proxies.FindChecked(SourceA).OutPos.X < SourceAStart.X - 1000.f);
	TestTrue(TEXT("Second upstream root does not retain its far-right old coordinate"),
		Proxies.FindChecked(SourceB).OutPos.X < SourceBStart.X - 1000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_FinalSolver_HardComponentPackedFirst,
	"BlueprintAutoLayout.FinalSolver.Components.HardAnchorPackedFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_FinalSolver_HardComponentPackedFirst::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* FreeA = MakeNode(Graph, false, 0, 0);
	UEdGraphNode* FreeB = MakeNode(Graph, false, 0, 0);
	UEdGraphNode* Hard = MakeNode(Graph, false, 0, 0);
	const FVector2D FreeAStart(0.f, 0.f);
	const FVector2D FreeBStart(120.f, 0.f);
	const FVector2D HardStart(50.f, 20.f);

	TMap<UEdGraphNode*, FBALNode> Proxies;
	FBALNode FreeAProxy = MakeProxy(
		FreeA, EBALNodeRole::Isolated, FVector2D(100.f, 80.f), FreeAStart);
	FBALNode FreeBProxy = MakeProxy(
		FreeB, EBALNodeRole::Isolated, FVector2D(100.f, 80.f), FreeBStart);
	FBALNode HardProxy = MakeProxy(
		Hard, EBALNodeRole::Isolated, FVector2D(100.f, 80.f), HardStart, true);
	FreeAProxy.StableIndex = 0;
	FreeBProxy.StableIndex = 1;
	HardProxy.StableIndex = 2;
	HardProxy.bConstrained = true;
	HardProxy.ConstraintType = EBALConstraintType::Hard;
	Proxies.Add(FreeA, FreeAProxy);
	Proxies.Add(FreeB, FreeBProxy);
	Proxies.Add(Hard, HardProxy);

	FBALComponent FreeComponent;
	FreeComponent.Id = 0;
	FreeComponent.Nodes = { FreeA, FreeB };
	FreeComponent.Anchor = FreeA;
	FreeComponent.OriginalAnchor = FreeAStart;
	FBALComponent HardComponent;
	HardComponent.Id = 1;
	HardComponent.Nodes = { Hard };
	HardComponent.Anchor = Hard;
	HardComponent.OriginalAnchor = HardStart;
	HardComponent.bHasHardAnchor = true;
	// The free component sorts first by original coordinates in the old behavior.
	TArray<FBALComponent> Components;
	Components.Add(FreeComponent);
	Components.Add(HardComponent);
	TArray<FBALEdge> Edges;

	FBALSettings Settings;
	Settings.bPreserveAnchors = false;
	Settings.ComponentGapX = 16.f;
	Settings.ComponentGapY = 16.f;
	SolveFinalSolverRegression(Proxies, Edges, Components, Settings);

	const FBALNode& SolvedFreeA = Proxies.FindChecked(FreeA);
	const FBALNode& SolvedFreeB = Proxies.FindChecked(FreeB);
	const FBALNode& SolvedHard = Proxies.FindChecked(Hard);
	const FVector2D FreeADelta = SolvedFreeA.OutPos - FreeAStart;
	const FVector2D FreeBDelta = SolvedFreeB.OutPos - FreeBStart;
	TestTrue(TEXT("Hard anchor remains fixed"),
		SolvedHard.OutPos.Equals(HardStart, 0.01f));
	TestFalse(TEXT("First free member no longer overlaps the hard component"),
		NodesOverlap(SolvedFreeA, SolvedHard, Settings.ComponentGapY * 0.5f));
	TestFalse(TEXT("Second free member no longer overlaps the hard component"),
		NodesOverlap(SolvedFreeB, SolvedHard, Settings.ComponentGapY * 0.5f));
	TestTrue(TEXT("Free component is moved away from the hard component"),
		!FreeADelta.IsNearlyZero(0.01f));
	TestTrue(TEXT("All free-component members receive one rigid translation"),
		FreeADelta.Equals(FreeBDelta, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_FinalSolver_LargePureGroupPinOrdering,
	"BlueprintAutoLayout.FinalSolver.PureGroups.LargeGroupPinOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_FinalSolver_LargePureGroupPinOrdering::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	const int32 PureCount = 1024;
	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Consumer = MakeNode(Graph, false, 0, 0);
	TMap<UEdGraphNode*, FBALNode> Proxies;
	FBALNode ConsumerProxy = MakeProxy(
		Consumer, EBALNodeRole::Exec, FVector2D(200.f, 80.f));
	ConsumerProxy.StableIndex = 0;
	Proxies.Add(Consumer, ConsumerProxy);

	TArray<UEdGraphNode*> NodesByTargetPin;
	NodesByTargetPin.SetNumZeroed(PureCount);
	TArray<FBALEdge> Edges;
	Edges.Reserve(PureCount);
	FBALComponent Component;
	Component.Id = 0;
	Component.Anchor = Consumer;
	Component.Nodes.Add(Consumer);
	for (int32 Index = 0; Index < PureCount; ++Index)
	{
		UEdGraphNode* PureNode = MakeNode(Graph, false, 0, 0);
		FBALNode PureProxy = MakeProxy(
			PureNode,
			EBALNodeRole::Pure,
			FVector2D(80.f, 20.f),
			FVector2D(0.f, static_cast<float>(Index)));
		PureProxy.StableIndex = Index + 1;
		PureProxy.PureOwner = Consumer;
		PureProxy.PureDepth = 0;
		Proxies.Add(PureNode, PureProxy);
		Component.Nodes.Add(PureNode);

		const int32 TargetPin = PureCount - 1 - Index;
		NodesByTargetPin[TargetPin] = PureNode;
		Edges.Add(MakeFinalSolverDataEdge(PureNode, Consumer, TargetPin));
	}
	TArray<FBALComponent> Components;
	Components.Add(Component);

	FBALSettings Settings;
	Settings.bPreserveAnchors = false;
	Settings.NodeMargin = 0.f;
	Settings.PureGapY = 4.f;
	Settings.CrossingReductionPasses = 0;
	Settings.AlignmentPasses = 0;
	SolveFinalSolverRegression(Proxies, Edges, Components, Settings);

	bool bStrictlyOrdered = true;
	bool bAllFinite = true;
	for (int32 TargetPin = 0; TargetPin < PureCount; ++TargetPin)
	{
		const FBALNode& Pure = Proxies.FindChecked(NodesByTargetPin[TargetPin]);
		bAllFinite = bAllFinite
			&& FMath::IsFinite(Pure.OutPos.X)
			&& FMath::IsFinite(Pure.OutPos.Y);
		if (TargetPin > 0)
		{
			const FBALNode& Previous = Proxies.FindChecked(NodesByTargetPin[TargetPin - 1]);
			bStrictlyOrdered = bStrictlyOrdered && Previous.OutPos.Y < Pure.OutPos.Y;
		}
	}
	TestTrue(TEXT("Large pure group receives finite coordinates"), bAllFinite);
	TestTrue(TEXT("Large pure group is ordered by relevant target pin"), bStrictlyOrdered);
	TestTrue(TEXT("Target pin priority overrides reverse stable order"),
		Proxies.FindChecked(NodesByTargetPin[0]).StableIndex
			> Proxies.FindChecked(NodesByTargetPin.Last()).StableIndex
		&& Proxies.FindChecked(NodesByTargetPin[0]).OutPos.Y
			< Proxies.FindChecked(NodesByTargetPin.Last()).OutPos.Y);
	return true;
}
