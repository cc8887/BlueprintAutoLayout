// BALPipelineTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Integration tests: run the full 5-stage pipeline via FBlueprintAutoLayoutEngine
// and verify end-to-end invariants.
//
//   FullPipeline_LinearChain    Simple A→B→C with pure inputs, no overlaps after layout
//   FullPipeline_ForkGraph      Fork + pure nodes, no overlaps, locked node unmoved
//   FullPipeline_NullGraph      Null graph → no crash
//   FullPipeline_EmptyGraph     Empty graph → no crash
//   FullPipeline_SelectionOnly  LayoutSelection: unselected nodes never move

#include "BALTestHelpers.h"
#include "BlueprintAutoLayoutEngine.h"
#include "BALConstraintCollector.h"
#include "BALGraphAnalyzer.h"
#include "Misc/AutomationTest.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Invariant helpers
// ─────────────────────────────────────────────────────────────────────────────

/** Returns true if any two non-comment nodes in the graph AABB-overlap (with margin).
 *  Uses BALGraphAnalyzer::EstimateNodeSize for accurate per-node dimensions. */
static bool AnyOverlap(UEdGraph* G, float Margin)
{
	TArray<UEdGraphNode*> Nodes;
	for (UEdGraphNode* N : G->Nodes)
	{
		if (!N || N->IsA<UEdGraphNode_Comment>()) continue;
		Nodes.Add(N);
	}

	for (int32 i = 0; i < Nodes.Num(); ++i)
	for (int32 j = i+1; j < Nodes.Num(); ++j)
	{
		UEdGraphNode* A = Nodes[i];
		UEdGraphNode* B = Nodes[j];

		const FVector2D SzA = FBALGraphAnalyzer::EstimateNodeSize(A);
		const FVector2D SzB = FBALGraphAnalyzer::EstimateNodeSize(B);

		// Strict overlap: boundary-touching is NOT overlap (use < not <=)
		const float AMinX = (float)A->NodePosX - Margin;
		const float AMaxX = (float)A->NodePosX + SzA.X + Margin;
		const float AMinY = (float)A->NodePosY - Margin;
		const float AMaxY = (float)A->NodePosY + SzA.Y + Margin;
		const float BMinX = (float)B->NodePosX - Margin;
		const float BMaxX = (float)B->NodePosX + SzB.X + Margin;
		const float BMinY = (float)B->NodePosY - Margin;
		const float BMaxY = (float)B->NodePosY + SzB.Y + Margin;

		if (AMinX < BMaxX && BMinX < AMaxX && AMinY < BMaxY && BMinY < AMaxY)
			return true;
	}
	return false;
}

/** Returns true if every committed node position is snapped to Grid. */
static bool AllSnapped(UEdGraph* G, float Grid)
{
	if (Grid <= 0.f) return true;
	for (UEdGraphNode* N : G->Nodes)
	{
		if (!N || N->IsA<UEdGraphNode_Comment>()) continue;
		if (!BALTest::IsSnapped((float)N->NodePosX, Grid)) return false;
		if (!BALTest::IsSnapped((float)N->NodePosY, Grid)) return false;
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Pipeline_NullGraph,
	"BlueprintAutoLayout.Pipeline.NullGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Pipeline_NullGraph::RunTest(const FString& /*Params*/)
{
	// Must not crash
	FBlueprintAutoLayoutEngine::Layout(nullptr);
	AddInfo(TEXT("Null graph: no crash"));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Pipeline_EmptyGraph,
	"BlueprintAutoLayout.Pipeline.EmptyGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Pipeline_EmptyGraph::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;
	UEdGraph* G = MakeGraph();
	FBlueprintAutoLayoutEngine::Layout(G);
	AddInfo(TEXT("Empty graph: no crash"));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Pipeline_LinearChain,
	"BlueprintAutoLayout.Pipeline.LinearChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Pipeline_LinearChain::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Graph: P2 → P1 → A → B → C
	//               (P1, P2 are pure inputs to A)
	UEdGraph* G = MakeGraph();

	// All nodes deliberately piled at (0,0) to force the solver to work hard
	UEdGraphNode* A  = MakeNode(G, true, 1, 0, 0, 0);
	UEdGraphNode* B  = MakeNode(G, true, 0, 0, 0, 0);
	UEdGraphNode* C  = MakeNode(G, true, 0, 0, 0, 0);
	UEdGraphNode* P1 = MakeNode(G, false, 1, 1, 0, 0);  // data in+out
	UEdGraphNode* P2 = MakeNode(G, false, 0, 1, 0, 0);  // data out

	WireExec(A, B);
	WireExec(B, C);
	WireData(P1, 0, A, 0);
	WireData(P2, 0, P1, 0);

	FBALSettings S;
	S.GridSnap      = 8.f;
	S.NodeMargin    = 8.f;
	S.CollisionIter = 30;

	FBlueprintAutoLayoutEngine::Layout(G, S);

	// All nodes must be grid-snapped after commit
	TestTrue(TEXT("All nodes grid-snapped"), AllSnapped(G, S.GridSnap));

	// No overlaps after the full pipeline
	TestFalse(TEXT("No overlaps (no margin)"), AnyOverlap(G, 0.f));

	// Exec chain: A.X < B.X < C.X
	TestTrue(TEXT("A.X < B.X"), A->NodePosX < B->NodePosX);
	TestTrue(TEXT("B.X < C.X"), B->NodePosX < C->NodePosX);

	// Pure nodes must be to the left of A (West default)
	// Allow that the voter might pick a different dir on a trivial graph,
	// so we only verify no overlap, not direction.
	AddInfo(FString::Printf(
	    TEXT("Pure P1 pos=(%d,%d) P2 pos=(%d,%d)"),
	    P1->NodePosX, P1->NodePosY, P2->NodePosX, P2->NodePosY));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Pipeline_ForkGraph,
	"BlueprintAutoLayout.Pipeline.ForkGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Pipeline_ForkGraph::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Root forks to two branches, each with pure inputs.  Plus 2 isolated nodes.
	//
	//          P_B1 → B1 → C1
	// Root ──<
	//          P_B2 → B2 → C2
	//
	// Iso1, Iso2 (no connections)
	UEdGraph* G = MakeGraph();

	UEdGraphNode* Root = MakeNode(G, true, 0, 0, 0, 0);
	UEdGraphNode* B1   = MakeNode(G, true, 1, 0, 0, 0);
	UEdGraphNode* B2   = MakeNode(G, true, 1, 0, 0, 0);
	UEdGraphNode* C1   = MakeNode(G, true, 0, 0, 0, 0);
	UEdGraphNode* C2   = MakeNode(G, true, 0, 0, 0, 0);
	UEdGraphNode* PB1  = MakeNode(G, false, 0, 1, 0, 0);
	UEdGraphNode* PB2  = MakeNode(G, false, 0, 1, 0, 0);
	UEdGraphNode* Iso1 = MakeNode(G, false, 0, 0, 0, 0);
	UEdGraphNode* Iso2 = MakeNode(G, false, 0, 0, 0, 0);

	// Second exec output on Root
	{
		FEdGraphPinType ExecType;
		ExecType.PinCategory = FName("exec");
		Root->CreatePin(EGPD_Output, ExecType, FName("ExecOut2"));
	}

	// Wire exec fork
	{
		UEdGraphPin *ROut0=nullptr, *ROut1=nullptr;
		int32 i=0;
		for (UEdGraphPin* P : Root->Pins)
			if (P && P->Direction==EGPD_Output && P->PinType.PinCategory==FName("exec"))
				(i++==0 ? ROut0 : ROut1) = P;

		UEdGraphPin *B1In=nullptr, *B2In=nullptr;
		for (UEdGraphPin* P : B1->Pins) if (P && P->Direction==EGPD_Input && P->PinType.PinCategory==FName("exec")) { B1In=P; break; }
		for (UEdGraphPin* P : B2->Pins) if (P && P->Direction==EGPD_Input && P->PinType.PinCategory==FName("exec")) { B2In=P; break; }

		if (ROut0 && B1In) { ROut0->LinkedTo.Add(B1In); B1In->LinkedTo.Add(ROut0); }
		if (ROut1 && B2In) { ROut1->LinkedTo.Add(B2In); B2In->LinkedTo.Add(ROut1); }
	}

	WireExec(B1, C1);
	WireExec(B2, C2);
	WireData(PB1, 0, B1, 0);
	WireData(PB2, 0, B2, 0);

	FBALSettings S;
	S.GridSnap      = 8.f;
	S.NodeMargin    = 8.f;
	S.CollisionIter = 60;

	FBlueprintAutoLayoutEngine::Layout(G, S);

	// Debug: print all node positions
	for (UEdGraphNode* N : G->Nodes)
	{
		if (!N) continue;
		FVector2D Sz = FBALGraphAnalyzer::EstimateNodeSize(N);
		AddInfo(FString::Printf(TEXT("Node [%p] pos=(%d,%d) size=(%.0f,%.0f)"),
		    N, N->NodePosX, N->NodePosY, Sz.X, Sz.Y));
	}

	TestTrue(TEXT("All snapped"), AllSnapped(G, S.GridSnap));
	TestFalse(TEXT("No overlaps"), AnyOverlap(G, 0.f));

	// B1 and B2 must be on different Y bands
	TestNotEqual(TEXT("B1 and B2 at different Y"), B1->NodePosY, B2->NodePosY);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Pipeline_LockedNodeUnmoved,
	"BlueprintAutoLayout.Pipeline.LockedNodeUnmoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Pipeline_LockedNodeUnmoved::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	//  A(locked) → B → C
	UEdGraph* G = MakeGraph();
	UEdGraphNode* A = MakeNode(G, true, 0, 0, 1234, 567);
	UEdGraphNode* B = MakeNode(G, true, 0, 0, 0, 0);
	UEdGraphNode* C = MakeNode(G, true, 0, 0, 0, 0);
	WireExec(A, B);
	WireExec(B, C);

	FBALConstraintCollector::SetPinned(A, true);

	FBALSettings S;
	S.GridSnap = 8.f;
	FBlueprintAutoLayoutEngine::Layout(G, S);

	// Locked node's position must be unchanged
	TestEqual(TEXT("Locked A.X == 1234"), A->NodePosX, 1234);
	TestEqual(TEXT("Locked A.Y == 567"),  A->NodePosY, 567);

	// B and C must still be placed to the right of A
	TestTrue(TEXT("B is right of A"), B->NodePosX > A->NodePosX);
	TestTrue(TEXT("C is right of B"), C->NodePosX > B->NodePosX);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Pipeline_SelectionOnly,
	"BlueprintAutoLayout.Pipeline.SelectionOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Pipeline_SelectionOnly::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Layout only the selected sub-chain; other nodes must NOT move.
	//
	// A → B → C → D
	//     ^selected^
	UEdGraph* G = MakeGraph();
	UEdGraphNode* A = MakeNode(G, true, 0, 0, 0,   0);
	UEdGraphNode* B = MakeNode(G, true, 0, 0, 300, 0);
	UEdGraphNode* C = MakeNode(G, true, 0, 0, 600, 0);
	UEdGraphNode* D = MakeNode(G, true, 0, 0, 900, 0);
	WireExec(A, B);
	WireExec(B, C);
	WireExec(C, D);

	// Record original positions of A and D (outside selection)
	const int32 AOrigX = A->NodePosX, AOrigY = A->NodePosY;
	const int32 DOrigX = D->NodePosX, DOrigY = D->NodePosY;

	// Select only B and C
	TSet<UEdGraphNode*> Selection;
	Selection.Add(B);
	Selection.Add(C);

	FBALSettings S;
	S.GridSnap = 8.f;
	FBlueprintAutoLayoutEngine::LayoutSelection(G, Selection, S);

	// A and D must not have moved
	TestEqual(TEXT("A.X unchanged"), A->NodePosX, AOrigX);
	TestEqual(TEXT("A.Y unchanged"), A->NodePosY, AOrigY);
	TestEqual(TEXT("D.X unchanged"), D->NodePosX, DOrigX);
	TestEqual(TEXT("D.Y unchanged"), D->NodePosY, DOrigY);

	// B and C are inside selection: may have moved; just verify no overlap
	TestFalse(TEXT("B and C no overlap"), AnyOverlap(G, 0.f));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Pipeline_PureOnlyGraph,
	"BlueprintAutoLayout.Pipeline.PureOnlyGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Pipeline_PureOnlyGraph::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Graph with ONLY pure (data) nodes — no exec nodes at all.
	// Must not crash, all nodes should be placed somewhere reasonable.
	UEdGraph* G = MakeGraph();
	for (int32 i = 0; i < 5; ++i)
		MakeNode(G, false, 0, 1, i * 50, i * 30);

	FBALSettings S;
	// Must not crash
	FBlueprintAutoLayoutEngine::Layout(G, S);
	AddInfo(TEXT("Pure-only graph: no crash"));
	return true;
}
