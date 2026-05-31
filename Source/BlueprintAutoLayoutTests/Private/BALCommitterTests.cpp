// BALCommitterTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Covers: FBALCommitter
//   - GridSnap            Computed positions are snapped to 8px grid before write
//   - MoveThreshold       Positions within MoveThreshold of original are NOT written
//   - HardConstraintSkip  Hard-constrained nodes are never written
//   - CommitCount         Return value equals actual number of written nodes
//   - ZeroGridSnap        GridSnap=0 → no snapping, raw positions written

#include "BALTestHelpers.h"
#include "BALCommitter.h"
#include "BALConstraintCollector.h"
#include "Misc/AutomationTest.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: build a single-node proxy map, run Commit, return written count.
// ─────────────────────────────────────────────────────────────────────────────
static int32 CommitSingle(UEdGraphNode* Node,
                           FVector2D OriginalPos,
                           FVector2D NewOutPos,
                           const FBALSettings& S,
                           const TArray<FBALConstraint>& Constraints = {})
{
	using namespace BALTest;

	// Set the UEdGraphNode's actual position to OriginalPos
	Node->NodePosX = (int32)OriginalPos.X;
	Node->NodePosY = (int32)OriginalPos.Y;

	FBALNode Proxy = MakeProxy(Node, EBALNodeRole::Exec,
	                            FVector2D(200.f, 80.f), NewOutPos);
	Proxy.OriginalPos = OriginalPos;

	// bLocked is derived from Constraints inside Committer — set it on the proxy too
	bool bHard = Constraints.ContainsByPredicate(
		[Node](const FBALConstraint& C)
		{ return C.Node == Node && C.Type == EBALConstraintType::Hard; });
	Proxy.bLocked = bHard;

	TMap<UEdGraphNode*, FBALNode> Proxies;
	Proxies.Add(Node, Proxy);

	// Committer needs UEdGraph owner for Modify() – use the outer
	UEdGraph* G = Cast<UEdGraph>(Node->GetOuter());

	return FBALCommitter::Commit(G, Proxies, Constraints, S);
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_GridSnap,
	"BlueprintAutoLayout.Committer.GridSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_GridSnap::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G    = MakeGraph();
	UEdGraphNode* N = MakeNode(G, false, 0, 0, 0, 0);

	FBALSettings S;
	S.GridSnap      = 8.f;
	S.MoveThreshold = 2.f;

	// Move to a non-snapped position (37, 53) → should snap to (40, 56)
	CommitSingle(N, FVector2D(0.f, 0.f), FVector2D(37.f, 53.f), S);

	// After commit, the node's integer position should be snapped
	TestTrue(TEXT("NodePosX snapped to 8-grid"), BALTest::IsSnapped((float)N->NodePosX, 8.f));
	TestTrue(TEXT("NodePosY snapped to 8-grid"), BALTest::IsSnapped((float)N->NodePosY, 8.f));

	// Specifically: 37 → 40, 53 → 56 (round to nearest multiple of 8)
	TestEqual(TEXT("NodePosX == 40"), N->NodePosX, 40);
	TestEqual(TEXT("NodePosY == 56"), N->NodePosY, 56);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_MoveThreshold,
	"BlueprintAutoLayout.Committer.MoveThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_MoveThreshold::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G     = MakeGraph();
	UEdGraphNode* N = MakeNode(G, false, 0, 0, 100, 200);

	FBALSettings S;
	S.GridSnap       = 0.f;   // disable snap so we control exact values
	S.MoveThreshold  = 2.f;

	// Movement of 1 pixel — below threshold, must NOT be written
	int32 Written = CommitSingle(N, FVector2D(100.f, 200.f), FVector2D(101.f, 200.f), S);

	TestEqual(TEXT("Sub-threshold move: 0 nodes written"), Written, 0);
	TestEqual(TEXT("NodePosX unchanged"), N->NodePosX, 100);
	TestEqual(TEXT("NodePosY unchanged"), N->NodePosY, 200);

	// Movement of 10 pixels — above threshold, must be written
	Written = CommitSingle(N, FVector2D(100.f, 200.f), FVector2D(110.f, 220.f), S);
	TestEqual(TEXT("Super-threshold move: 1 node written"), Written, 1);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_HardConstraintSkip,
	"BlueprintAutoLayout.Committer.HardConstraintSkip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_HardConstraintSkip::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G     = MakeGraph();
	UEdGraphNode* N = MakeNode(G, false, 0, 0, 50, 50);

	// Pin the node (Hard constraint)
	FBALConstraintCollector::SetPinned(N, true);

	TArray<FBALConstraint> Constraints;
	FBALConstraintCollector::Collect(G, Constraints);

	FBALSettings S;
	S.GridSnap      = 0.f;
	S.MoveThreshold = 2.f;

	// Try to move it far away
	int32 Written = CommitSingle(N,
	    FVector2D(50.f, 50.f),
	    FVector2D(999.f, 888.f),
	    S, Constraints);

	TestEqual(TEXT("Hard-constrained node: 0 written"), Written, 0);
	TestEqual(TEXT("NodePosX locked at 50"), N->NodePosX, 50);
	TestEqual(TEXT("NodePosY locked at 50"), N->NodePosY, 50);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_CommitCount,
	"BlueprintAutoLayout.Committer.CommitCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_CommitCount::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	// 3 nodes: 2 free (large move), 1 pinned
	UEdGraphNode* N1 = MakeNode(G, false, 0, 0, 0,   0);
	UEdGraphNode* N2 = MakeNode(G, false, 0, 0, 300, 0);
	UEdGraphNode* N3 = MakeNode(G, false, 0, 0, 600, 0);
	FBALConstraintCollector::SetPinned(N3, true);

	TArray<FBALConstraint> Constraints;
	FBALConstraintCollector::Collect(G, Constraints);

	FBALSettings S;
	S.GridSnap      = 0.f;
	S.MoveThreshold = 2.f;

	// Build proxy map with large moves for N1, N2; hard constraint for N3
	TMap<UEdGraphNode*, FBALNode> Proxies;
	{
		auto P1 = BALTest::MakeProxy(N1, EBALNodeRole::Exec, FVector2D(200,80), FVector2D(100, 100));
		P1.OriginalPos = FVector2D(0,0);
		Proxies.Add(N1, P1);

		auto P2 = BALTest::MakeProxy(N2, EBALNodeRole::Exec, FVector2D(200,80), FVector2D(500, 100));
		P2.OriginalPos = FVector2D(300,0);
		Proxies.Add(N2, P2);

		auto P3 = BALTest::MakeProxy(N3, EBALNodeRole::Exec, FVector2D(200,80), FVector2D(999, 999));
		P3.OriginalPos = FVector2D(600,0);
		P3.bLocked = true;
		Proxies.Add(N3, P3);
	}

	int32 Written = FBALCommitter::Commit(G, Proxies, Constraints, S);

	// N1 and N2 should be written; N3 (hard) should not
	TestEqual(TEXT("Commit returns 2 written nodes"), Written, 2);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_ZeroGridSnap,
	"BlueprintAutoLayout.Committer.ZeroGridSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_ZeroGridSnap::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G     = MakeGraph();
	UEdGraphNode* N = MakeNode(G, false, 0, 0, 0, 0);

	FBALSettings S;
	S.GridSnap      = 0.f;
	S.MoveThreshold = 2.f;

	// Move to non-grid-aligned value — should be written as-is
	CommitSingle(N, FVector2D(0.f, 0.f), FVector2D(37.f, 53.f), S);

	TestEqual(TEXT("NodePosX == 37 (no snap)"), N->NodePosX, 37);
	TestEqual(TEXT("NodePosY == 53 (no snap)"), N->NodePosY, 53);

	return true;
}
