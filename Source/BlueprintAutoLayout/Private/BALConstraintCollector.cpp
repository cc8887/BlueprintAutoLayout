// BALConstraintCollector.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALConstraintCollector.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"

// ─────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────

void FBALConstraintCollector::Collect(UEdGraph* Graph,
                                      TArray<FBALConstraint>& OutConstraints)
{
	if (!Graph) return;

	TMap<UEdGraphNode*, int32> GroupMap;
	CollectCommentGroups(Graph, OutConstraints, GroupMap);

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		if (IsPinned(Node))
		{
			// Explicit pinning always overrides comment membership.
			OutConstraints.RemoveAll([Node](const FBALConstraint& Existing)
			{
				return Existing.Node == Node;
			});
			FBALConstraint C;
			C.Node        = Node;
			C.Type        = EBALConstraintType::Hard;
			C.MaxDrift    = 0.f;
			C.OriginalPos = FVector2D((float)Node->NodePosX, (float)Node->NodePosY);
			OutConstraints.Add(C);
		}
		// Already represented as a movable comment group member.
		else if (GroupMap.Contains(Node))
		{
			continue;
		}
		// Knot nodes: soft constraint — let them drift a bit to resolve wire crossings
		else if (Node->GetClass()->GetName().Contains(TEXT("Knot")) ||
		         Node->GetClass()->GetName().Contains(TEXT("Reroute")))
		{
			FBALConstraint C;
			C.Node        = Node;
			C.Type        = EBALConstraintType::Soft;
			C.MaxDrift    = 80.f;
			C.OriginalPos = FVector2D((float)Node->NodePosX, (float)Node->NodePosY);
			OutConstraints.Add(C);
		}
	}
}

void FBALConstraintCollector::CollectForSelection(UEdGraph* Graph,
                                                  const TSet<UEdGraphNode*>& Selection,
                                                  TArray<FBALConstraint>& OutConstraints)
{
	if (!Graph) return;

	// First collect the standard constraints within the selection
	TArray<FBALConstraint> StandardConstraints;
	Collect(Graph, StandardConstraints);

	// Keep standard constraints only for nodes inside the selection.
	for (FBALConstraint& C : StandardConstraints)
	{
		if (Selection.Contains(C.Node))
		{
			OutConstraints.Add(C);
		}
	}

	// All nodes OUTSIDE the selection become Hard constraints
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (Selection.Contains(Node)) continue;

		OutConstraints.RemoveAll([Node](const FBALConstraint& Existing)
		{
			return Existing.Node == Node;
		});

		FBALConstraint C;
		C.Node        = Node;
		C.Type        = EBALConstraintType::Hard;
		C.MaxDrift    = 0.f;
		C.OriginalPos = FVector2D((float)Node->NodePosX, (float)Node->NodePosY);
		OutConstraints.Add(C);
	}
}

// ─────────────────────────────────────────────────────────────
//  Metadata helpers
// ─────────────────────────────────────────────────────────────

bool FBALConstraintCollector::IsPinned(const UEdGraphNode* Node)
{
	if (!Node) return false;
	// We store the pin tag in NodeComment as a substring
	return Node->NodeComment.Contains(FString(GetPinMetaKey()));
}

void FBALConstraintCollector::SetPinned(UEdGraphNode* Node, bool bPinned)
{
	if (!Node) return;
	const FString Tag(GetPinMetaKey());
	if (bPinned)
	{
		if (!Node->NodeComment.Contains(Tag))
		{
			Node->NodeComment += FString(TEXT(" ")) + Tag;
			Node->NodeComment.TrimStartAndEndInline();
		}
	}
	else
	{
		Node->NodeComment = Node->NodeComment.Replace(*Tag, TEXT(""));
		Node->NodeComment.TrimStartAndEndInline();
	}
}

// ─────────────────────────────────────────────────────────────
//  Private: Comment box rigid groups
// ─────────────────────────────────────────────────────────────

void FBALConstraintCollector::CollectCommentGroups(UEdGraph* Graph,
                                                   TArray<FBALConstraint>& OutConstraints,
                                                   TMap<UEdGraphNode*, int32>& OutGroupMap)
{
	TArray<UEdGraphNode_Comment*> Comments;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
		{
			Comments.Add(Comment);
		}
	}

	TMap<UEdGraphNode_Comment*, int32> CommentIds;
	TMap<UEdGraphNode_Comment*, TArray<UEdGraphNode*>> DirectMembers;
	for (int32 Index = 0; Index < Comments.Num(); ++Index)
	{
		CommentIds.Add(Comments[Index], Index);
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node || Cast<UEdGraphNode_Comment>(Node)) continue;
		UEdGraphNode_Comment* BestComment = nullptr;
		float BestArea = MAX_flt;
		const float CenterX = Node->NodePosX + FMath::Max(0, Node->NodeWidth) * 0.5f;
		const float CenterY = Node->NodePosY + FMath::Max(0, Node->NodeHeight) * 0.5f;
		for (UEdGraphNode_Comment* Comment : Comments)
		{
			const float Area = static_cast<float>(Comment->NodeWidth) * Comment->NodeHeight;
			const bool bInside = CenterX >= Comment->NodePosX
				&& CenterX <= Comment->NodePosX + Comment->NodeWidth
				&& CenterY >= Comment->NodePosY
				&& CenterY <= Comment->NodePosY + Comment->NodeHeight;
			if (bInside && Area < BestArea)
			{
				BestArea = Area;
				BestComment = Comment;
			}
		}
		if (BestComment) DirectMembers.FindOrAdd(BestComment).Add(Node);
	}

	for (UEdGraphNode_Comment* Comment : Comments)
	{
		const TArray<UEdGraphNode*>* Members = DirectMembers.Find(Comment);
		if (!Members || Members->Num() == 0) continue;
		const int32 GroupId = CommentIds.FindChecked(Comment);
		FBALConstraint CommentConstraint;
		CommentConstraint.Node = Comment;
		CommentConstraint.Type = EBALConstraintType::RigidGroup;
		CommentConstraint.OriginalPos = FVector2D(Comment->NodePosX, Comment->NodePosY);
		CommentConstraint.GroupId = GroupId;
		CommentConstraint.bCommentGroup = true;
		OutConstraints.Add(CommentConstraint);
		OutGroupMap.Add(Comment, GroupId);

		for (UEdGraphNode* Member : *Members)
		{
			FBALConstraint MemberConstraint;
			MemberConstraint.Node = Member;
			MemberConstraint.Type = EBALConstraintType::RigidGroup;
			MemberConstraint.OriginalPos = FVector2D(Member->NodePosX, Member->NodePosY);
			MemberConstraint.GroupId = GroupId;
			MemberConstraint.bCommentGroup = true;
			OutConstraints.Add(MemberConstraint);
			OutGroupMap.Add(Member, GroupId);
		}
	}
}
