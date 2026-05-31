// BALCollisionResolverTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Covers: FBALCollisionResolver
//   - TwoFreeNodesOverlap        Two free nodes overlap → pushed apart
//   - LockedVsFreeOverlap        Locked node never moves; free node absorbs full push
//   - TwoLockedNodesOverlap      Both locked → neither moves (unsolvable, no crash)
//   - NoOverlapUnchanged         Non-overlapping nodes → positions unchanged
//   - CompactionTowardIdeal      After push, compaction pulls node back toward ideal

#include "BALTestHelpers.h"
#include "BALCollisionResolver.h"
#include "Misc/AutomationTest.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: run the resolver on a raw proxy map (no UEdGraph needed)
// ─────────────────────────────────────────────────────────────────────────────
static void RunResolver(TMap<UEdGraphNode*, FBALNode>& Proxies,
                         const FBALSettings& S)
{
	FBALCollisionResolver::FResolveInput Input;
	Input.Proxies     = &Proxies;
	Input.Settings    = &S;
	TArray<FBALConstraint> NoCon;
	Input.Constraints = &NoCon;
	FBALCollisionResolver::Resolve(Input);
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_TwoFreeNodesOverlap,
	"BlueprintAutoLayout.CollisionResolver.TwoFreeNodesOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_TwoFreeNodesOverlap::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Two 200×80 nodes placed at the exact same position — severe overlap.
	UEdGraph* G = MakeGraph();
	UEdGraphNode* NA = MakeNode(G, false, 0, 0);
	UEdGraphNode* NB = MakeNode(G, false, 0, 0);

	TMap<UEdGraphNode*, FBALNode> Proxies;
	Proxies.Add(NA, MakeProxy(NA, EBALNodeRole::Exec,
	                          FVector2D(200.f, 80.f), FVector2D(0.f,  0.f)));
	Proxies.Add(NB, MakeProxy(NB, EBALNodeRole::Exec,
	                          FVector2D(200.f, 80.f), FVector2D(0.f,  0.f)));

	FBALSettings S;
	S.NodeMargin  = 8.f;
	S.CollisionIter = 30;
	RunResolver(Proxies, S);

	TestFalse(TEXT("No overlap after resolve"),
	          NodesOverlap(Proxies[NA], Proxies[NB], S.NodeMargin));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_LockedVsFreeOverlap,
	"BlueprintAutoLayout.CollisionResolver.LockedVsFreeOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_LockedVsFreeOverlap::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	UEdGraphNode* NLocked = MakeNode(G, false, 0, 0, 0, 0);
	UEdGraphNode* NFree   = MakeNode(G, false, 0, 0, 50, 0); // overlapping

	const FVector2D LockedStartPos(0.f, 0.f);

	TMap<UEdGraphNode*, FBALNode> Proxies;
	Proxies.Add(NLocked, MakeProxy(NLocked, EBALNodeRole::Exec,
	                               FVector2D(200.f, 80.f), LockedStartPos, /*bLocked=*/true));
	Proxies.Add(NFree,   MakeProxy(NFree,   EBALNodeRole::Exec,
	                               FVector2D(200.f, 80.f), FVector2D(50.f, 0.f)));

	FBALSettings S;
	RunResolver(Proxies, S);

	// Locked node must not have moved
	TestEqual(TEXT("Locked X unchanged"), Proxies[NLocked].OutPos.X, LockedStartPos.X);
	TestEqual(TEXT("Locked Y unchanged"), Proxies[NLocked].OutPos.Y, LockedStartPos.Y);

	// Free node must have been pushed away
	TestFalse(TEXT("No overlap after resolve"),
	          NodesOverlap(Proxies[NLocked], Proxies[NFree], S.NodeMargin));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_TwoLockedNodesOverlap,
	"BlueprintAutoLayout.CollisionResolver.TwoLockedNodesOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_TwoLockedNodesOverlap::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Both locked — resolver must not crash and must not move either node.
	UEdGraph* G = MakeGraph();
	UEdGraphNode* NA = MakeNode(G, false, 0, 0);
	UEdGraphNode* NB = MakeNode(G, false, 0, 0);

	TMap<UEdGraphNode*, FBALNode> Proxies;
	Proxies.Add(NA, MakeProxy(NA, EBALNodeRole::Exec, FVector2D(200.f,80.f), FVector2D(0.f,0.f), true));
	Proxies.Add(NB, MakeProxy(NB, EBALNodeRole::Exec, FVector2D(200.f,80.f), FVector2D(0.f,0.f), true));

	FBALSettings S;
	RunResolver(Proxies, S);  // must not crash

	TestEqual(TEXT("Locked A X unchanged"), Proxies[NA].OutPos.X, 0.0);
	TestEqual(TEXT("Locked B X unchanged"), Proxies[NB].OutPos.X, 0.0);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_NoOverlapUnchanged,
	"BlueprintAutoLayout.CollisionResolver.NoOverlapUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_NoOverlapUnchanged::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	UEdGraphNode* NA = MakeNode(G, false, 0, 0);
	UEdGraphNode* NB = MakeNode(G, false, 0, 0);

	// Well-separated nodes (1000px apart)
	const FVector2D PosA(0.f,    0.f);
	const FVector2D PosB(1000.f, 0.f);

	TMap<UEdGraphNode*, FBALNode> Proxies;
	Proxies.Add(NA, MakeProxy(NA, EBALNodeRole::Exec, FVector2D(200.f,80.f), PosA));
	Proxies.Add(NB, MakeProxy(NB, EBALNodeRole::Exec, FVector2D(200.f,80.f), PosB));

	FBALSettings S;
	RunResolver(Proxies, S);

	// Positions must be essentially unchanged (allow only compaction drift < 2px)
	const float Tol = 2.f;
	TestTrue(TEXT("A X barely moved"), FMath::Abs(Proxies[NA].OutPos.X - PosA.X) <= Tol);
	TestTrue(TEXT("A Y barely moved"), FMath::Abs(Proxies[NA].OutPos.Y - PosA.Y) <= Tol);
	TestTrue(TEXT("B X barely moved"), FMath::Abs(Proxies[NB].OutPos.X - PosB.X) <= Tol);
	TestTrue(TEXT("B Y barely moved"), FMath::Abs(Proxies[NB].OutPos.Y - PosB.Y) <= Tol);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests that the resolver leaves a well-separated multi-node layout intact,
// and also correctly separates a pair of overlapping nodes inside that layout.
//
// 5 nodes in a column, already non-overlapping (spacing > height),
// PLUS one extra node placed on top of the middle node to create a known overlap.
// After resolve the known overlap must be resolved; the others must remain separated.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_ManyNodesNoOverlap,
	"BlueprintAutoLayout.CollisionResolver.ManyNodesNoOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_ManyNodesNoOverlap::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// 5 nodes placed 120px apart (height=80, so 40px gap between each pair).
	// Plus NExtra placed exactly on top of N2 — creates a known, single overlap.
	// NodeMargin=0 so adjacent well-separated nodes do NOT trigger push.
	// After resolve: NExtra and N2 must no longer overlap; no new overlaps allowed.
	UEdGraph* G = MakeGraph();

	TMap<UEdGraphNode*, FBALNode> Proxies;
	TArray<UEdGraphNode*> Nodes;
	for (int32 i = 0; i < 5; ++i)
	{
		UEdGraphNode* N = MakeNode(G, false, 0, 0);
		Nodes.Add(N);
		Proxies.Add(N, MakeProxy(N, EBALNodeRole::Exec, FVector2D(200.f, 80.f),
		                          FVector2D(0.f, (float)i * 120.f)));
	}

	// NExtra exactly overlaps N2 (at Y=240)
	UEdGraphNode* NExtra = MakeNode(G, false, 0, 0);
	Proxies.Add(NExtra, MakeProxy(NExtra, EBALNodeRole::Exec, FVector2D(200.f, 80.f),
	                               FVector2D(0.f, 240.f)));

	FBALSettings S;
	S.NodeMargin    = 0.f;
	S.CollisionIter = 60;
	RunResolver(Proxies, S);

	// Debug: print positions
	for (int32 i = 0; i < Nodes.Num(); ++i)
		AddInfo(FString::Printf(TEXT("N%d: (%.1f, %.1f)"), i, Proxies[Nodes[i]].OutPos.X, Proxies[Nodes[i]].OutPos.Y));
	AddInfo(FString::Printf(TEXT("NExtra: (%.1f, %.1f)"), Proxies[NExtra].OutPos.X, Proxies[NExtra].OutPos.Y));

	// After resolve: no pairwise overlaps
	TArray<UEdGraphNode*> All = Nodes;
	All.Add(NExtra);
	bool bAnyOverlap = false;
	for (int32 i = 0; i < All.Num() && !bAnyOverlap; ++i)
	for (int32 j = i+1; j < All.Num() && !bAnyOverlap; ++j)
	{
		if (NodesOverlap(Proxies[All[i]], Proxies[All[j]], 0.f))
		{
			AddInfo(FString::Printf(TEXT("OVERLAP: [%d] (%.1f,%.1f) vs [%d] (%.1f,%.1f)"),
			    i, Proxies[All[i]].OutPos.X, Proxies[All[i]].OutPos.Y,
			    j, Proxies[All[j]].OutPos.X, Proxies[All[j]].OutPos.Y));
			bAnyOverlap = true;
		}
	}
	TestFalse(TEXT("No pairwise overlaps after 6-node resolve"), bAnyOverlap);

	return true;
}
