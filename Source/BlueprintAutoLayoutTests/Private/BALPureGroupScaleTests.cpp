// BALPureGroupScaleTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALTestHelpers.h"
#include "BALLayoutSolver.h"
#include "Misc/AutomationTest.h"

namespace
{
	FBALEdge MakePureScaleDataEdge(UEdGraphNode* Source, UEdGraphNode* Target)
	{
		FBALEdge Edge;
		Edge.Source = Source;
		Edge.Target = Target;
		Edge.Kind = EBALEdgeKind::Data;
		return Edge;
	}

	void SolvePureScaleGraph(
		TMap<UEdGraphNode*, FBALNode>& Proxies,
		TArray<FBALEdge>& Edges,
		TArray<FBALComponent>& Components,
		const FBALSettings& Settings)
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
		Input.PureDir = EBALPureDir::West;
		Input.Settings = &Settings;
		FBALLayoutSolver::Solve(Input);
	}

	FBALSettings MakePureScaleSettings()
	{
		FBALSettings Settings;
		Settings.bPreserveAnchors = false;
		Settings.bAlignExecPins = false;
		Settings.NodeMargin = 0.f;
		Settings.GapX = 64.f;
		Settings.GapY = 16.f;
		Settings.PureGapX = 12.f;
		Settings.PureGapY = 4.f;
		Settings.CrossingReductionPasses = 0;
		Settings.AlignmentPasses = 0;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_PureGroupScale_ManySparseOwners,
	"BlueprintAutoLayout.Scale.PureGroups.ManySparseOwners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_PureGroupScale_ManySparseOwners::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	const int32 ConsumerCount = 4096;
	const FVector2D ConsumerSize(120.f, 60.f);
	const FVector2D PureSize(48.f, 20.f);
	UEdGraph* Graph = MakeGraph();
	TMap<UEdGraphNode*, FBALNode> Proxies;
	Proxies.Reserve(ConsumerCount * 2);
	TArray<UEdGraphNode*> Consumers;
	TArray<UEdGraphNode*> Pures;
	Consumers.Reserve(ConsumerCount);
	Pures.Reserve(ConsumerCount);
	TArray<FBALEdge> Edges;
	Edges.Reserve(ConsumerCount);
	FBALComponent Component;
	Component.Id = 0;
	Component.Nodes.Reserve(ConsumerCount * 2);

	for (int32 Index = 0; Index < ConsumerCount; ++Index)
	{
		UEdGraphNode* Consumer = MakeNode(Graph, false, 0, 0);
		UEdGraphNode* Pure = MakeNode(Graph, false, 0, 0);
		FBALNode ConsumerProxy = MakeProxy(
			Consumer,
			EBALNodeRole::Exec,
			ConsumerSize,
			FVector2D(0.f, static_cast<float>(Index * 2)));
		FBALNode PureProxy = MakeProxy(
			Pure,
			EBALNodeRole::Pure,
			PureSize,
			FVector2D(-1000.f, static_cast<float>(Index * 2)));
		ConsumerProxy.StableIndex = Index * 2;
		PureProxy.StableIndex = Index * 2 + 1;
		PureProxy.PureOwner = Consumer;
		PureProxy.PureDepth = 0;
		Proxies.Add(Consumer, ConsumerProxy);
		Proxies.Add(Pure, PureProxy);
		Consumers.Add(Consumer);
		Pures.Add(Pure);
		Edges.Add(MakePureScaleDataEdge(Pure, Consumer));
		Component.Nodes.Add(Consumer);
		Component.Nodes.Add(Pure);
	}
	Component.Anchor = Consumers[0];
	Component.OriginalAnchor = FVector2D::ZeroVector;
	TArray<FBALComponent> Components;
	Components.Add(MoveTemp(Component));
	const FBALSettings Settings = MakePureScaleSettings();
	SolvePureScaleGraph(Proxies, Edges, Components, Settings);

	const float HorizontalGap = FMath::Max(Settings.GapX, Settings.NodeMargin * 2.f);
	int32 FiniteCount = 0;
	int32 SeparatedCount = 0;
	int32 BelowExecCount = 0;
	for (int32 Index = 0; Index < ConsumerCount; ++Index)
	{
		const FBALNode& Consumer = Proxies.FindChecked(Consumers[Index]);
		const FBALNode& Pure = Proxies.FindChecked(Pures[Index]);
		if (FMath::IsFinite(Consumer.OutPos.X) && FMath::IsFinite(Consumer.OutPos.Y)
			&& FMath::IsFinite(Pure.OutPos.X) && FMath::IsFinite(Pure.OutPos.Y))
		{
			++FiniteCount;
		}
		if (Pure.OutPos.X + Pure.Size.X + HorizontalGap <= Consumer.OutPos.X + 0.01f)
		{
			++SeparatedCount;
		}
		if (Pure.OutPos.Y > Consumer.OutPos.Y)
		{
			++BelowExecCount;
		}
	}
	TestEqual(TEXT("Every sparse owner pair receives finite coordinates"), FiniteCount, ConsumerCount);
	TestEqual(TEXT("Every sparse pure remains west of its own consumer"), SeparatedCount, ConsumerCount);
	TestEqual(TEXT("Every one-node pure group starts below its consumer's exec spine"), BelowExecCount, ConsumerCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_PureGroupScale_DeepSingleOwner,
	"BlueprintAutoLayout.Scale.PureGroups.DeepSingleOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_PureGroupScale_DeepSingleOwner::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	const int32 DepthCount = 4096;
	const FVector2D ConsumerSize(120.f, 60.f);
	const FVector2D PureSize(48.f, 20.f);
	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Consumer = MakeNode(Graph, false, 0, 0);
	TMap<UEdGraphNode*, FBALNode> Proxies;
	Proxies.Reserve(DepthCount + 1);
	FBALNode ConsumerProxy = MakeProxy(Consumer, EBALNodeRole::Exec, ConsumerSize);
	ConsumerProxy.StableIndex = 0;
	Proxies.Add(Consumer, ConsumerProxy);
	TArray<UEdGraphNode*> PuresByDepth;
	PuresByDepth.Reserve(DepthCount);
	FBALComponent Component;
	Component.Id = 0;
	Component.Anchor = Consumer;
	Component.OriginalAnchor = FVector2D::ZeroVector;
	Component.Nodes.Reserve(DepthCount + 1);
	Component.Nodes.Add(Consumer);

	for (int32 Depth = 0; Depth < DepthCount; ++Depth)
	{
		UEdGraphNode* Pure = MakeNode(Graph, false, 0, 0);
		FBALNode PureProxy = MakeProxy(
			Pure,
			EBALNodeRole::Pure,
			PureSize,
			FVector2D(2000.f + static_cast<float>(Depth), -static_cast<float>(Depth)));
		PureProxy.StableIndex = Depth + 1;
		PureProxy.PureOwner = Consumer;
		PureProxy.PureDepth = Depth;
		Proxies.Add(Pure, PureProxy);
		PuresByDepth.Add(Pure);
		Component.Nodes.Add(Pure);
	}

	TArray<FBALEdge> Edges;
	Edges.Reserve(DepthCount);
	for (int32 Depth = 0; Depth < DepthCount; ++Depth)
	{
		UEdGraphNode* Target = Depth == 0 ? Consumer : PuresByDepth[Depth - 1];
		Edges.Add(MakePureScaleDataEdge(PuresByDepth[Depth], Target));
	}
	TArray<FBALComponent> Components;
	Components.Add(MoveTemp(Component));
	const FBALSettings Settings = MakePureScaleSettings();
	SolvePureScaleGraph(Proxies, Edges, Components, Settings);

	int32 FiniteCount = 0;
	int32 BelowExecCount = 0;
	int32 HelixedDepthCount = 0;
	int32 WrappedDepthCount = 0;
	const FBALNode& SolvedConsumer = Proxies.FindChecked(Consumer);
	const float LayerGap = FMath::Max(Settings.PureGapX, Settings.NodeMargin * 2.f);
	const float VerticalGap = FMath::Max(Settings.PureGapY, Settings.NodeMargin * 2.f);
	for (int32 Depth = 0; Depth < DepthCount; ++Depth)
	{
		const FBALNode& Pure = Proxies.FindChecked(PuresByDepth[Depth]);
		if (FMath::IsFinite(Pure.OutPos.X) && FMath::IsFinite(Pure.OutPos.Y))
		{
			++FiniteCount;
		}
		if (Pure.OutPos.Y > SolvedConsumer.OutPos.Y)
		{
			++BelowExecCount;
		}
		if (Depth > 0)
		{
			const FBALNode& Shallower = Proxies.FindChecked(PuresByDepth[Depth - 1]);
			if (FMath::IsNearlyEqual(Pure.OutPos.X, Shallower.OutPos.X, 0.01f)
				&& Pure.OutPos.Y >= Shallower.OutPos.Y + Shallower.Size.Y + VerticalGap - 0.01f)
			{
				++HelixedDepthCount;
			}
			else if (Pure.OutPos.X + Pure.Size.X + LayerGap <= Shallower.OutPos.X + 0.01f)
			{
				++WrappedDepthCount;
			}
		}
	}
	TestEqual(TEXT("Every deep pure receives finite coordinates"), FiniteCount, DepthCount);
	TestEqual(TEXT("Every deep pure remains below the consumer's exec spine"), BelowExecCount, DepthCount);
	TestEqual(TEXT("Every depth either stacks or wraps west"),
		HelixedDepthCount + WrappedDepthCount, DepthCount - 1);
	TestTrue(TEXT("Deep chains use vertical helixing"), HelixedDepthCount > 0);
	TestTrue(TEXT("Deep chains wrap into additional west columns"), WrappedDepthCount > 0);
	return true;
}
