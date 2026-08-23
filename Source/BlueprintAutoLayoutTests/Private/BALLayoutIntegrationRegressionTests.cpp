// BALLayoutIntegrationRegressionTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALTestHelpers.h"
#include "BALGraphAnalyzer.h"
#include "BALLayoutSolver.h"
#include "Misc/AutomationTest.h"

namespace
{
	void SolveLayoutIntegrationAnalysis(
		FBALGraphAnalyzer::FAnalysisResult& Analysis,
		const FBALSettings& Settings,
		const TArray<FBALConstraint>& Constraints,
		EBALPureDir PureDir = EBALPureDir::West)
	{
		FBALLayoutSolver::FSolverInput Input;
		Input.Proxies = &Analysis.Proxies;
		Input.ExecRoots = &Analysis.ExecRoots;
		Input.Edges = &Analysis.Edges;
		Input.Components = &Analysis.Components;
		Input.IsolatedPures = &Analysis.IsolatedPures;
		Input.IsolatedNodes = &Analysis.IsolatedNodes;
		Input.Constraints = &Constraints;
		Input.PureDir = PureDir;
		Input.Settings = &Settings;
		FBALLayoutSolver::Solve(Input);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_LayoutIntegration_MultipleHardAnchors,
	"BlueprintAutoLayout.AdvancedRegression.Layout.MultipleHardAnchorsUseNearestBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_LayoutIntegration_MultipleHardAnchors::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Left = MakeNode(Graph, true, 0, 0, 0, 0);
	UEdGraphNode* Free = MakeNode(Graph, true, 0, 0, 900, 0);
	UEdGraphNode* Right = MakeNode(Graph, true, 0, 0, 1000, 0);
	WireExec(Left, Free);
	WireExec(Free, Right);

	TArray<FBALConstraint> Constraints;
	FBALConstraint Locked;
	Locked.Type = EBALConstraintType::Hard;
	Locked.Node = Left;
	Constraints.Add(Locked);
	Locked.Node = Right;
	Constraints.Add(Locked);

	FBALSettings Settings;
	Settings.bPreserveAnchors = true;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);
	SolveLayoutIntegrationAnalysis(Analysis, Settings, Constraints);

	const FBALNode& FreeProxy = Analysis.Proxies.FindChecked(Free);
	const FBALNode& RightProxy = Analysis.Proxies.FindChecked(Right);
	TestTrue(TEXT("Free region follows its closest locked boundary"), FreeProxy.OutPos.X > 500.f);
	TestTrue(TEXT("Free node remains before the right boundary"),
		FreeProxy.OutPos.X + FreeProxy.Size.X < RightProxy.OutPos.X);
	TestTrue(TEXT("Right hard node remains fixed"),
		RightProxy.OutPos.Equals(RightProxy.OriginalPos, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_LayoutIntegration_NorthPureClusterSpacing,
	"BlueprintAutoLayout.AdvancedRegression.Layout.NorthPureClustersReserveVerticalSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_LayoutIntegration_NorthPureClusterSpacing::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Root = MakeNode(Graph, true, 0, 0, 0, 0);
	UEdGraphNode* Upper = MakeNode(Graph, true, 1, 0, 300, 0);
	UEdGraphNode* Lower = MakeNode(Graph, true, 1, 0, 300, 300);
	UEdGraphNode* UpperPure = MakeNode(Graph, false, 0, 1, 100, -300);
	UEdGraphNode* LowerPure = MakeNode(Graph, false, 0, 1, 100, 600);
	AddExecOutput(Root, FName(TEXT("SecondBranch")));
	WireExecPins(Root, 0, Upper);
	WireExecPins(Root, 1, Lower);
	WireData(UpperPure, 0, Upper, 0);
	WireData(LowerPure, 0, Lower, 0);

	FBALSettings Settings;
	Settings.bPreserveAnchors = false;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);
	SolveLayoutIntegrationAnalysis(Analysis, Settings, Constraints, EBALPureDir::North);

	const FBALNode& UpperExec = Analysis.Proxies.FindChecked(Upper);
	const FBALNode& LowerExec = Analysis.Proxies.FindChecked(Lower);
	const FBALNode& UpperData = Analysis.Proxies.FindChecked(UpperPure);
	const FBALNode& LowerData = Analysis.Proxies.FindChecked(LowerPure);
	TestFalse(TEXT("North pure groups do not overlap each other"),
		NodesOverlap(UpperData, LowerData, Settings.NodeMargin));
	TestFalse(TEXT("Upper pure group does not enter lower exec row"),
		NodesOverlap(UpperData, LowerExec, Settings.NodeMargin));
	TestFalse(TEXT("Lower pure group does not enter upper exec row"),
		NodesOverlap(LowerData, UpperExec, Settings.NodeMargin));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_LayoutIntegration_ExecToPureSink,
	"BlueprintAutoLayout.AdvancedRegression.Layout.ExecToPureSinkMovesRight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_LayoutIntegration_ExecToPureSink::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Producer = MakeNode(Graph, true, 0, 1, 500, 300);
	UEdGraphNode* Sink = MakeNode(Graph, false, 1, 0, -500, -200);
	WireData(Producer, 0, Sink, 0);

	FBALSettings Settings;
	Settings.bPreserveAnchors = false;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);
	SolveLayoutIntegrationAnalysis(Analysis, Settings, Constraints);

	const FBALNode& ProducerProxy = Analysis.Proxies.FindChecked(Producer);
	const FBALNode& SinkProxy = Analysis.Proxies.FindChecked(Sink);
	TestNull(TEXT("Downstream pure sink has no consumer owner"), SinkProxy.PureOwner);
	TestTrue(TEXT("Downstream pure sink is placed after producer"),
		SinkProxy.OutPos.X >= ProducerProxy.OutPos.X + ProducerProxy.Size.X
			+ FMath::Max(Settings.GapX, Settings.NodeMargin * 2.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_LayoutIntegration_ExternalSoftOrigin,
	"BlueprintAutoLayout.AdvancedRegression.Constraints.ExternalSoftOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_LayoutIntegration_ExternalSoftOrigin::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Node = MakeNode(Graph, false, 0, 0, 1000, 0);
	FBALConstraint Soft;
	Soft.Node = Node;
	Soft.Type = EBALConstraintType::Soft;
	Soft.OriginalPos = FVector2D(100.f, 20.f);
	Soft.MaxDrift = 10.f;
	TArray<FBALConstraint> Constraints;
	Constraints.Add(Soft);

	FBALSettings Settings;
	Settings.bPreserveAnchors = false;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);
	FBALNode& Proxy = Analysis.Proxies.FindChecked(Node);
	TestTrue(TEXT("Analyzer uses caller-provided soft anchor"),
		Proxy.OriginalPos.Equals(Soft.OriginalPos, 0.01f));
	Proxy.OutPos = FVector2D(500.f, 20.f);
	SolveLayoutIntegrationAnalysis(Analysis, Settings, Constraints);
	TestTrue(TEXT("Soft node is clamped around the caller-provided anchor"),
		Proxy.OutPos.Equals(FVector2D(110.f, 20.f), 0.01f));
	return true;
}
