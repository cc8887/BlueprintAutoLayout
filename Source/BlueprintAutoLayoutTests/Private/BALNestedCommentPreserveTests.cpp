// BALNestedCommentPreserveTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALTestHelpers.h"
#include "BALCommitter.h"
#include "BALConstraintCollector.h"
#include "BALGraphAnalyzer.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_NestedCommentPreserve,
	"BlueprintAutoLayout.Comments.NestedPreserve.NoOuterGridDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_NestedCommentPreserve::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode_Comment* Outer = MakeComment(Graph, 3, 5, 900, 700);
	UEdGraphNode_Comment* Inner = MakeComment(Graph, 101, 109, 500, 400);
	UEdGraphNode* Member = MakeNode(Graph, false, 0, 0, 180, 190);

	TArray<FBALConstraint> Constraints;
	FBALConstraintCollector::Collect(Graph, Constraints);

	FBALSettings Settings;
	Settings.bAutoSizeComments = true;
	Settings.bPreserveCommentContents = true;
	Settings.GridSnap = 8.f;
	Settings.MoveThreshold = 2.f;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);

	const int32 Written = FBALCommitter::Commit(
		Graph, Analysis.Proxies, Constraints, Settings, &Analysis.CommentGroups);
	TestEqual(TEXT("Preserved nested comment group requires no writes"), Written, 0);
	TestEqual(TEXT("Outer comment X does not snap independently"), Outer->NodePosX, 3);
	TestEqual(TEXT("Outer comment Y does not snap independently"), Outer->NodePosY, 5);
	TestEqual(TEXT("Inner comment X remains fixed"), Inner->NodePosX, 101);
	TestEqual(TEXT("Inner comment Y remains fixed"), Inner->NodePosY, 109);
	TestEqual(TEXT("Nested member X remains fixed"), Member->NodePosX, 180);
	TestEqual(TEXT("Nested member Y remains fixed"), Member->NodePosY, 190);

	Settings.bPreserveCommentContents = false;
	FBALGraphAnalyzer::FAnalysisResult UnpreservedAnalysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);
	const int32 UnpreservedWritten = FBALCommitter::Commit(
		Graph, UnpreservedAnalysis.Proxies, Constraints, Settings,
		&UnpreservedAnalysis.CommentGroups);
	TestTrue(TEXT("Unpreserved nested comments are still processed"), UnpreservedWritten > 0);
	TestTrue(TEXT("Outer comment can move or resize when preservation is disabled"),
		Outer->NodePosX != 3 || Outer->NodePosY != 5
			|| Outer->NodeWidth != 900 || Outer->NodeHeight != 700);

	return true;
}
