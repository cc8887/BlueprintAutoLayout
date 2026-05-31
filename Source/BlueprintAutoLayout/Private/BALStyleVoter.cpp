// BALStyleVoter.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALStyleVoter.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

// ─────────────────────────────────────────────────────────────
//  Public: Vote
// ─────────────────────────────────────────────────────────────

EBALPureDir FBALStyleVoter::Vote(const FVoteContext& Ctx)
{
	if (!Ctx.Proxies || !Ctx.ExecRoots || !Ctx.Settings) return EBALPureDir::West;

	const FBALSettings& Settings = *Ctx.Settings;

	if (Settings.bForceDir)
		return Settings.ForcedDir;

	TMap<EBALPureDir, int32> Tally;
	Tally.Add(EBALPureDir::West,  0);
	Tally.Add(EBALPureDir::East,  0);
	Tally.Add(EBALPureDir::North, 0);
	Tally.Add(EBALPureDir::South, 0);

	// Traverse exec tree and vote for each exec node that has a Pure group
	TFunction<void(const FBALExecNode*)> Traverse = [&](const FBALExecNode* EN)
	{
		if (!EN) return;

		if (EN->PureGroup.Num() > 0 && !EN->Proxy->bLocked)
		{
			const FBALNode& Consumer = *EN->Proxy;

			FVoteCandidate Best = { EBALPureDir::West, -1e9f };
			for (EBALPureDir Dir : {EBALPureDir::West, EBALPureDir::East,
			                        EBALPureDir::North, EBALPureDir::South})
			{
				FVoteCandidate Cand = EvaluateDir(Dir, EN->PureGroup, Consumer, Ctx);
				if (Cand.Score > Best.Score)
					Best = Cand;
			}
			Tally[Best.Dir]++;
		}

		for (const FBALExecNode* Child : EN->Children)
			Traverse(Child);
	};

	for (const FBALExecNode* Root : *Ctx.ExecRoots)
		Traverse(Root);

	// Find winner
	EBALPureDir Winner = EBALPureDir::West;
	int32 BestVotes = -1;
	for (auto& KV : Tally)
	{
		if (KV.Value > BestVotes)
		{
			BestVotes = KV.Value;
			Winner    = KV.Key;
		}
	}

	// No Pure groups at all → default West
	if (BestVotes == 0) return EBALPureDir::West;

	return Winner;
}

// ─────────────────────────────────────────────────────────────
//  Private: per-direction evaluation
// ─────────────────────────────────────────────────────────────

FBALStyleVoter::FVoteCandidate FBALStyleVoter::EvaluateDir(EBALPureDir Dir,
                                                           const TArray<FBALNode*>& PureGroup,
                                                           const FBALNode& Consumer,
                                                           const FVoteContext& Ctx)
{
	FBox2D CandAABB  = ComputeCandidateAABB(Dir, PureGroup, Consumer, *Ctx.Settings);
	float OverlapCost = MeasureOverlap(CandAABB, *Ctx.Proxies);
	float WireCost    = MeasureWireCost(Dir, PureGroup, Consumer);
	float CrossCost   = MeasureAxisCross(CandAABB, *Ctx.ExecRoots);

	float Score = -(OverlapCost * 3.f) - WireCost - (CrossCost * 2.f);
	return { Dir, Score };
}

FBox2D FBALStyleVoter::ComputeCandidateAABB(EBALPureDir Dir,
                                             const TArray<FBALNode*>& PureGroup,
                                             const FBALNode& Consumer,
                                             const FBALSettings& Settings)
{
	// Compute total group size
	float MaxW = 0.f, TotalH = 0.f, MaxH = 0.f, TotalW = 0.f;
	for (const FBALNode* P : PureGroup)
	{
		MaxW   = FMath::Max(MaxW,   P->Size.X);
		TotalH += P->Size.Y;
		MaxH   = FMath::Max(MaxH,   P->Size.Y);
		TotalW += P->Size.X;
	}

	FVector2D GroupSize;
	FVector2D Origin;

	switch (Dir)
	{
	case EBALPureDir::West:
		// Stacked vertically, placed left of consumer
		GroupSize = FVector2D(MaxW, TotalH);
		Origin    = FVector2D(Consumer.OutPos.X - MaxW - Settings.GapX,
		                      Consumer.OutPos.Y + Consumer.Size.Y * 0.5f - TotalH * 0.5f);
		break;

	case EBALPureDir::East:
		GroupSize = FVector2D(MaxW, TotalH);
		Origin    = FVector2D(Consumer.OutPos.X + Consumer.Size.X + Settings.GapX,
		                      Consumer.OutPos.Y + Consumer.Size.Y * 0.5f - TotalH * 0.5f);
		break;

	case EBALPureDir::North:
		GroupSize = FVector2D(TotalW, MaxH);
		Origin    = FVector2D(Consumer.OutPos.X + Consumer.Size.X * 0.5f - TotalW * 0.5f,
		                      Consumer.OutPos.Y - MaxH - Settings.GapY);
		break;

	case EBALPureDir::South:
		GroupSize = FVector2D(TotalW, MaxH);
		Origin    = FVector2D(Consumer.OutPos.X + Consumer.Size.X * 0.5f - TotalW * 0.5f,
		                      Consumer.OutPos.Y + Consumer.Size.Y + Settings.GapY);
		break;
	}

	return FBox2D(Origin, Origin + GroupSize);
}

float FBALStyleVoter::MeasureOverlap(const FBox2D& CandAABB,
                                     const TMap<UEdGraphNode*, FBALNode>& Proxies)
{
	float TotalOverlap = 0.f;
	for (const auto& KV : Proxies)
	{
		const FBALNode& N = KV.Value;
		FBox2D NodeBox(N.OutPos, N.OutPos + N.Size);
		if (CandAABB.Intersect(NodeBox))
		{
			FBox2D Intersection(
				FVector2D(FMath::Max(CandAABB.Min.X, NodeBox.Min.X),
				          FMath::Max(CandAABB.Min.Y, NodeBox.Min.Y)),
				FVector2D(FMath::Min(CandAABB.Max.X, NodeBox.Max.X),
				          FMath::Min(CandAABB.Max.Y, NodeBox.Max.Y))
			);
			FVector2D Extent = Intersection.GetExtent() * 2.f;
			TotalOverlap += Extent.X * Extent.Y;
		}
	}
	return TotalOverlap;
}

float FBALStyleVoter::MeasureWireCost(EBALPureDir Dir,
                                      const TArray<FBALNode*>& PureGroup,
                                      const FBALNode& Consumer)
{
	// Average Manhattan distance from the center of the Pure group's output side
	// to the center of Consumer's input side — depends on direction
	float TotalH = 0.f;
	for (const FBALNode* P : PureGroup) TotalH += P->Size.Y;

	FVector2D GroupCenter = FVector2D::ZeroVector;
	FVector2D ConsumerInput = Consumer.OutPos + FVector2D(0.f, Consumer.Size.Y * 0.5f);

	switch (Dir)
	{
	case EBALPureDir::West:
		GroupCenter = FVector2D(Consumer.OutPos.X - 100.f,
		                        Consumer.OutPos.Y + Consumer.Size.Y * 0.5f);
		break;
	case EBALPureDir::East:
		GroupCenter = FVector2D(Consumer.OutPos.X + Consumer.Size.X + 100.f,
		                        Consumer.OutPos.Y + Consumer.Size.Y * 0.5f);
		break;
	case EBALPureDir::North:
		GroupCenter = FVector2D(Consumer.OutPos.X + Consumer.Size.X * 0.5f,
		                        Consumer.OutPos.Y - 100.f);
		break;
	case EBALPureDir::South:
		GroupCenter = FVector2D(Consumer.OutPos.X + Consumer.Size.X * 0.5f,
		                        Consumer.OutPos.Y + Consumer.Size.Y + 100.f);
		break;
	}

	return FMath::Abs(GroupCenter.X - ConsumerInput.X) +
	       FMath::Abs(GroupCenter.Y - ConsumerInput.Y);
}

float FBALStyleVoter::MeasureAxisCross(const FBox2D& CandAABB,
                                       const TArray<FBALExecNode*>& ExecRoots)
{
	// Exec chain runs horizontally — compute average Y of all exec nodes
	float SumY = 0.f;
	int32 Count = 0;

	TFunction<void(const FBALExecNode*)> Collect = [&](const FBALExecNode* EN)
	{
		if (!EN) return;
		SumY += EN->Proxy->OutPos.Y;
		Count++;
		for (const FBALExecNode* Child : EN->Children) Collect(Child);
	};
	for (const FBALExecNode* Root : ExecRoots) Collect(Root);

	if (Count == 0) return 0.f;

	float AxisY = SumY / (float)Count;

	// Cost = how much of the candidate AABB crosses the horizontal exec axis
	if (AxisY >= CandAABB.Min.Y && AxisY <= CandAABB.Max.Y)
	{
		float CrossH = FMath::Min(AxisY - CandAABB.Min.Y, CandAABB.Max.Y - AxisY);
		return CrossH * CandAABB.GetExtent().X * 2.f;
	}
	return 0.f;
}
