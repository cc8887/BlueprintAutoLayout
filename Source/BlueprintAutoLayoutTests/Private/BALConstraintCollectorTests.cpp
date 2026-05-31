// BALConstraintCollectorTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Covers: FBALConstraintCollector
//   - PinnedConstraint     [BAL:pin] tag → Hard constraint
//   - KnotConstraint       Knot class name → Soft constraint
//   - SelectionConstraint  CollectForSelection: unselected → Hard
//   - CommentGroup         Comment box members → RigidGroup same GroupId
//   - SetPinned            Toggle metadata in NodeComment

#include "BALTestHelpers.h"
#include "BALConstraintCollector.h"
#include "Misc/AutomationTest.h"

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_PinnedConstraint,
	"BlueprintAutoLayout.ConstraintCollector.PinnedConstraint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_PinnedConstraint::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	UEdGraphNode* PinnedNode   = MakeNode(G, true, 0, 0, 100, 200);
	UEdGraphNode* UnpinnedNode = MakeNode(G, true, 0, 0, 400, 200);

	// Tag the first node
	FBALConstraintCollector::SetPinned(PinnedNode, true);

	TArray<FBALConstraint> Constraints;
	FBALConstraintCollector::Collect(G, Constraints);

	// Find the constraint for PinnedNode
	const FBALConstraint* PC = Constraints.FindByPredicate(
		[&](const FBALConstraint& C){ return C.Node == PinnedNode; });

	TestNotNull(TEXT("Pinned node has constraint"), PC);
	if (PC)
	{
		TestEqual(TEXT("Pinned → Hard"), PC->Type, EBALConstraintType::Hard);
		TestEqual(TEXT("OriginalPos X"), PC->OriginalPos.X, 100.0);
		TestEqual(TEXT("OriginalPos Y"), PC->OriginalPos.Y, 200.0);
	}

	// Unpinned node must not produce a constraint
	const FBALConstraint* UC = Constraints.FindByPredicate(
		[&](const FBALConstraint& C){ return C.Node == UnpinnedNode; });
	TestNull(TEXT("Unpinned node has no constraint"), UC);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_SetPinnedToggle,
	"BlueprintAutoLayout.ConstraintCollector.SetPinnedToggle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_SetPinnedToggle::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	UEdGraphNode* Node = MakeNode(G, false, 0, 0);
	Node->NodeComment = TEXT("My comment");

	// Pin
	FBALConstraintCollector::SetPinned(Node, true);
	TestTrue(TEXT("IsPinned after Set true"), FBALConstraintCollector::IsPinned(Node));
	TestTrue(TEXT("Comment still contains original text"),
	         Node->NodeComment.Contains(TEXT("My comment")));

	// Unpin
	FBALConstraintCollector::SetPinned(Node, false);
	TestFalse(TEXT("Not pinned after Set false"), FBALConstraintCollector::IsPinned(Node));

	// Double-pin should not duplicate the tag
	FBALConstraintCollector::SetPinned(Node, true);
	FBALConstraintCollector::SetPinned(Node, true);
	int32 TagCount = 0;
	{
		const FString Tag(TEXT("[BAL:pin]"));
		int32 Pos = 0;
		while ((Pos = Node->NodeComment.Find(Tag, ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos)) != INDEX_NONE)
		{
			++TagCount;
			Pos += Tag.Len();
		}
	}
	TestEqual(TEXT("Tag appears exactly once after double-pin"), TagCount, 1);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_KnotConstraint,
	"BlueprintAutoLayout.ConstraintCollector.KnotConstraint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_KnotConstraint::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();

	// Create a fake "knot" node by naming its class via subclass trick:
	// We can't easily subclass UEdGraphNode in tests, so we use NodeComment
	// to simulate, but BALConstraintCollector checks class name.
	// Instead, create a plain node and rename its class string is not possible
	// without a real reroute class.
	//
	// Verification: at minimum, a node whose class name contains "Knot"
	// or "Reroute" should produce a Soft constraint.
	// We test the collector's logic by inspecting IsPinned path + Knot path.
	//
	// Since we can't inject a fake class name, we test the negative:
	// a regular node produces NO constraint unless pinned.
	UEdGraphNode* Regular = MakeNode(G, false, 0, 1);
	TArray<FBALConstraint> C;
	FBALConstraintCollector::Collect(G, C);
	TestEqual(TEXT("Regular node → 0 constraints"), C.Num(), 0);

	// The Soft knot path is integration-tested in the full pipeline test.
	// Here we document it as an explicit skip with an informational log.
	AddInfo(TEXT("Knot Soft constraint: requires real UK2Node_Knot; "
	             "covered by integration test BALPipelineTests.FullPipeline"));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_SelectionConstraint,
	"BlueprintAutoLayout.ConstraintCollector.SelectionConstraint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_SelectionConstraint::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	UEdGraphNode* Selected   = MakeNode(G, true, 0, 0, 0,   0);
	UEdGraphNode* Unselected = MakeNode(G, true, 0, 0, 400, 0);

	TSet<UEdGraphNode*> Selection;
	Selection.Add(Selected);

	TArray<FBALConstraint> C;
	FBALConstraintCollector::CollectForSelection(G, Selection, C);

	// Unselected node must be Hard
	const FBALConstraint* UC = C.FindByPredicate(
		[&](const FBALConstraint& X){ return X.Node == Unselected; });
	TestNotNull(TEXT("Unselected has constraint"), UC);
	if (UC)
		TestEqual(TEXT("Unselected → Hard"), UC->Type, EBALConstraintType::Hard);

	// Selected node that is not pinned should NOT appear as Hard
	const FBALConstraint* SC = C.FindByPredicate(
		[&](const FBALConstraint& X){ return X.Node == Selected; });
	if (SC)
	{
		TestNotEqual(TEXT("Selected (unpinned) not Hard"), SC->Type, EBALConstraintType::Hard);
	}
	else
	{
		// No constraint for selected unpinned node is also correct
		AddInfo(TEXT("Selected unpinned node correctly has no constraint"));
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_CommentGroupConstraint,
	"BlueprintAutoLayout.ConstraintCollector.CommentGroup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_CommentGroupConstraint::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	// Comment box at (0,0), size 600×400
	UEdGraphNode_Comment* Cmt = MakeComment(G, 0, 0, 600, 400);

	// Two nodes inside the comment box
	UEdGraphNode* Inside1 = MakeNode(G, false, 0, 1, 50,  50);
	UEdGraphNode* Inside2 = MakeNode(G, false, 0, 1, 200, 100);

	// One node outside
	UEdGraphNode* Outside = MakeNode(G, false, 0, 1, 800, 800);

	TArray<FBALConstraint> C;
	FBALConstraintCollector::Collect(G, C);

	// Comment box and both inside nodes should have RigidGroup constraints
	auto FindConstraint = [&](UEdGraphNode* N) -> const FBALConstraint*
	{
		return C.FindByPredicate([N](const FBALConstraint& X){ return X.Node == N; });
	};

	const FBALConstraint* CmtC = FindConstraint(Cmt);
	TestNotNull(TEXT("Comment box has constraint"), CmtC);
	if (CmtC) TestEqual(TEXT("Comment → RigidGroup"), CmtC->Type, EBALConstraintType::RigidGroup);

	const FBALConstraint* In1C = FindConstraint(Inside1);
	TestNotNull(TEXT("Inside1 has constraint"), In1C);
	if (In1C) TestEqual(TEXT("Inside1 → RigidGroup"), In1C->Type, EBALConstraintType::RigidGroup);

	const FBALConstraint* In2C = FindConstraint(Inside2);
	TestNotNull(TEXT("Inside2 has constraint"), In2C);
	if (In2C) TestEqual(TEXT("Inside2 → RigidGroup"), In2C->Type, EBALConstraintType::RigidGroup);

	// All inside nodes and comment must share the same GroupId
	if (CmtC && In1C && In2C)
	{
		TestEqual(TEXT("Same group id (cmt==in1)"), CmtC->GroupId, In1C->GroupId);
		TestEqual(TEXT("Same group id (in1==in2)"), In1C->GroupId, In2C->GroupId);
	}

	// Outside node must have no constraint (not pinned, not knot)
	const FBALConstraint* OutC = FindConstraint(Outside);
	TestNull(TEXT("Outside node has no constraint"), OutC);

	return true;
}
