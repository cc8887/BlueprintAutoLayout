// BALLayoutSolverTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Covers: FBALLayoutSolver
//   - LinearExecLayout     Linear chain: X monotone increasing, Y consistent
//   - ForkExecLayout       Fork: children Y-bands don't overlap
//   - PureGroupWest        Pure West: OutPos.X < Consumer.OutPos.X
//   - PureGroupEast        Pure East: OutPos.X >= Consumer.Right
//   - PureGroupNorth       Pure North: OutPos.Y < Consumer.OutPos.Y
//   - PureGroupSouth       Pure South: OutPos.Y >= Consumer.Bottom
//   - PureDepthLayering    Multi-depth: deeper layers further away from consumer
//   - IsolatedPacking      Isolated nodes packed right-below graph, no overlap
//   - LockedExecNotMoved   Locked exec node OutPos unchanged after Solve

#include "BALTestHelpers.h"
#include "BALLayoutSolver.h"
#include "BALGraphAnalyzer.h"
#include "Misc/AutomationTest.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: build a proxy map and exec tree for an in-memory graph,
//  then run the solver.  Returns true on success.
// ─────────────────────────────────────────────────────────────────────────────
static void SolveGraph(UEdGraph* G,
                       EBALPureDir PureDir,
                       const FBALSettings& S,
                       FBALGraphAnalyzer::FAnalysisResult& OutResult)
{
	OutResult = FBALGraphAnalyzer::Analyze(G, S, {});

	FBALLayoutSolver::FSolverInput Input;
	Input.Proxies        = &OutResult.Proxies;
	Input.ExecRoots      = &OutResult.ExecRoots;
	Input.Edges          = &OutResult.Edges;
	Input.Components     = &OutResult.Components;
	Input.IsolatedPures  = &OutResult.IsolatedPures;
	Input.IsolatedNodes  = &OutResult.IsolatedNodes;
	Input.PureDir        = PureDir;
	Input.Settings       = &S;
	TArray<FBALConstraint> NoCon;
	Input.Constraints    = &NoCon;

	FBALLayoutSolver::Solve(Input);
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_LinearExecLayout,
	"BlueprintAutoLayout.LayoutSolver.LinearExecLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_LinearExecLayout::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	//  A → B → C (all at the same original Y — messy positions intentionally)
	UEdGraph* G = MakeGraph();
	UEdGraphNode* A = MakeNode(G, true, 0, 0, 999, 999);
	UEdGraphNode* B = MakeNode(G, true, 0, 0, 0,   500);
	UEdGraphNode* C = MakeNode(G, true, 0, 0, 100, 100);
	WireExec(A, B);
	WireExec(B, C);

	FBALSettings S;
	FBALGraphAnalyzer::FAnalysisResult R;
	SolveGraph(G, EBALPureDir::West, S, R);

	const FVector2D& PA = R.Proxies[A].OutPos;
	const FVector2D& PB = R.Proxies[B].OutPos;
	const FVector2D& PC = R.Proxies[C].OutPos;

	// X must be strictly increasing along the exec chain
	TestTrue(TEXT("A.X < B.X"), PA.X < PB.X);
	TestTrue(TEXT("B.X < C.X"), PB.X < PC.X);

	// Linear chain: B and C should be on the same Y band as A (centered)
	// Allow tolerance = half a node height
	const float Tol = R.Proxies[A].Size.Y;
	TestTrue(TEXT("A,B,C Y within same band"),
	         FMath::Abs(PA.Y - PB.Y) <= Tol && FMath::Abs(PB.Y - PC.Y) <= Tol);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_ForkExecLayout,
	"BlueprintAutoLayout.LayoutSolver.ForkExecLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_ForkExecLayout::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Root forks to B1 and B2
	UEdGraph* G = MakeGraph();
	UEdGraphNode* Root = MakeNode(G, true, 0, 0, 0, 0);
	UEdGraphNode* B1   = MakeNode(G, true, 0, 0, 0, 0);
	UEdGraphNode* B2   = MakeNode(G, true, 0, 0, 0, 0);

	// Add second exec output to Root
	{
		FEdGraphPinType ExecType;
		ExecType.PinCategory = FName("exec");
		Root->CreatePin(EGPD_Output, ExecType, FName("ExecOut2"));
	}

	UEdGraphPin* RootOut0 = nullptr, *RootOut1 = nullptr;
	int32 OIdx = 0;
	for (UEdGraphPin* P : Root->Pins)
		if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == FName("exec"))
			(OIdx++ == 0 ? RootOut0 : RootOut1) = P;

	UEdGraphPin* B1In = nullptr, *B2In = nullptr;
	for (UEdGraphPin* P : B1->Pins) if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == FName("exec")) { B1In=P; break; }
	for (UEdGraphPin* P : B2->Pins) if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == FName("exec")) { B2In=P; break; }

	if (RootOut0 && B1In) { RootOut0->LinkedTo.Add(B1In); B1In->LinkedTo.Add(RootOut0); }
	if (RootOut1 && B2In) { RootOut1->LinkedTo.Add(B2In); B2In->LinkedTo.Add(RootOut1); }

	FBALSettings S;
	S.GapY = 48.f;
	FBALGraphAnalyzer::FAnalysisResult R;
	SolveGraph(G, EBALPureDir::West, S, R);

	const FBALNode& PB1 = R.Proxies[B1];
	const FBALNode& PB2 = R.Proxies[B2];

	// The two branches must not overlap in Y (with margin)
	TestFalse(TEXT("B1 and B2 do not overlap"),
	          NodesOverlap(PB1, PB2, S.NodeMargin));

	// Both branches must be to the right of Root
	const float RootRight = R.Proxies[Root].OutPos.X + R.Proxies[Root].Size.X;
	TestTrue(TEXT("B1 is right of Root"), PB1.OutPos.X >= RootRight);
	TestTrue(TEXT("B2 is right of Root"), PB2.OutPos.X >= RootRight);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_PureGroupWest,
	"BlueprintAutoLayout.LayoutSolver.PureGroupWest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_PureGroupWest::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// P → Exec, pure dir = West
	UEdGraph* G = MakeGraph();
	UEdGraphNode* Exec = MakeNode(G, true, 1, 0, 600, 0);
	UEdGraphNode* Pure = MakeNode(G, false, 0, 1, 0,  0);
	WireData(Pure, 0, Exec, 0);

	FBALSettings S;
	FBALGraphAnalyzer::FAnalysisResult R;
	SolveGraph(G, EBALPureDir::West, S, R);

	const FVector2D& EP = R.Proxies[Exec].OutPos;
	const FVector2D& PP = R.Proxies[Pure].OutPos;

	// Pure must be to the LEFT of the Exec node
	TestTrue(TEXT("Pure.X < Exec.X (West)"),
	         PP.X < EP.X);

	// Pure right edge must be left of Exec left edge (no overlap on X)
	float PureRight = PP.X + R.Proxies[Pure].Size.X;
	TestTrue(TEXT("Pure right edge <= Exec left edge"),
	         PureRight <= EP.X + 0.1f);

	// Blueprint Assist keeps parameter trees below the execution pin baseline so
	// data wires do not run through the main white execution spine.
	TestTrue(TEXT("Pure parameter tree starts below the exec node top"),
	         PP.Y > EP.Y);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_PureGroupEast,
	"BlueprintAutoLayout.LayoutSolver.PureGroupEast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_PureGroupEast::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	UEdGraphNode* Exec = MakeNode(G, true, 1, 0, 0, 0);
	UEdGraphNode* Pure = MakeNode(G, false, 0, 1, 0, 0);
	WireData(Pure, 0, Exec, 0);

	FBALSettings S;
	FBALGraphAnalyzer::FAnalysisResult R;
	SolveGraph(G, EBALPureDir::East, S, R);

	const FBALNode& EN = R.Proxies[Exec];
	const FBALNode& PN = R.Proxies[Pure];

	// Pure must start to the right of the Exec node's right edge
	float ExecRight = EN.OutPos.X + EN.Size.X;
	TestTrue(TEXT("Pure.X >= Exec right edge (East)"),
	         PN.OutPos.X >= ExecRight - 0.1f);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_PureGroupNorth,
	"BlueprintAutoLayout.LayoutSolver.PureGroupNorth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_PureGroupNorth::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	UEdGraphNode* Exec = MakeNode(G, true, 1, 0, 0, 400);
	UEdGraphNode* Pure = MakeNode(G, false, 0, 1, 0, 700);
	WireData(Pure, 0, Exec, 0);

	FBALSettings S;
	FBALGraphAnalyzer::FAnalysisResult R;
	SolveGraph(G, EBALPureDir::North, S, R);

	const FBALNode& EN = R.Proxies[Exec];
	const FBALNode& PN = R.Proxies[Pure];

	// Pure bottom edge must be above Exec top edge
	float PureBottom = PN.OutPos.Y + PN.Size.Y;
	TestTrue(TEXT("Pure bottom < Exec top (North)"),
	         PureBottom <= EN.OutPos.Y + 0.1f);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_PureGroupSouth,
	"BlueprintAutoLayout.LayoutSolver.PureGroupSouth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_PureGroupSouth::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	UEdGraphNode* Exec = MakeNode(G, true, 1, 0, 0, 0);
	UEdGraphNode* Pure = MakeNode(G, false, 0, 1, 0, 0);
	WireData(Pure, 0, Exec, 0);

	FBALSettings S;
	FBALGraphAnalyzer::FAnalysisResult R;
	SolveGraph(G, EBALPureDir::South, S, R);

	const FBALNode& EN = R.Proxies[Exec];
	const FBALNode& PN = R.Proxies[Pure];

	float ExecBottom = EN.OutPos.Y + EN.Size.Y;
	TestTrue(TEXT("Pure.Y >= Exec bottom (South)"),
	         PN.OutPos.Y >= ExecBottom - 0.1f);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_PureDepthLayering,
	"BlueprintAutoLayout.LayoutSolver.PureDepthLayering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_PureDepthLayering::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// P2 -> P1 -> Exec (depth 0 = P1, depth 1 = P2).
	// Blueprint Assist-style helixing keeps consecutive shallow depths in one
	// bounded-height column instead of stretching the chain across the graph.
	UEdGraph* G = MakeGraph();
	UEdGraphNode* Exec = MakeNode(G, true,  1, 0, 600, 0);
	UEdGraphNode* P1   = MakeNode(G, false, 1, 1, 300, 0);
	UEdGraphNode* P2   = MakeNode(G, false, 0, 1, 0,   0);
	WireData(P1, 0, Exec, 0);
	WireData(P2, 0, P1,   0);

	FBALSettings S;
	FBALGraphAnalyzer::FAnalysisResult R;
	SolveGraph(G, EBALPureDir::West, S, R);

	const FBALNode& EP  = R.Proxies[Exec];
	const FBALNode& PP1 = R.Proxies[P1];
	const FBALNode& PP2 = R.Proxies[P2];

	// P1 (depth 0) is directly left of Exec
	TestTrue(TEXT("P1 left of Exec"), PP1.OutPos.X < EP.OutPos.X);

	TestTrue(TEXT("P2 shares P1 parameter column"),
		FMath::IsNearlyEqual(PP2.OutPos.X, PP1.OutPos.X, 0.01f));
	TestTrue(TEXT("P2 is stacked below P1"),
		PP2.OutPos.Y >= PP1.OutPos.Y + PP1.Size.Y + S.PureGapY - 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_SharedPureLowerLane,
	"BlueprintAutoLayout.LayoutSolver.SharedPureLowerLane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_SharedPureLowerLane::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	UEdGraphNode* ExecA = MakeNode(G, true, 2, 0, 0, 0);
	UEdGraphNode* ExecB = MakeNode(G, true, 2, 0, 600, 0);
	UEdGraphNode* Shared = MakeNode(G, false, 0, 2, 200, 0);
	UEdGraphNode* PrivateA = MakeNode(G, false, 0, 1, 100, 200);
	UEdGraphNode* PrivateB = MakeNode(G, false, 0, 1, 500, 200);
	WireExec(ExecA, ExecB);
	WireData(Shared, 0, ExecA, 0);
	WireData(Shared, 1, ExecB, 0);
	WireData(PrivateA, 0, ExecA, 1);
	WireData(PrivateB, 0, ExecB, 1);

	FBALSettings S;
	FBALGraphAnalyzer::FAnalysisResult R;
	SolveGraph(G, EBALPureDir::West, S, R);

	const FBALNode& SharedNode = R.Proxies[Shared];
	float LocalBottom = -MAX_flt;
	for (UEdGraphNode* Local : { ExecA, ExecB, PrivateA, PrivateB })
	{
		const FBALNode& Node = R.Proxies[Local];
		LocalBottom = FMath::Max(LocalBottom, Node.OutPos.Y + Node.Size.Y);
	}
	const float LaneGap = FMath::Max(S.SharedPureLaneGap, S.GapY);
	TestTrue(TEXT("Shared Pure subtree starts below all local parameter blocks"),
		SharedNode.OutPos.Y >= LocalBottom + LaneGap - 0.01f);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_IsolatedPacking,
	"BlueprintAutoLayout.LayoutSolver.IsolatedPacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_IsolatedPacking::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Graph: one exec chain + 3 isolated nodes
	UEdGraph* G = MakeGraph();
	UEdGraphNode* A = MakeNode(G, true, 0, 0, 0, 0);
	UEdGraphNode* B = MakeNode(G, true, 0, 0, 300, 0);
	WireExec(A, B);

	// 3 isolated (no connections, no exec pins)
	UEdGraphNode* I1 = MakeNode(G, false, 0, 0, 9999, 9999);
	UEdGraphNode* I2 = MakeNode(G, false, 0, 0, 9999, 9999);
	UEdGraphNode* I3 = MakeNode(G, false, 0, 0, 9999, 9999);

	FBALSettings S;
	FBALGraphAnalyzer::FAnalysisResult R;
	SolveGraph(G, EBALPureDir::West, S, R);

	const FBALNode& PI1 = R.Proxies[I1];
	const FBALNode& PI2 = R.Proxies[I2];
	const FBALNode& PI3 = R.Proxies[I3];

	// Isolated nodes must not overlap each other
	TestFalse(TEXT("I1 not overlap I2"), NodesOverlap(PI1, PI2, S.NodeMargin));
	TestFalse(TEXT("I1 not overlap I3"), NodesOverlap(PI1, PI3, S.NodeMargin));
	TestFalse(TEXT("I2 not overlap I3"), NodesOverlap(PI2, PI3, S.NodeMargin));

	// All isolated nodes should be to the right and/or below the exec chain
	float ExecMaxX = FMath::Max(R.Proxies[A].OutPos.X, R.Proxies[B].OutPos.X + R.Proxies[B].Size.X);
	float ExecMaxY = FMath::Max(R.Proxies[A].OutPos.Y + R.Proxies[A].Size.Y,
	                             R.Proxies[B].OutPos.Y + R.Proxies[B].Size.Y);

	for (UEdGraphNode* Iso : {I1, I2, I3})
	{
		const FBALNode& PN = R.Proxies[Iso];
		bool bRightOrBelow = (PN.OutPos.X >= ExecMaxX) || (PN.OutPos.Y >= ExecMaxY);
		TestTrue(TEXT("Isolated is right-below exec graph"), bRightOrBelow);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_LockedExecNotMoved,
	"BlueprintAutoLayout.LayoutSolver.LockedExecNotMoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_LockedExecNotMoved::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Locked exec node at a specific position — solver must not move it.
	UEdGraph* G = MakeGraph();
	UEdGraphNode* Locked = MakeNode(G, true, 0, 0, 1234, 567);
	UEdGraphNode* Free   = MakeNode(G, true, 0, 0, 0,    0);
	WireExec(Locked, Free);

	// Pin Locked
	FBALConstraintCollector::SetPinned(Locked, true);

	FBALSettings S;
	TArray<FBALConstraint> Constraints;
	FBALConstraintCollector::Collect(G, Constraints);

	FBALGraphAnalyzer::FAnalysisResult R =
	    FBALGraphAnalyzer::Analyze(G, S, Constraints);

	FBALLayoutSolver::FSolverInput Input;
	Input.Proxies       = &R.Proxies;
	Input.ExecRoots     = &R.ExecRoots;
	Input.Edges         = &R.Edges;
	Input.Components    = &R.Components;
	Input.IsolatedPures = &R.IsolatedPures;
	Input.IsolatedNodes = &R.IsolatedNodes;
	Input.Constraints   = &Constraints;
	Input.PureDir       = EBALPureDir::West;
	Input.Settings      = &S;
	FBALLayoutSolver::Solve(Input);

	const FVector2D& LockedOut = R.Proxies[Locked].OutPos;
	TestEqual(TEXT("Locked X unchanged"), LockedOut.X,
		static_cast<decltype(LockedOut.X)>(1234));
	TestEqual(TEXT("Locked Y unchanged"), LockedOut.Y,
		static_cast<decltype(LockedOut.Y)>(567));

	return true;
}
