// HighlightServiceTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Logical-hit tests for IDrivenHighlightService / IDrivenHighlightInspector.
// Per the design contract these verify "Show*() registers a driven id and
// IsActive returns true" — they intentionally do not exercise the Slate
// overlay's pulsing draw path (covered separately by manual smoke tests).

#include "IDrivenHighlightService.h"
#include "IDrivenHighlightInspector.h"
#include "DrivenId.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DrivenHighlightTestsService
{
	static UEdGraphNode* MakeNode(const FGuid& Guid)
	{
		UEdGraph* Graph = NewObject<UEdGraph>(GetTransientPackage(), NAME_None,
			RF_Transient | RF_NoFlags);
		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NAME_None,
			RF_Transient | RF_NoFlags);
		Node->NodeGuid = Guid;
		Graph->Nodes.Add(Node);
		return Node;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Node highlight: ShowNode → IsActive(true) → Hide → IsActive(false).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHighlightServiceShowNodeLogical,
	"DrivenHighlight.Service.ShowNodeLogical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FHighlightServiceShowNodeLogical::RunTest(const FString&)
{
	if (!IDrivenHighlightService::IsAvailable())
	{
		AddWarning(TEXT("DrivenHighlightEditor module not loaded; skipping."));
		return true;
	}

	const FGuid Guid = FGuid::NewGuid();
	UEdGraphNode* Node = DrivenHighlightTestsService::MakeNode(Guid);

	IDrivenHighlightService& Svc = IDrivenHighlightService::Get();
	IDrivenHighlightInspector& Insp = IDrivenHighlightInspector::Get();

	// Clean slate.
	Svc.HideAll();
	TestFalse(TEXT("not active before show"), Insp.IsActive(DrivenId::MakeNode(Node)));

	const FName Id = Svc.ShowNode(Node, EDrivenChangeType::Modified);
	TestFalse(TEXT("returned id non-empty"), Id.IsNone());
	TestTrue(TEXT("active after show"),  Insp.IsActive(Id));

	// Hide → no longer active.
	Svc.Hide(Id);
	TestFalse(TEXT("inactive after hide"), Insp.IsActive(Id));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Property highlight: derived widget tag matches DetailRowItem.<Prop>.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHighlightServiceShowPropertyLogical,
	"DrivenHighlight.Service.ShowPropertyLogical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FHighlightServiceShowPropertyLogical::RunTest(const FString&)
{
	if (!IDrivenHighlightService::IsAvailable())
	{
		AddWarning(TEXT("DrivenHighlightEditor module not loaded; skipping."));
		return true;
	}

	UEdGraphNode* Node = DrivenHighlightTestsService::MakeNode(FGuid::NewGuid());
	const FName Prop(TEXT("MoveSpeed"));

	IDrivenHighlightService& Svc = IDrivenHighlightService::Get();
	IDrivenHighlightInspector& Insp = IDrivenHighlightInspector::Get();

	Svc.HideAll();
	const FName Id = Svc.ShowNodeProperty(Node, Prop);

	TestFalse(TEXT("non-empty id"), Id.IsNone());
	TestTrue(TEXT("is active"), Insp.IsActive(Id));
	TestEqual(TEXT("derived tag matches DetailRowItem.<Prop>"),
		DrivenId::GetDetailRowTagForPropertyId(Id),
		FName(TEXT("DetailRowItem.MoveSpeed")));

	Svc.HideAll();
	TestFalse(TEXT("inactive after hide-all"), Insp.IsActive(Id));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Inspector enumeration: GetAllActive contains every Show*() id.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FHighlightInspectorEnumeration,
	"DrivenHighlight.Service.InspectorEnumeration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FHighlightInspectorEnumeration::RunTest(const FString&)
{
	if (!IDrivenHighlightService::IsAvailable())
	{
		AddWarning(TEXT("DrivenHighlightEditor module not loaded; skipping."));
		return true;
	}

	UEdGraphNode* N1 = DrivenHighlightTestsService::MakeNode(FGuid::NewGuid());
	UEdGraphNode* N2 = DrivenHighlightTestsService::MakeNode(FGuid::NewGuid());

	IDrivenHighlightService& Svc = IDrivenHighlightService::Get();
	IDrivenHighlightInspector& Insp = IDrivenHighlightInspector::Get();

	Svc.HideAll();
	const FName A = Svc.ShowNode(N1);
	const FName B = Svc.ShowNodeProperty(N2, FName(TEXT("Color")));

	const TArray<FName> Active = Insp.GetAllActive();
	TestTrue(TEXT("contains A"), Active.Contains(A));
	TestTrue(TEXT("contains B"), Active.Contains(B));
	TestEqual(TEXT("count"), Active.Num(), 2);

	Svc.HideAll();
	TestEqual(TEXT("empty after HideAll"), Insp.GetAllActive().Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
