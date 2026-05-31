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

		// Already handled as a RigidGroup member or Comment box itself
		if (GroupMap.Contains(Node)) continue;

		if (IsPinned(Node))
		{
			FBALConstraint C;
			C.Node        = Node;
			C.Type        = EBALConstraintType::Hard;
			C.MaxDrift    = 0.f;
			C.OriginalPos = FVector2D((float)Node->NodePosX, (float)Node->NodePosY);
			OutConstraints.Add(C);
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

	// Keep only constraints whose node is inside the selection (or is a Comment)
	for (FBALConstraint& C : StandardConstraints)
	{
		if (Selection.Contains(C.Node) || Cast<UEdGraphNode_Comment>(C.Node))
		{
			OutConstraints.Add(C);
		}
	}

	// All nodes OUTSIDE the selection become Hard constraints
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (Selection.Contains(Node)) continue;

		// Don't double-add
		bool bAlreadyAdded = OutConstraints.ContainsByPredicate(
			[Node](const FBALConstraint& C){ return C.Node == Node; });
		if (bAlreadyAdded) continue;

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
	int32 GroupId = 0;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node);
		if (!Comment) continue;

		// Find all nodes inside this comment box
		// Comment boxes in UE store their contained nodes via the NodeComment rect
		const int32 CX = Comment->NodePosX;
		const int32 CY = Comment->NodePosY;
		const int32 CW = Comment->NodeWidth;
		const int32 CH = Comment->NodeHeight;

		TArray<UEdGraphNode*> MembersInBox;

		for (UEdGraphNode* Other : Graph->Nodes)
		{
			if (Other == Comment) continue;
			// Check if Other's centre is inside the comment rect
			const float OX = (float)Other->NodePosX;
			const float OY = (float)Other->NodePosY;
			if (OX >= CX && OX <= (CX + CW) && OY >= CY && OY <= (CY + CH))
			{
				MembersInBox.Add(Other);
			}
		}

		if (MembersInBox.Num() == 0) continue;

		// Comment box itself: Hard (it will be moved as a rigid unit)
		{
			FBALConstraint C;
			C.Node        = Comment;
			C.Type        = EBALConstraintType::RigidGroup;
			C.MaxDrift    = 0.f;
			C.OriginalPos = FVector2D((float)CX, (float)CY);
			C.GroupId     = GroupId;
			OutConstraints.Add(C);
			OutGroupMap.Add(Comment, GroupId);
		}

		// Members: RigidGroup
		for (UEdGraphNode* Member : MembersInBox)
		{
			FBALConstraint C;
			C.Node        = Member;
			C.Type        = EBALConstraintType::RigidGroup;
			C.MaxDrift    = 0.f;
			C.OriginalPos = FVector2D((float)Member->NodePosX, (float)Member->NodePosY);
			C.GroupId     = GroupId;
			OutConstraints.Add(C);
			OutGroupMap.Add(Member, GroupId);
		}

		++GroupId;
	}
}
