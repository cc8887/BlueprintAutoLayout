// BALAdvancedRegressionTests.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Constraint, comment hierarchy, collision direction, and full-pipeline
// idempotence regressions for the advanced layout pipeline.

#include "BALTestHelpers.h"
#include "BALCollisionResolver.h"
#include "BALCommentHandler.h"
#include "BALCommitter.h"
#include "BALGraphAnalyzer.h"
#include "BlueprintAutoLayoutEngine.h"
#include "Misc/AutomationTest.h"

namespace
{
	void ResolveProxies(TMap<UEdGraphNode*, FBALNode>& Proxies,
	                    const FBALSettings& Settings)
	{
		TArray<FBALConstraint> Constraints;
		FBALCollisionResolver::FResolveInput Input;
		Input.Proxies = &Proxies;
		Input.Constraints = &Constraints;
		Input.Settings = &Settings;
		FBALCollisionResolver::Resolve(Input);
	}

	const FBALCommentGroup* FindCommentGroup(
		const TArray<FBALCommentGroup>& Groups,
		const UEdGraphNode* Comment)
	{
		return Groups.FindByPredicate(
			[Comment](const FBALCommentGroup& Group)
			{
				return Group.Comment == Comment;
			});
	}

	bool ContainsBounds(const FBALNode& Container, const FBALNode& Member)
	{
		return Member.OutPos.X >= Container.OutPos.X
			&& Member.OutPos.Y >= Container.OutPos.Y
			&& Member.OutPos.X + Member.Size.X <= Container.OutPos.X + Container.Size.X
			&& Member.OutPos.Y + Member.Size.Y <= Container.OutPos.Y + Container.Size.Y;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_SoftMaxDrift,
	"BlueprintAutoLayout.AdvancedRegression.Constraints.SoftMaxDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_SoftMaxDrift::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* HardNode = MakeNode(Graph, false, 0, 0);
	UEdGraphNode* SoftNode = MakeNode(Graph, false, 0, 0);

	const FVector2D HardStart(0.f, 0.f);
	const FVector2D SoftStart(80.f, 0.f);
	const float MaxDrift = 12.f;
	TMap<UEdGraphNode*, FBALNode> Proxies;
	FBALNode HardProxy = MakeProxy(HardNode, EBALNodeRole::Exec,
		FVector2D(100.f, 300.f), HardStart, true);
	HardProxy.StableIndex = 0;
	HardProxy.bConstrained = true;
	HardProxy.ConstraintType = EBALConstraintType::Hard;
	FBALNode SoftProxy = MakeProxy(SoftNode, EBALNodeRole::Exec,
		FVector2D(100.f, 300.f), SoftStart, false);
	SoftProxy.StableIndex = 1;
	SoftProxy.bConstrained = true;
	SoftProxy.ConstraintType = EBALConstraintType::Soft;
	SoftProxy.MaxDrift = MaxDrift;
	Proxies.Add(HardNode, HardProxy);
	Proxies.Add(SoftNode, SoftProxy);

	FBALSettings Settings;
	Settings.NodeMargin = 0.f;
	Settings.CollisionIter = 8;
	ResolveProxies(Proxies, Settings);

	const FVector2D SoftDelta = Proxies.FindChecked(SoftNode).OutPos - SoftStart;
	TestTrue(TEXT("Soft node moves in response to the collision"),
		SoftDelta.Size() > KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Soft node never exceeds MaxDrift"),
		SoftDelta.Size() <= MaxDrift + 0.01f);
	TestTrue(TEXT("Insufficient drift is clamped at the configured radius"),
		FMath::IsNearlyEqual(SoftDelta.Size(), MaxDrift, 0.01f));
	TestTrue(TEXT("Soft node is pushed away from the hard node"), SoftDelta.X > 0.f);
	TestTrue(TEXT("Hard obstacle remains unchanged"),
		Proxies.FindChecked(HardNode).OutPos.Equals(HardStart, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_HardCollisionDirection,
	"BlueprintAutoLayout.AdvancedRegression.Collision.HardCollisionDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_HardCollisionDirection::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* HardNode = MakeNode(Graph, false, 0, 0);
	UEdGraphNode* FreeNode = MakeNode(Graph, false, 0, 0);
	const FVector2D HardStart(0.f, 0.f);
	const FVector2D FreeStart(80.f, 0.f);

	TMap<UEdGraphNode*, FBALNode> Proxies;
	FBALNode HardProxy = MakeProxy(HardNode, EBALNodeRole::Exec,
		FVector2D(100.f, 300.f), HardStart, true);
	HardProxy.StableIndex = 0;
	HardProxy.bConstrained = true;
	HardProxy.ConstraintType = EBALConstraintType::Hard;
	FBALNode FreeProxy = MakeProxy(FreeNode, EBALNodeRole::Exec,
		FVector2D(100.f, 300.f), FreeStart, false);
	FreeProxy.StableIndex = 1;
	Proxies.Add(HardNode, HardProxy);
	Proxies.Add(FreeNode, FreeProxy);

	FBALSettings Settings;
	Settings.NodeMargin = 0.f;
	Settings.CollisionIter = 8;
	ResolveProxies(Proxies, Settings);

	const FBALNode& ResolvedHard = Proxies.FindChecked(HardNode);
	const FBALNode& ResolvedFree = Proxies.FindChecked(FreeNode);
	TestTrue(TEXT("Hard node position is immutable"),
		ResolvedHard.OutPos.Equals(HardStart, 0.01f));
	TestTrue(TEXT("Free node moves right, away from the hard obstacle"),
		ResolvedFree.OutPos.X > FreeStart.X);
	TestTrue(TEXT("Minor-axis collision does not introduce Y movement"),
		FMath::IsNearlyEqual(ResolvedFree.OutPos.Y, FreeStart.Y, 0.01f));
	TestFalse(TEXT("Hard and free nodes are separated"),
		NodesOverlap(ResolvedHard, ResolvedFree, Settings.NodeMargin));

	// Mirror the pair so the free node is the first stable item and the hard
	// obstacle is on its right. This exercises the opposite MTV application.
	UEdGraphNode* FreeLeftNode = MakeNode(Graph, false, 0, 0);
	UEdGraphNode* HardRightNode = MakeNode(Graph, false, 0, 0);
	const FVector2D FreeLeftStart(0.f, 500.f);
	const FVector2D HardRightStart(80.f, 500.f);
	TMap<UEdGraphNode*, FBALNode> MirroredProxies;
	FBALNode FreeLeftProxy = MakeProxy(FreeLeftNode, EBALNodeRole::Exec,
		FVector2D(100.f, 300.f), FreeLeftStart, false);
	FreeLeftProxy.StableIndex = 0;
	FBALNode HardRightProxy = MakeProxy(HardRightNode, EBALNodeRole::Exec,
		FVector2D(100.f, 300.f), HardRightStart, true);
	HardRightProxy.StableIndex = 1;
	HardRightProxy.bConstrained = true;
	HardRightProxy.ConstraintType = EBALConstraintType::Hard;
	MirroredProxies.Add(FreeLeftNode, FreeLeftProxy);
	MirroredProxies.Add(HardRightNode, HardRightProxy);
	ResolveProxies(MirroredProxies, Settings);

	const FBALNode& ResolvedFreeLeft = MirroredProxies.FindChecked(FreeLeftNode);
	const FBALNode& ResolvedHardRight = MirroredProxies.FindChecked(HardRightNode);
	TestTrue(TEXT("Mirrored hard node remains immutable"),
		ResolvedHardRight.OutPos.Equals(HardRightStart, 0.01f));
	TestTrue(TEXT("Free node on the left moves further left"),
		ResolvedFreeLeft.OutPos.X < FreeLeftStart.X);
	TestTrue(TEXT("Mirrored separation keeps the original Y"),
		FMath::IsNearlyEqual(ResolvedFreeLeft.OutPos.Y, FreeLeftStart.Y, 0.01f));
	TestFalse(TEXT("Mirrored hard and free nodes are separated"),
		NodesOverlap(ResolvedFreeLeft, ResolvedHardRight, Settings.NodeMargin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_NestedCommentBounds,
	"BlueprintAutoLayout.AdvancedRegression.Comments.NestedAutoBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_NestedCommentBounds::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode_Comment* Outer = MakeComment(Graph, 0, 0, 1200, 900);
	UEdGraphNode_Comment* Inner = MakeComment(Graph, 100, 100, 500, 400);
	UEdGraphNode* InnerA = MakeNode(Graph, false, 0, 0, 150, 180);
	UEdGraphNode* InnerB = MakeNode(Graph, false, 0, 0, 400, 300);
	UEdGraphNode* OuterOnly = MakeNode(Graph, false, 0, 0, 900, 650);
	InnerA->NodeWidth = 100;
	InnerA->NodeHeight = 60;
	InnerB->NodeWidth = 120;
	InnerB->NodeHeight = 80;
	OuterOnly->NodeWidth = 90;
	OuterOnly->NodeHeight = 50;

	FBALSettings Settings;
	Settings.bAutoSizeComments = true;
	Settings.bPreserveCommentContents = false;
	Settings.CommentPaddingX = 20.f;
	Settings.CommentPaddingY = 15.f;
	Settings.CommentHeaderHeight = 30.f;
	Settings.GridSnap = 0.f;
	Settings.MoveThreshold = 0.5f;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);

	const FBALCommentGroup* InnerGroup = FindCommentGroup(Analysis.CommentGroups, Inner);
	const FBALCommentGroup* OuterGroup = FindCommentGroup(Analysis.CommentGroups, Outer);
	TestNotNull(TEXT("Inner comment hierarchy entry exists"), InnerGroup);
	TestNotNull(TEXT("Outer comment hierarchy entry exists"), OuterGroup);
	if (InnerGroup)
	{
		TestEqual(TEXT("Inner comment parent is outer"), InnerGroup->ParentComment,
			static_cast<UEdGraphNode*>(Outer));
		TestTrue(TEXT("Inner comment directly owns first node"),
			InnerGroup->DirectMembers.Contains(InnerA));
		TestTrue(TEXT("Inner comment directly owns second node"),
			InnerGroup->DirectMembers.Contains(InnerB));
		TestEqual(TEXT("Inner comment depth"), InnerGroup->Depth, 1);
	}
	if (OuterGroup)
	{
		TestTrue(TEXT("Outer directly owns nested comment"),
			OuterGroup->DirectMembers.Contains(Inner));
		TestTrue(TEXT("Outer directly owns its non-nested node"),
			OuterGroup->DirectMembers.Contains(OuterOnly));
		TestFalse(TEXT("Outer does not flatten inner members"),
			OuterGroup->DirectMembers.Contains(InnerA));
	}

	FBALNode& InnerAProxy = Analysis.Proxies.FindChecked(InnerA);
	FBALNode& InnerBProxy = Analysis.Proxies.FindChecked(InnerB);
	FBALNode& OuterOnlyProxy = Analysis.Proxies.FindChecked(OuterOnly);
	InnerAProxy.OutPos = FVector2D(240.f, 320.f);
	InnerBProxy.OutPos = FVector2D(520.f, 440.f);
	OuterOnlyProxy.OutPos = FVector2D(900.f, 700.f);

	FBALCommentHandler::UpdateBounds(
		Analysis.Proxies, Analysis.CommentGroups, Settings);
	const FBALNode& InnerProxy = Analysis.Proxies.FindChecked(Inner);
	const FBALNode& OuterProxy = Analysis.Proxies.FindChecked(Outer);
	TestTrue(TEXT("Inner comment position is derived from direct members"),
		InnerProxy.OutPos.Equals(FVector2D(220.f, 275.f), 0.01f));
	TestTrue(TEXT("Inner comment size includes padding and header"),
		InnerProxy.Size.Equals(FVector2D(440.f, 260.f), 0.01f));
	TestTrue(TEXT("Outer comment is updated after inner comment"),
		OuterProxy.OutPos.Equals(FVector2D(200.f, 230.f), 0.01f));
	TestTrue(TEXT("Outer comment encloses updated inner and direct node"),
		OuterProxy.Size.Equals(FVector2D(810.f, 535.f), 0.01f));
	TestTrue(TEXT("Inner contains first member"), ContainsBounds(InnerProxy, InnerAProxy));
	TestTrue(TEXT("Inner contains second member"), ContainsBounds(InnerProxy, InnerBProxy));
	TestTrue(TEXT("Outer contains resized inner"), ContainsBounds(OuterProxy, InnerProxy));
	TestTrue(TEXT("Outer contains outer-only member"), ContainsBounds(OuterProxy, OuterOnlyProxy));

	const int32 Written = FBALCommitter::Commit(
		Graph, Analysis.Proxies, Constraints, Settings);
	TestTrue(TEXT("Comment and member updates are committed"), Written >= 5);
	TestEqual(TEXT("Committed inner X"), Inner->NodePosX, 220);
	TestEqual(TEXT("Committed inner Y"), Inner->NodePosY, 275);
	TestEqual(TEXT("Committed inner width"), Inner->NodeWidth, 440);
	TestEqual(TEXT("Committed inner height"), Inner->NodeHeight, 260);
	TestEqual(TEXT("Committed outer X"), Outer->NodePosX, 200);
	TestEqual(TEXT("Committed outer Y"), Outer->NodePosY, 230);
	TestEqual(TEXT("Committed outer width"), Outer->NodeWidth, 810);
	TestEqual(TEXT("Committed outer height"), Outer->NodeHeight, 535);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_RepeatedLayoutIdempotence,
	"BlueprintAutoLayout.AdvancedRegression.Pipeline.RepeatedLayoutIdempotence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_RepeatedLayoutIdempotence::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* Root = MakeNode(Graph, true, 0, 0, 320, 240);
	UEdGraphNode* Upper = MakeNode(Graph, true, 0, 0, 0, 0);
	UEdGraphNode* Lower = MakeNode(Graph, true, 0, 0, 0, 0);
	UEdGraphNode* Merge = MakeNode(Graph, true, 1, 0, 0, 0);
	UEdGraphNode* PureNear = MakeNode(Graph, false, 1, 1, 0, 0);
	UEdGraphNode* PureFar = MakeNode(Graph, false, 0, 1, 0, 0);
	AddExecOutput(Root, FName(TEXT("ExecOutLower")));
	WireExecPins(Root, 0, Upper);
	WireExecPins(Root, 1, Lower);
	WireExecPins(Upper, 0, Merge);
	WireExecPins(Lower, 0, Merge);
	WireData(PureNear, 0, Merge, 0);
	WireData(PureFar, 0, PureNear, 0);

	FBALSettings Settings;
	Settings.bForceDir = true;
	Settings.ForcedDir = EBALPureDir::West;
	Settings.bPreserveAnchors = true;
	Settings.GridSnap = 8.f;
	Settings.NodeMargin = 8.f;
	Settings.CollisionIter = 60;
	FBlueprintAutoLayoutEngine::Layout(Graph, Settings);

	TMap<UEdGraphNode*, FIntPoint> FirstPositions;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			FirstPositions.Add(Node, FIntPoint(Node->NodePosX, Node->NodePosY));
		}
	}

	FBlueprintAutoLayoutEngine::Layout(Graph, Settings);
	bool bAllPositionsStable = true;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		const FIntPoint* First = FirstPositions.Find(Node);
		if (!Node || !First)
		{
			continue;
		}
		if (Node->NodePosX != First->X || Node->NodePosY != First->Y)
		{
			bAllPositionsStable = false;
			AddError(FString::Printf(
				TEXT("Repeated layout moved node %d from (%d,%d) to (%d,%d)"),
				Graph->Nodes.IndexOfByKey(Node), First->X, First->Y,
				Node->NodePosX, Node->NodePosY));
		}
	}
	TestTrue(TEXT("Second layout is exactly position-idempotent"), bAllPositionsStable);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_RigidGroupRelativeOffsets,
	"BlueprintAutoLayout.AdvancedRegression.Constraints.RigidGroupRelativeOffsets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_RigidGroupRelativeOffsets::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode* First = MakeNode(Graph, true, 0, 0, 101, 103);
	UEdGraphNode* Second = MakeNode(Graph, true, 0, 0, 401, 253);
	WireExec(First, Second);

	TArray<FBALConstraint> Constraints;
	FBALConstraint FirstConstraint;
	FirstConstraint.Node = First;
	FirstConstraint.Type = EBALConstraintType::RigidGroup;
	FirstConstraint.GroupId = 41;
	FirstConstraint.OriginalPos = FVector2D(First->NodePosX, First->NodePosY);
	Constraints.Add(FirstConstraint);
	FBALConstraint SecondConstraint = FirstConstraint;
	SecondConstraint.Node = Second;
	SecondConstraint.OriginalPos = FVector2D(Second->NodePosX, Second->NodePosY);
	Constraints.Add(SecondConstraint);

	const FIntPoint OriginalOffset(
		Second->NodePosX - First->NodePosX,
		Second->NodePosY - First->NodePosY);
	FBALSettings Settings;
	Settings.GridSnap = 8.f;
	Settings.NodeMargin = 8.f;
	Settings.CollisionIter = 60;
	FBlueprintAutoLayoutEngine::LayoutWithConstraints(Graph, Constraints, Settings);

	const FIntPoint FinalOffset(
		Second->NodePosX - First->NodePosX,
		Second->NodePosY - First->NodePosY);
	TestEqual(TEXT("Rigid group preserves X offset"), FinalOffset.X, OriginalOffset.X);
	TestEqual(TEXT("Rigid group preserves Y offset"), FinalOffset.Y, OriginalOffset.Y);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBALTest_Advanced_CommentBoundsAfterFinalSnap,
	"BlueprintAutoLayout.AdvancedRegression.Comments.BoundsAfterFinalSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBALTest_Advanced_CommentBoundsAfterFinalSnap::RunTest(const FString& /*Params*/)
{
	using namespace BALTest;

	UEdGraph* Graph = MakeGraph();
	UEdGraphNode_Comment* Comment = MakeComment(Graph, -100, -100, 500, 500);
	UEdGraphNode* Member = MakeNode(Graph, false, 0, 0, 0, 0);
	Member->NodeWidth = 100;
	Member->NodeHeight = 100;

	FBALSettings Settings;
	Settings.GridSnap = 8.f;
	Settings.MoveThreshold = 0.1f;
	Settings.CommentPaddingX = 20.f;
	Settings.CommentPaddingY = 20.f;
	Settings.CommentHeaderHeight = 30.f;
	TArray<FBALConstraint> Constraints;
	FBALGraphAnalyzer::FAnalysisResult Analysis =
		FBALGraphAnalyzer::Analyze(Graph, Settings, Constraints);
	Analysis.Proxies.FindChecked(Member).OutPos = FVector2D(13.f, 13.f);

	const int32 Written = FBALCommitter::Commit(
		Graph, Analysis.Proxies, Constraints, Settings, &Analysis.CommentGroups);
	TestTrue(TEXT("Member and comment are committed"), Written >= 2);
	TestEqual(TEXT("Member snaps before comment bounds X"), Member->NodePosX, 16);
	TestEqual(TEXT("Member snaps before comment bounds Y"), Member->NodePosY, 16);
	TestEqual(TEXT("Comment uses final snapped member X"), Comment->NodePosX, -8);
	TestEqual(TEXT("Comment uses final snapped member Y"), Comment->NodePosY, -40);
	TestEqual(TEXT("Comment width wraps final member"), Comment->NodeWidth, 144);
	TestEqual(TEXT("Comment height wraps final member"), Comment->NodeHeight, 176);

	return true;
}
