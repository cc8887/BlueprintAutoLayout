// BALCommentHandler.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALCommentHandler.h"

void FBALCommentHandler::UpdateBounds(
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	const TArray<FBALCommentGroup>& Groups,
	const FBALSettings& Settings)
{
	if (!Settings.bAutoSizeComments || Settings.bPreserveCommentContents)
	{
		return;
	}

	TArray<int32> Order;
	for (int32 Index = 0; Index < Groups.Num(); ++Index) Order.Add(Index);
	Order.Sort([&Groups](int32 A, int32 B)
	{
		if (Groups[A].Depth != Groups[B].Depth) return Groups[A].Depth > Groups[B].Depth;
		return A < B;
	});

	for (int32 GroupIndex : Order)
	{
		const FBALCommentGroup& Group = Groups[GroupIndex];
		FBALNode* Comment = Proxies.Find(Group.Comment);
		if (!Comment || Comment->bConstrained
			|| Group.DirectMembers.Num() == 0)
		{
			continue;
		}

		FBox2D Bounds(EForceInit::ForceInit);
		bool bHasMember = false;
		for (UEdGraphNode* MemberNode : Group.DirectMembers)
		{
			const FBALNode* Member = Proxies.Find(MemberNode);
			if (!Member) continue;
			const FBox2D MemberBounds(Member->OutPos, Member->OutPos + Member->Size);
			Bounds = bHasMember ? Bounds + MemberBounds : MemberBounds;
			bHasMember = true;
		}
		if (!bHasMember) continue;

		Comment->OutPos = FVector2D(
			Bounds.Min.X - Settings.CommentPaddingX,
			Bounds.Min.Y - Settings.CommentPaddingY - Settings.CommentHeaderHeight);
		Comment->Size = FVector2D(
			Bounds.GetSize().X + Settings.CommentPaddingX * 2.f,
			Bounds.GetSize().Y + Settings.CommentPaddingY * 2.f + Settings.CommentHeaderHeight);
		if (Settings.GridSnap > 0.f)
		{
			const FVector2D RawMax = Comment->OutPos + Comment->Size;
			Comment->OutPos = FVector2D(
				FMath::FloorToFloat(Comment->OutPos.X / Settings.GridSnap) * Settings.GridSnap,
				FMath::FloorToFloat(Comment->OutPos.Y / Settings.GridSnap) * Settings.GridSnap);
			const FVector2D SnappedMax(
				FMath::CeilToFloat(RawMax.X / Settings.GridSnap) * Settings.GridSnap,
				FMath::CeilToFloat(RawMax.Y / Settings.GridSnap) * Settings.GridSnap);
			Comment->Size = SnappedMax - Comment->OutPos;
		}
	}
}
