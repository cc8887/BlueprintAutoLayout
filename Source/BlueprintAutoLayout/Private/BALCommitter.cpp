// BALCommitter.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALCommitter.h"
#include "BALCommentHandler.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "BlueprintAutoLayout"

namespace
{
	struct FFinalNode
	{
		UEdGraphNode* Node = nullptr;
		const FBALNode* Proxy = nullptr;
		FVector2D Position = FVector2D::ZeroVector;
		bool bLocked = false;
		bool bRigid = false;
		int32 GroupId = INDEX_NONE;
	};
	typedef TMap<int32, TArray<FFinalNode*>> FFinalRigidGroups;

	FVector2D ClampSoftPosition(const FBALNode& Proxy, FVector2D Position)
	{
		if (!Proxy.bConstrained || Proxy.ConstraintType != EBALConstraintType::Soft || Proxy.MaxDrift < 0.f)
		{
			return Position;
		}
		const FVector2D Delta = Position - Proxy.OriginalPos;
		const float Distance = Delta.Size();
		return Distance > Proxy.MaxDrift && Distance > KINDA_SMALL_NUMBER
			? Proxy.OriginalPos + Delta * (Proxy.MaxDrift / Distance)
			: Position;
	}

	bool MoveFinalNode(
		FFinalNode& FinalNode,
		FVector2D Delta,
		const FFinalRigidGroups& RigidGroups)
	{
		if (FinalNode.bLocked || !FinalNode.Proxy) return false;
		if (FinalNode.bRigid && FinalNode.GroupId != INDEX_NONE)
		{
			const TArray<FFinalNode*>* Group = RigidGroups.Find(FinalNode.GroupId);
			if (!Group || Group->ContainsByPredicate([](const FFinalNode* Member)
				{ return !Member || Member->bLocked; }))
			{
				return false;
			}
			for (FFinalNode* Member : *Group)
			{
				Member->Position += Delta;
			}
			return true;
		}
		const FVector2D Before = FinalNode.Position;
		FinalNode.Position = ClampSoftPosition(*FinalNode.Proxy, FinalNode.Position + Delta);
		return !FinalNode.Position.Equals(Before, 0.01f);
	}
}

int32 FBALCommitter::Commit(
	UEdGraph* Graph,
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	const TArray<FBALConstraint>& Constraints,
	const FBALSettings& Settings,
	const TArray<FBALCommentGroup>* CommentGroups)
{
	if (!Graph)
	{
		return 0;
	}

	TSet<UEdGraphNode*> HardLocked;
	for (const FBALConstraint& Constraint : Constraints)
	{
		if (Constraint.Type == EBALConstraintType::Hard
			|| (Constraint.Type == EBALConstraintType::RigidGroup
				&& Constraint.bCommentGroup && Settings.bPreserveCommentContents))
		{
			HardLocked.Add(Constraint.Node);
		}
	}
	if (Settings.bPreserveCommentContents)
	{
		// Some outer comments directly contain only another comment and therefore
		// have no collected rigid-group constraint. Preserve mode must still keep
		// every comment fixed instead of snapping those outer boxes independently.
		for (const TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
		{
			if (Pair.Key && Pair.Value.Role == EBALNodeRole::Comment)
			{
				HardLocked.Add(Pair.Key);
			}
		}
	}

	TArray<FFinalNode> FinalNodes;
	for (const TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		if (!Pair.Key || Pair.Value.Role == EBALNodeRole::Comment) continue;
		FFinalNode FinalNode;
		FinalNode.Node = Pair.Key;
		FinalNode.Proxy = &Pair.Value;
		FinalNode.bLocked = Pair.Value.bLocked || HardLocked.Contains(Pair.Key);
		FinalNode.bRigid = Pair.Value.bConstrained && Pair.Value.GroupId != INDEX_NONE;
		FinalNode.GroupId = Pair.Value.GroupId;
		FinalNode.Position = FinalNode.bLocked
			? Pair.Value.OriginalPos
			: (FinalNode.bRigid
				? Pair.Value.OutPos
				: ClampSoftPosition(Pair.Value, SnapToGrid(Pair.Value.OutPos, Settings.GridSnap)));
		FinalNodes.Add(FinalNode);
	}
	FinalNodes.Sort([](const FFinalNode& A, const FFinalNode& B)
	{
		return A.Proxy->StableIndex < B.Proxy->StableIndex;
	});

	FFinalRigidGroups RigidGroups;
	TMap<int32, FVector2D> TranslationSums;
	TMap<int32, int32> TranslationCounts;
	for (FFinalNode& FinalNode : FinalNodes)
	{
		if (!FinalNode.bRigid) continue;
		RigidGroups.FindOrAdd(FinalNode.GroupId).Add(&FinalNode);
		if (FinalNode.bLocked) continue;
		const FVector2D Delta = FinalNode.Position - FinalNode.Proxy->OriginalPos;
		if (FVector2D* Sum = TranslationSums.Find(FinalNode.GroupId))
		{
			*Sum += Delta;
		}
		else
		{
			TranslationSums.Add(FinalNode.GroupId, Delta);
		}
		++TranslationCounts.FindOrAdd(FinalNode.GroupId);
	}
	for (TPair<int32, TArray<FFinalNode*>>& Pair : RigidGroups)
	{
		const bool bLockedGroup = Pair.Value.ContainsByPredicate(
			[](const FFinalNode* Member) { return !Member || Member->bLocked; });
		const int32 Count = TranslationCounts.FindRef(Pair.Key);
		if (!bLockedGroup && Count <= 0) continue;
		const FVector2D Translation = bLockedGroup
			? FVector2D::ZeroVector
			: SnapToGrid(TranslationSums.FindRef(Pair.Key) / static_cast<float>(Count), Settings.GridSnap);
		for (FFinalNode* Member : Pair.Value)
		{
			Member->Position = Member->Proxy->OriginalPos + Translation;
		}
	}
	for (FFinalNode& FinalNode : FinalNodes)
	{
		if (!FinalNode.bLocked
			&& (FinalNode.Position - FinalNode.Proxy->OriginalPos).Size() < Settings.MoveThreshold)
		{
			FinalNode.Position = FinalNode.Proxy->OriginalPos;
		}
	}

	// Resolve any overlap reintroduced by grid snapping. Include locked and
	// unchanged nodes, since they are obstacles for selection formatting.
	const float Step = Settings.GridSnap > 0.f ? Settings.GridSnap : 1.f;
	for (int32 Iteration = 0; Iteration < 100; ++Iteration)
	{
		bool bMoved = false;
		for (int32 AIndex = 0; AIndex < FinalNodes.Num(); ++AIndex)
		{
			for (int32 BIndex = AIndex + 1; BIndex < FinalNodes.Num(); ++BIndex)
			{
				FFinalNode& A = FinalNodes[AIndex];
				FFinalNode& B = FinalNodes[BIndex];
				if (A.bRigid && B.bRigid && A.GroupId == B.GroupId) continue;
				const float Margin = Settings.NodeMargin;
				const float OverlapX = FMath::Min(A.Position.X + A.Proxy->Size.X + Margin, B.Position.X + B.Proxy->Size.X + Margin)
					- FMath::Max(A.Position.X - Margin, B.Position.X - Margin);
				const float OverlapY = FMath::Min(A.Position.Y + A.Proxy->Size.Y + Margin, B.Position.Y + B.Proxy->Size.Y + Margin)
					- FMath::Max(A.Position.Y - Margin, B.Position.Y - Margin);
				if (OverlapX <= 0.f || OverlapY <= 0.f || (A.bLocked && B.bLocked)) continue;

				FVector2D Delta = FVector2D::ZeroVector;
				if (OverlapX < OverlapY)
				{
					const float Amount = FMath::CeilToFloat(OverlapX / Step) * Step;
					Delta.X = B.Position.X + B.Proxy->Size.X * 0.5f >= A.Position.X + A.Proxy->Size.X * 0.5f
						? Amount : -Amount;
				}
				else
				{
					const float Amount = FMath::CeilToFloat(OverlapY / Step) * Step;
					Delta.Y = B.Position.Y + B.Proxy->Size.Y * 0.5f >= A.Position.Y + A.Proxy->Size.Y * 0.5f
						? Amount : -Amount;
				}

				bool bPairMoved = false;
				if (!B.bLocked) bPairMoved = MoveFinalNode(B, Delta, RigidGroups);
				if (!bPairMoved && !A.bLocked) bPairMoved = MoveFinalNode(A, -Delta, RigidGroups);
				bMoved = bMoved || bPairMoved;
			}
		}
		if (!bMoved) break;
	}

	TMap<int32, FVector2D> FinalGroupTranslations;
	for (const FFinalNode& FinalNode : FinalNodes)
	{
		FBALNode* Proxy = Proxies.Find(FinalNode.Node);
		if (!Proxy) continue;
		Proxy->OutPos = FinalNode.Position;
		if (FinalNode.bRigid && !FinalNode.bLocked)
		{
			FinalGroupTranslations.FindOrAdd(FinalNode.GroupId) =
				FinalNode.Position - FinalNode.Proxy->OriginalPos;
		}
	}
	for (TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		FBALNode& Proxy = Pair.Value;
		if (!Proxy.bLocked && Proxy.bConstrained
			&& Proxy.ConstraintType == EBALConstraintType::RigidGroup)
		{
			if (const FVector2D* Translation = FinalGroupTranslations.Find(Proxy.GroupId))
			{
				Proxy.OutPos = Proxy.OriginalPos + *Translation;
			}
		}
	}
	if (CommentGroups)
	{
		FBALCommentHandler::UpdateBounds(Proxies, *CommentGroups, Settings);
	}
	TSet<UEdGraphNode*> AutoSizedComments;
	if (CommentGroups && Settings.bAutoSizeComments && !Settings.bPreserveCommentContents)
	{
		for (const FBALCommentGroup& Group : *CommentGroups)
		{
			const FBALNode* Comment = Proxies.Find(Group.Comment);
			if (Comment && !Comment->bConstrained && Group.DirectMembers.Num() > 0)
			{
				AutoSizedComments.Add(Group.Comment);
			}
		}
	}

	struct FWrite
	{
		UEdGraphNode* Node = nullptr;
		FVector2D Position = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		bool bWritePosition = false;
		bool bWriteSize = false;
	};
	TArray<FWrite> Writes;
	for (const FFinalNode& FinalNode : FinalNodes)
	{
		if (FinalNode.bLocked) continue;
		if ((FinalNode.Position - FinalNode.Proxy->OriginalPos).Size() < Settings.MoveThreshold) continue;
		FWrite Write;
		Write.Node = FinalNode.Node;
		Write.Position = FinalNode.Position;
		Write.bWritePosition = true;
		Writes.Add(Write);
	}
	for (const TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		if (!Pair.Key || Pair.Value.Role != EBALNodeRole::Comment || HardLocked.Contains(Pair.Key)) continue;
		const bool bAutoSized = AutoSizedComments.Contains(Pair.Key);
		const FVector2D Position = bAutoSized
			? Pair.Value.OutPos
			: (Pair.Value.bConstrained && Pair.Value.ConstraintType == EBALConstraintType::RigidGroup
				? Pair.Value.OutPos
				: ClampSoftPosition(Pair.Value, SnapToGrid(Pair.Value.OutPos, Settings.GridSnap)));
		const FVector2D Size(
			static_cast<float>(FMath::RoundToInt(Pair.Value.Size.X)),
			static_cast<float>(FMath::RoundToInt(Pair.Value.Size.Y)));
		const bool bMove = (Position - Pair.Value.OriginalPos).Size() >= Settings.MoveThreshold;
		const bool bResize = !Size.Equals(Pair.Value.OriginalSize, 0.5f);
		if (!bMove && !bResize) continue;
		FWrite Write;
		Write.Node = Pair.Key;
		Write.Position = Position;
		Write.Size = Size;
		Write.bWritePosition = bMove;
		Write.bWriteSize = bResize;
		Writes.Add(Write);
	}

	if (Writes.Num() == 0)
	{
		return 0;
	}

	FScopedTransaction Transaction(LOCTEXT("AutoLayout", "Auto Layout Blueprint Graph"));
	Graph->Modify();
	for (const FWrite& Write : Writes)
	{
		Write.Node->Modify();
		if (Write.bWritePosition)
		{
			Write.Node->NodePosX = FMath::RoundToInt(Write.Position.X);
			Write.Node->NodePosY = FMath::RoundToInt(Write.Position.Y);
		}
		if (Write.bWriteSize)
		{
			Write.Node->NodeWidth = FMath::Max(1, FMath::RoundToInt(Write.Size.X));
			Write.Node->NodeHeight = FMath::Max(1, FMath::RoundToInt(Write.Size.Y));
		}
	}
	Graph->NotifyGraphChanged();
	return Writes.Num();
}

FVector2D FBALCommitter::SnapToGrid(FVector2D Position, float GridSnap)
{
	if (GridSnap <= 0.f) return Position;
	return FVector2D(
		FMath::RoundToFloat(Position.X / GridSnap) * GridSnap,
		FMath::RoundToFloat(Position.Y / GridSnap) * GridSnap);
}

#undef LOCTEXT_NAMESPACE
