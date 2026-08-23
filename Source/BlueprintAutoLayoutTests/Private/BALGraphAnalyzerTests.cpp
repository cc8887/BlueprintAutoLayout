// BALGraphAnalyzerTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Covers: FBALGraphAnalyzer
//   - NodeClassification      (Exec / Pure / Isolated / Comment)
//   - ExecTreeBuilding        (linear chain, fork, isolated root)
//   - PureSubtreeAssignment   (ownership, PureDepth, PureSlot, multi-consumer)

#include "BALTestHelpers.h"
#include "BALGraphAnalyzer.h"
#include "Misc/AutomationTest.h"

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_NodeClassification,
	"BlueprintAutoLayout.GraphAnalyzer.NodeClassification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_NodeClassification::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* G = MakeGraph();
	// Exec node  (has exec pins + 1 data input so Pure can feed it)
	UEdGraphNode* Exec    = MakeNode(G, /*bWithExec=*/true,  /*DataIn=*/1, /*DataOut=*/0);
	// Pure node  (data only, connected)
	UEdGraphNode* Pure    = MakeNode(G, false, 0, 1);
	// Isolated   (no connections at all — placed far away, no wires)
	UEdGraphNode* Iso     = MakeNode(G, false, 0, 0, 500, 0);
	// Comment
	UEdGraphNode_Comment* Comment = MakeComment(G, 0, 0, 600, 400);

	// Connect Pure → Exec (data)
	WireData(Pure, 0, Exec, 0);

	FBALSettings S;
	FBALGraphAnalyzer::FAnalysisResult R =
	    FBALGraphAnalyzer::Analyze(G, S, {});

	// Four proxies expected
	TestEqual(TEXT("Proxy count"), R.Proxies.Num(), 4);

	TestEqual(TEXT("Exec role"),    R.Proxies[Exec].Role,    EBALNodeRole::Exec);
	TestEqual(TEXT("Pure role"),    R.Proxies[Pure].Role,    EBALNodeRole::Pure);
	TestEqual(TEXT("Isolated role"),R.Proxies[Iso].Role,     EBALNodeRole::Isolated);
	TestEqual(TEXT("Comment role"), R.Proxies[Comment].Role, EBALNodeRole::Comment);

	// IsolatedNodes and IsolatedPures populated correctly
	TestTrue(TEXT("Iso in IsolatedNodes"),   R.IsolatedNodes.Contains(&R.Proxies[Iso]));
	// Pure with an owner is NOT an isolated pure
	TestFalse(TEXT("Connected pure NOT in IsolatedPures"),
	          R.IsolatedPures.Contains(&R.Proxies[Pure]));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_ExecTree_Linear,
	"BlueprintAutoLayout.GraphAnalyzer.ExecTree.Linear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_ExecTree_Linear::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// A → B → C (linear exec chain)
	UEdGraph* G = MakeGraph();
	UEdGraphNode* A = MakeNode(G, true, 0, 0, 0,   0);
	UEdGraphNode* B = MakeNode(G, true, 0, 0, 300, 0);
	UEdGraphNode* C = MakeNode(G, true, 0, 0, 600, 0);
	WireExec(A, B);
	WireExec(B, C);

	FBALSettings S;
	auto R = FBALGraphAnalyzer::Analyze(G, S, {});

	// Exactly one exec root (A)
	TestEqual(TEXT("One exec root"), R.ExecRoots.Num(), 1);
	TestEqual(TEXT("Root is A"), R.ExecRoots[0]->Proxy->GraphNode, A);

	// Depths
	TestEqual(TEXT("A depth 0"), R.Proxies[A].ExecDepth, 0);
	TestEqual(TEXT("B depth 1"), R.Proxies[B].ExecDepth, 1);
	TestEqual(TEXT("C depth 2"), R.Proxies[C].ExecDepth, 2);

	// Tree structure
	TestEqual(TEXT("A has 1 child"),  R.ExecRoots[0]->Children.Num(), 1);
	TestEqual(TEXT("B has 1 child"),  R.ExecRoots[0]->Children[0]->Children.Num(), 1);
	TestEqual(TEXT("C is leaf"),      R.ExecRoots[0]->Children[0]->Children[0]->Children.Num(), 0);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_ExecTree_Fork,
	"BlueprintAutoLayout.GraphAnalyzer.ExecTree.Fork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_ExecTree_Fork::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Fork: Root → B1, Root → B2
	// Root needs two exec output pins.  Simplest: add a second exec out manually.
	UEdGraph* G = MakeGraph();
	UEdGraphNode* Root = MakeNode(G, true, 0, 0, 0,   0);
	UEdGraphNode* B1   = MakeNode(G, true, 0, 0, 300, 0);
	UEdGraphNode* B2   = MakeNode(G, true, 0, 0, 300, 200);

	// Add a second exec output to Root
	{
		FEdGraphPinType ExecType;
		ExecType.PinCategory = FName("exec");
		Root->CreatePin(EGPD_Output, ExecType, FName("ExecOut2"));
	}

	// Wire Root→B1 via first exec out, Root→B2 via second exec out
	// Wire helper uses index among Output pins; exec out is index 0 for B1
	// For B2, wire the second exec output (index 1 among outputs of Root)
	UEdGraphPin* RootOut0 = nullptr;
	UEdGraphPin* RootOut1 = nullptr;
	int32 OutIdx = 0;
	for (UEdGraphPin* P : Root->Pins)
	{
		if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == FName("exec"))
		{
			if (OutIdx == 0) RootOut0 = P;
			else             RootOut1 = P;
			++OutIdx;
		}
	}

	// Get B1's exec input
	UEdGraphPin* B1In = nullptr;
	for (UEdGraphPin* P : B1->Pins)
		if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == FName("exec"))
			{ B1In = P; break; }

	UEdGraphPin* B2In = nullptr;
	for (UEdGraphPin* P : B2->Pins)
		if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == FName("exec"))
			{ B2In = P; break; }

	if (RootOut0 && B1In) { RootOut0->LinkedTo.Add(B1In); B1In->LinkedTo.Add(RootOut0); }
	if (RootOut1 && B2In) { RootOut1->LinkedTo.Add(B2In); B2In->LinkedTo.Add(RootOut1); }

	FBALSettings S;
	auto R = FBALGraphAnalyzer::Analyze(G, S, {});

	TestEqual(TEXT("One exec root"), R.ExecRoots.Num(), 1);
	TestEqual(TEXT("Root is Root"), R.ExecRoots[0]->Proxy->GraphNode, Root);
	TestEqual(TEXT("Root has 2 children"), R.ExecRoots[0]->Children.Num(), 2);

	// B1 and B2 have no children
	for (FBALExecNode* Child : R.ExecRoots[0]->Children)
	{
		TestEqual(TEXT("Branch is leaf"), Child->Children.Num(), 0);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_PureSubtreeAssignment,
	"BlueprintAutoLayout.GraphAnalyzer.PureSubtreeAssignment.Chain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_PureSubtreeAssignment::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	//  P2 → P1 → Exec
	//             (P1 is depth 0 — direct input to Exec)
	//             (P2 is depth 1 — input to P1)
	UEdGraph* G = MakeGraph();
	UEdGraphNode* Exec = MakeNode(G, true,  1, 0, 600, 0);
	UEdGraphNode* P1   = MakeNode(G, false, 1, 1, 300, 0);  // data in+out
	UEdGraphNode* P2   = MakeNode(G, false, 0, 1, 0,   0);  // data out only

	WireData(P1, 0, Exec, 0);  // P1 output → Exec data input
	WireData(P2, 0, P1,   0);  // P2 output → P1 data input

	FBALSettings S;
	auto R = FBALGraphAnalyzer::Analyze(G, S, {});

	// Both P1 and P2 should be owned by Exec
	TestEqual(TEXT("P1 owner is Exec"), R.Proxies[P1].PureOwner, Exec);
	TestEqual(TEXT("P2 owner is Exec"), R.Proxies[P2].PureOwner, Exec);

	// PureDepth: P1 = 0, P2 = 1
	TestEqual(TEXT("P1 PureDepth 0"), R.Proxies[P1].PureDepth, 0);
	TestEqual(TEXT("P2 PureDepth 1"), R.Proxies[P2].PureDepth, 1);

	// ExecNode PureGroup should contain both
	TestEqual(TEXT("Exec PureGroup size 2"),
	          R.ExecRoots[0]->PureGroup.Num(), 2);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_PureMultiConsumer,
	"BlueprintAutoLayout.GraphAnalyzer.PureSubtreeAssignment.MultiConsumer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_PureMultiConsumer::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// Shared Pure: Pure → ExecA (depth 0), Pure → ExecB (depth 1)
	// Pure should be assigned to ExecA (shallower).
	UEdGraph* G = MakeGraph();
	UEdGraphNode* ExecA = MakeNode(G, true, 1, 0, 400,  0);
	UEdGraphNode* ExecB = MakeNode(G, true, 1, 0, 800,  0);
	UEdGraphNode* Pure  = MakeNode(G, false, 0, 2, 0,   0);  // 2 data outputs

	WireExec(ExecA, ExecB);
	WireData(Pure, 0, ExecA, 0);
	WireData(Pure, 1, ExecB, 0);

	FBALSettings S;
	auto R = FBALGraphAnalyzer::Analyze(G, S, {});

	// ExecA has depth 0, ExecB has depth 1 → Pure should go to ExecA
	TestEqual(TEXT("Pure assigned to ExecA (shallower)"),
	          R.Proxies[Pure].PureOwner, ExecA);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_IsolatedPure,
	"BlueprintAutoLayout.GraphAnalyzer.IsolatedPure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_IsolatedPure::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	// A pure node that feeds no exec node → goes to IsolatedPures
	UEdGraph* G  = MakeGraph();
	UEdGraphNode* P1 = MakeNode(G, false, 0, 1, 0, 0);
	UEdGraphNode* P2 = MakeNode(G, false, 1, 1, 200, 0);
	// Wire P1→P2 (both pure, neither feeds an exec)
	WireData(P1, 0, P2, 0);

	FBALSettings S;
	auto R = FBALGraphAnalyzer::Analyze(G, S, {});

	// No exec roots
	TestEqual(TEXT("No exec roots"), R.ExecRoots.Num(), 0);

	// Both end up in IsolatedPures
	TestEqual(TEXT("IsolatedPures count 2"), R.IsolatedPures.Num(), 2);
	TestEqual(TEXT("P1 owner is nullptr"), R.Proxies[P1].PureOwner, (UEdGraphNode*)nullptr);
	TestEqual(TEXT("P2 owner is nullptr"), R.Proxies[P2].PureOwner, (UEdGraphNode*)nullptr);

	return true;
}
