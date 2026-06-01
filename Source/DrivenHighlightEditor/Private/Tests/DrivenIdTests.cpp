// DrivenIdTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Pure unit tests for DrivenId encode / decode. No editor / Slate / Engine
// state is required.

#include "DrivenId.h"
#include "DrivenHighlightTypes.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DrivenHighlightTests
{
	static UEdGraph* MakeGraph()
	{
		return NewObject<UEdGraph>(GetTransientPackage(), NAME_None,
			RF_Transient | RF_NoFlags);
	}

	static UEdGraphNode* MakeNode(UEdGraph* Graph, const FGuid& Guid)
	{
		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NAME_None,
			RF_Transient | RF_NoFlags);
		Node->NodeGuid = Guid;
		Graph->Nodes.Add(Node);
		return Node;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bare node id roundtrip (no change-type qualifier).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrivenIdNodeBareRoundtrip,
	"DrivenHighlight.DrivenId.NodeBareRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDrivenIdNodeBareRoundtrip::RunTest(const FString&)
{
	const FGuid G = FGuid::NewGuid();
	const FName Id = DrivenId::MakeNode(G);
	TestFalse(TEXT("non-empty"), Id.IsNone());

	DrivenId::FParts Parts;
	TestTrue(TEXT("parse ok"), DrivenId::Parse(Id, Parts));
	TestEqual(TEXT("kind"), (int32)Parts.Kind, (int32)DrivenId::EKind::Node);
	TestEqual(TEXT("guid"), Parts.NodeGuid, G);
	TestFalse(TEXT("no change-type"), Parts.bHasChangeType);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Node id with change-type roundtrip.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrivenIdNodeWithChangeRoundtrip,
	"DrivenHighlight.DrivenId.NodeWithChangeRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDrivenIdNodeWithChangeRoundtrip::RunTest(const FString&)
{
	const FGuid G = FGuid::NewGuid();
	for (EDrivenChangeType T : { EDrivenChangeType::Added,
	                             EDrivenChangeType::Modified,
	                             EDrivenChangeType::Removed })
	{
		const FName Id = DrivenId::MakeNode(G, T);
		DrivenId::FParts Parts;
		TestTrue(TEXT("parse ok"), DrivenId::Parse(Id, Parts));
		TestTrue(TEXT("has change-type"), Parts.bHasChangeType);
		TestEqual(TEXT("change-type"), (int32)Parts.ChangeType, (int32)T);
		TestEqual(TEXT("kind"), (int32)Parts.Kind, (int32)DrivenId::EKind::Node);
		TestEqual(TEXT("guid"), Parts.NodeGuid, G);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Node-property id roundtrip + DetailRowItem tag derivation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrivenIdNodePropRoundtrip,
	"DrivenHighlight.DrivenId.NodePropRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDrivenIdNodePropRoundtrip::RunTest(const FString&)
{
	UEdGraph* G = DrivenHighlightTests::MakeGraph();
	const FGuid Guid = FGuid::NewGuid();
	UEdGraphNode* N = DrivenHighlightTests::MakeNode(G, Guid);

	const FName Prop(TEXT("MoveSpeed"));
	const FName Id = DrivenId::MakeNodeProp(N, Prop);

	DrivenId::FParts Parts;
	TestTrue(TEXT("parse ok"), DrivenId::Parse(Id, Parts));
	TestEqual(TEXT("kind"), (int32)Parts.Kind, (int32)DrivenId::EKind::NodeProp);
	TestEqual(TEXT("guid"), Parts.NodeGuid, Guid);
	TestEqual(TEXT("prop"), Parts.PropertyDisplayName, Prop);

	// Derived widget tag
	const FName Tag = DrivenId::GetDetailRowTagForPropertyId(Id);
	TestEqual(TEXT("derived tag"), Tag, FName(TEXT("DetailRowItem.MoveSpeed")));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  AssetProp id roundtrip.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrivenIdAssetPropRoundtrip,
	"DrivenHighlight.DrivenId.AssetPropRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDrivenIdAssetPropRoundtrip::RunTest(const FString&)
{
	UObject* Asset = DrivenHighlightTests::MakeGraph(); // any UObject will do
	const FName Prop(TEXT("BaseColor"));
	const FName Id = DrivenId::MakeAssetProp(Asset, Prop);

	DrivenId::FParts Parts;
	TestTrue(TEXT("parse ok"), DrivenId::Parse(Id, Parts));
	TestEqual(TEXT("kind"), (int32)Parts.Kind, (int32)DrivenId::EKind::AssetProp);
	TestEqual(TEXT("prop"), Parts.PropertyDisplayName, Prop);
	TestEqual(TEXT("hash length"), Parts.AssetHash.Len(), 32);

	const FName Tag = DrivenId::GetDetailRowTagForPropertyId(Id);
	TestEqual(TEXT("derived tag"), Tag, FName(TEXT("DetailRowItem.BaseColor")));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Malformed inputs → Parse returns false.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrivenIdMalformedRejected,
	"DrivenHighlight.DrivenId.MalformedRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDrivenIdMalformedRejected::RunTest(const FString&)
{
	const TCHAR* Bads[] = {
		TEXT(""),
		TEXT("Driven"),
		TEXT("NotDriven.Node.deadbeef"),
		TEXT("Driven.Node."),                   // no guid
		TEXT("Driven.Node.zzzz"),               // bad guid
		TEXT("Driven.Prop."),                   // no guid + no prop
		TEXT("Driven.Prop.deadbeef.MyProp"),    // 8-char guid (need 32)
		TEXT("Driven.Prop.Asset"),              // no hash + no prop
	};
	for (const TCHAR* Bad : Bads)
	{
		DrivenId::FParts Parts;
		TestFalse(FString::Printf(TEXT("reject '%s'"), Bad),
			DrivenId::Parse(FName(Bad), Parts));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
