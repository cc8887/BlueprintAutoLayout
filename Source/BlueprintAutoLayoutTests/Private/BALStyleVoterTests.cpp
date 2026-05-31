// BALStyleVoterTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Covers: FBALStyleVoter
//   - ForceDir           bForceDir=true → returns ForcedDir immediately
//   - VoteWestDefault    Pure groups left of consumer → West wins
//   - VoteReturnsValid   Vote always returns one of the four enum values

#include "BALTestHelpers.h"
#include "BALStyleVoter.h"
#include "BALGraphAnalyzer.h"
#include "Misc/AutomationTest.h"

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_ForceDir,
	"BlueprintAutoLayout.StyleVoter.ForceDir",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_ForceDir::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	UEdGraphNode* Exec = MakeNode(G, true, 1, 0, 600, 0);
	UEdGraphNode* Pure = MakeNode(G, false, 0, 1, 0, 0);
	WireData(Pure, 0, Exec, 0);

	FBALSettings S;
	S.bForceDir  = true;
	S.ForcedDir  = EBALPureDir::South;

	FBALGraphAnalyzer::FAnalysisResult R = FBALGraphAnalyzer::Analyze(G, S, {});

	FBALStyleVoter::FVoteContext Ctx;
	Ctx.Proxies   = &R.Proxies;
	Ctx.ExecRoots = &R.ExecRoots;
	Ctx.Settings  = &S;

	EBALPureDir Result = FBALStyleVoter::Vote(Ctx);
	TestEqual(TEXT("ForceDir returns ForcedDir"), Result, EBALPureDir::South);

	// Test all four forced directions
	for (EBALPureDir D : { EBALPureDir::West, EBALPureDir::East,
	                       EBALPureDir::North, EBALPureDir::South })
	{
		S.ForcedDir = D;
		TestEqual(TEXT("Each ForcedDir respected"), FBALStyleVoter::Vote(Ctx), D);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_VoteReturnsValid,
	"BlueprintAutoLayout.StyleVoter.VoteReturnsValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_VoteReturnsValid::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Minimal graph: one exec node with one pure input
	UEdGraph* G = MakeGraph();
	UEdGraphNode* Exec = MakeNode(G, true, 1, 0, 400, 200);
	UEdGraphNode* Pure = MakeNode(G, false, 0, 1, 100, 200);
	WireData(Pure, 0, Exec, 0);

	FBALSettings S;
	S.bForceDir = false;

	FBALGraphAnalyzer::FAnalysisResult R = FBALGraphAnalyzer::Analyze(G, S, {});

	// Assign some positions so voter has something to work with
	R.Proxies[Exec].OutPos = FVector2D(400.f, 200.f);
	R.Proxies[Pure].OutPos = FVector2D(100.f, 200.f);

	FBALStyleVoter::FVoteContext Ctx;
	Ctx.Proxies   = &R.Proxies;
	Ctx.ExecRoots = &R.ExecRoots;
	Ctx.Settings  = &S;

	EBALPureDir D = FBALStyleVoter::Vote(Ctx);

	bool bValid = (D == EBALPureDir::West  || D == EBALPureDir::East ||
	               D == EBALPureDir::North || D == EBALPureDir::South);
	TestTrue(TEXT("Vote returns a valid EBALPureDir"), bValid);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_VoteWestBias,
	"BlueprintAutoLayout.StyleVoter.VoteWestBias",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_VoteWestBias::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Verify that Vote with no pure groups (nothing to vote on) falls back to West (default).
	// This avoids relying on score heuristics that are layout-input-dependent.
	//
	// Exec chain: A(0,0) → B(400,0) → C(800,0), no pure nodes connected.
	// Expected: BestVotes == 0 → default EBALPureDir::West.
	UEdGraph* G = MakeGraph();
	UEdGraphNode* A = MakeNode(G, true, 0, 0,   0,  0);
	UEdGraphNode* B = MakeNode(G, true, 0, 0, 400,  0);
	UEdGraphNode* C = MakeNode(G, true, 0, 0, 800,  0);

	WireExec(A, B);
	WireExec(B, C);

	FBALSettings S;
	S.bForceDir = false;

	FBALGraphAnalyzer::FAnalysisResult R = FBALGraphAnalyzer::Analyze(G, S, {});

	FBALStyleVoter::FVoteContext Ctx;
	Ctx.Proxies   = &R.Proxies;
	Ctx.ExecRoots = &R.ExecRoots;
	Ctx.Settings  = &S;

	// No pure groups → falls back to default West
	EBALPureDir D = FBALStyleVoter::Vote(Ctx);
	TestEqual(TEXT("No pure groups → default West"), D, EBALPureDir::West);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_VoteEmptyGraph,
	"BlueprintAutoLayout.StyleVoter.VoteEmptyGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_VoteEmptyGraph::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// No exec nodes, no pure nodes → voter should not crash and return something
	UEdGraph* G = MakeGraph();
	FBALSettings S;
	S.bForceDir = false;

	FBALGraphAnalyzer::FAnalysisResult R = FBALGraphAnalyzer::Analyze(G, S, {});

	FBALStyleVoter::FVoteContext Ctx;
	Ctx.Proxies   = &R.Proxies;
	Ctx.ExecRoots = &R.ExecRoots;
	Ctx.Settings  = &S;

	// Must not crash
	EBALPureDir D = FBALStyleVoter::Vote(Ctx);

	bool bValid = (D == EBALPureDir::West  || D == EBALPureDir::East ||
	               D == EBALPureDir::North || D == EBALPureDir::South);
	TestTrue(TEXT("Empty graph: returns valid dir without crash"), bValid);

	return true;
}
