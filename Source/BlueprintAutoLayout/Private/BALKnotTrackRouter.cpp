// BALKnotTrackRouter.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALKnotTrackRouter.h"

#include "BALGraphAnalyzer.h"
#include "BALTypes.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Knot.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "BlueprintAutoLayout"

namespace
{
	const FString AutoTrackTag(TEXT("BlueprintAutoLayout.AutoTrack"));

	bool IsAutoTrack(const UEdGraphNode* Node)
	{
		const UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Node);
		return Knot && Knot->NodeComment == AutoTrackTag;
	}

	bool IsPureNode(const UEdGraphNode* Node)
	{
		if (!Node || Cast<UK2Node_Knot>(Node)) return false;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				return false;
			}
		}
		return true;
	}

	float EstimatePinY(const UEdGraphPin* Pin)
	{
		const UEdGraphNode* Node = Pin ? Pin->GetOwningNode() : nullptr;
		if (!Node) return 0.f;
		int32 DirectionIndex = 0;
		for (const UEdGraphPin* Candidate : Node->Pins)
		{
			if (!Candidate || Candidate->Direction != Pin->Direction) continue;
			if (Candidate == Pin) break;
			++DirectionIndex;
		}
		return static_cast<float>(Node->NodePosY) + 32.f + DirectionIndex * 18.f;
	}

	void Link(UEdGraphPin* A, UEdGraphPin* B)
	{
		if (A && B && !A->LinkedTo.Contains(B))
		{
			A->MakeLinkTo(B);
		}
	}

	void CollectDownstreamPins(UK2Node_Knot* Start, TArray<UEdGraphPin*>& OutPins)
	{
		TArray<UK2Node_Knot*> Queue;
		TSet<UK2Node_Knot*> Visited;
		Queue.Add(Start);
		Visited.Add(Start);
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			UK2Node_Knot* Knot = Queue[Head];
			UEdGraphPin* Output = Knot ? Knot->GetOutputPin() : nullptr;
			if (!Output) continue;
			for (UEdGraphPin* Linked : Output->LinkedTo)
			{
				UK2Node_Knot* Next = Cast<UK2Node_Knot>(Linked ? Linked->GetOwningNode() : nullptr);
				if (Next && IsAutoTrack(Next))
				{
					if (!Visited.Contains(Next))
					{
						Visited.Add(Next);
						Queue.Add(Next);
					}
				}
				else if (Linked && Linked->Direction == EGPD_Input)
				{
					OutPins.AddUnique(Linked);
				}
			}
		}
	}

	void CollapseExistingTracks(UEdGraph* Graph, TArray<UK2Node_Knot*>& OutReusable)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Node);
			if (Knot && IsAutoTrack(Knot))
			{
				OutReusable.Add(Knot);
			}
		}
		OutReusable.Sort([](const UK2Node_Knot& A, const UK2Node_Knot& B)
		{
			return A.NodeGuid.ToString() < B.NodeGuid.ToString();
		});

		for (UK2Node_Knot* Knot : OutReusable)
		{
			UEdGraphPin* Input = Knot->GetInputPin();
			if (!Input) continue;
			for (UEdGraphPin* Linked : Input->LinkedTo)
			{
				if (!Linked || Linked->Direction != EGPD_Output || IsAutoTrack(Linked->GetOwningNode())) continue;
				TArray<UEdGraphPin*> Downstream;
				CollectDownstreamPins(Knot, Downstream);
				for (UEdGraphPin* Target : Downstream)
				{
					Link(Linked, Target);
				}
			}
		}
		for (UK2Node_Knot* Knot : OutReusable)
		{
			Knot->Modify();
			Knot->BreakAllNodeLinks();
		}
	}

	UK2Node_Knot* AcquireKnot(
		UEdGraph* Graph,
		TArray<UK2Node_Knot*>& Reusable,
		int32& ReuseIndex,
		TArray<UK2Node_Knot*>& Used)
	{
		UK2Node_Knot* Knot = nullptr;
		if (Reusable.IsValidIndex(ReuseIndex))
		{
			Knot = Reusable[ReuseIndex++];
		}
		else
		{
			Knot = NewObject<UK2Node_Knot>(Graph, NAME_None, RF_Transactional);
			Graph->AddNode(Knot, false, false);
			Knot->CreateNewGuid();
			Knot->PostPlacedNewNode();
			Knot->AllocateDefaultPins();
		}
		Knot->Modify();
		Knot->NodeComment = AutoTrackTag;
		Used.Add(Knot);
		return Knot;
	}

	struct FRouteCandidate
	{
		UEdGraphPin* Source = nullptr;
		TArray<UEdGraphPin*> Targets;
		float SourceX = 0.f;
		float SourceY = 0.f;
		float LastTargetX = 0.f;
		int32 StableNodeIndex = 0;
		int32 StablePinIndex = 0;
	};
}

int32 FBALKnotTrackRouter::RouteSharedPureOutputs(UEdGraph* Graph, const FBALSettings& Settings)
{
	if (!Graph || !Settings.bCreateKnotTracks) return 0;

	FScopedTransaction Transaction(LOCTEXT("RouteSharedPureOutputs", "Route Shared Blueprint Wires"));
	Graph->Modify();

	TArray<UK2Node_Knot*> Reusable;
	CollapseExistingTracks(Graph, Reusable);

	TArray<FRouteCandidate> Routes;
	int32 PureNodeCount = 0;
	int32 LinkedOutputCount = 0;
	int32 MaximumFanout = 0;
	float MaximumSpan = 0.f;
	for (int32 NodeIndex = 0; NodeIndex < Graph->Nodes.Num(); ++NodeIndex)
	{
		UEdGraphNode* Node = Graph->Nodes[NodeIndex];
		if (!IsPureNode(Node)) continue;
		++PureNodeCount;
		const FVector2D Size = FBALGraphAnalyzer::EstimateNodeSize(Node);
		for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
		{
			UEdGraphPin* Source = Node->Pins[PinIndex];
			if (!Source || Source->Direction != EGPD_Output
				|| Source->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
				|| Source->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
			{
				continue;
			}

			FRouteCandidate Route;
			Route.Source = Source;
			Route.SourceX = static_cast<float>(Node->NodePosX) + Size.X;
			Route.SourceY = EstimatePinY(Source);
			Route.StableNodeIndex = NodeIndex;
			Route.StablePinIndex = PinIndex;
			for (UEdGraphPin* Linked : Source->LinkedTo)
			{
				UEdGraphNode* TargetNode = Linked ? Linked->GetOwningNode() : nullptr;
				if (!Linked || Linked->Direction != EGPD_Input || !TargetNode || IsAutoTrack(TargetNode)) continue;
				const float TargetX = static_cast<float>(TargetNode->NodePosX);
				if (TargetX > Route.SourceX + Settings.KnotTrackMinSpan)
				{
					Route.Targets.Add(Linked);
					Route.LastTargetX = FMath::Max(Route.LastTargetX, TargetX);
				}
			}
			if (Source->LinkedTo.Num() > 0)
			{
				++LinkedOutputCount;
				MaximumFanout = FMath::Max(MaximumFanout, Source->LinkedTo.Num());
				MaximumSpan = FMath::Max(MaximumSpan, Route.LastTargetX - Route.SourceX);
			}
			const float Span = Route.LastTargetX - Route.SourceX;
			if (Route.Targets.Num() > 0
				&& (Route.Targets.Num() >= 2 || Span >= Settings.KnotTrackDistanceThreshold))
			{
				Routes.Add(MoveTemp(Route));
			}
		}
	}
	UE_LOG(LogTemp, Log,
		TEXT("BlueprintAutoLayout: Knot routing scan pure_nodes=%d linked_outputs=%d max_fanout=%d max_span=%.1f candidates=%d"),
		PureNodeCount, LinkedOutputCount, MaximumFanout, MaximumSpan, Routes.Num());

	Routes.Sort([](const FRouteCandidate& A, const FRouteCandidate& B)
	{
		if (A.SourceY != B.SourceY) return A.SourceY < B.SourceY;
		if (A.SourceX != B.SourceX) return A.SourceX < B.SourceX;
		if (A.StableNodeIndex != B.StableNodeIndex) return A.StableNodeIndex < B.StableNodeIndex;
		return A.StablePinIndex < B.StablePinIndex;
	});

	float SharedTop = MAX_flt;
	for (const FRouteCandidate& Route : Routes)
	{
		SharedTop = FMath::Min(SharedTop, static_cast<float>(Route.Source->GetOwningNode()->NodePosY));
	}
	const float TrackSpacing = FMath::Max(8.f, Settings.KnotTrackSpacing);
	const float TrackBaseY = SharedTop < MAX_flt * 0.5f
		? SharedTop - (Routes.Num() * TrackSpacing + TrackSpacing)
		: 0.f;

	int32 ReuseIndex = 0;
	int32 RoutedLinks = 0;
	TArray<UK2Node_Knot*> Used;
	for (int32 RouteIndex = 0; RouteIndex < Routes.Num(); ++RouteIndex)
	{
		FRouteCandidate& Route = Routes[RouteIndex];
		Route.Targets.Sort([](const UEdGraphPin& A, const UEdGraphPin& B)
		{
			const UEdGraphNode* NodeA = A.GetOwningNode();
			const UEdGraphNode* NodeB = B.GetOwningNode();
			if (NodeA->NodePosX != NodeB->NodePosX) return NodeA->NodePosX < NodeB->NodePosX;
			return NodeA->NodePosY < NodeB->NodePosY;
		});

		for (UEdGraphPin* Target : Route.Targets)
		{
			Route.Source->BreakLinkTo(Target);
		}

		const float TrackY = TrackBaseY + RouteIndex * TrackSpacing;
		const float FirstX = Route.SourceX + Settings.KnotTrackXOffset;
		UK2Node_Knot* Previous = AcquireKnot(Graph, Reusable, ReuseIndex, Used);
		Previous->NodePosX = FMath::RoundToInt(FirstX);
		Previous->NodePosY = FMath::RoundToInt(TrackY);
		Link(Route.Source, Previous->GetInputPin());

		float PreviousX = FirstX;
		for (UEdGraphPin* Target : Route.Targets)
		{
			const float TargetX = static_cast<float>(Target->GetOwningNode()->NodePosX)
				- Settings.KnotTrackXOffset;
			UK2Node_Knot* Drop = Previous;
			if (TargetX - PreviousX >= Settings.KnotTrackMergeDistance)
			{
				Drop = AcquireKnot(Graph, Reusable, ReuseIndex, Used);
				Drop->NodePosX = FMath::RoundToInt(TargetX);
				Drop->NodePosY = FMath::RoundToInt(TrackY);
				Link(Previous->GetOutputPin(), Drop->GetInputPin());
				Previous = Drop;
				PreviousX = TargetX;
			}
			Link(Drop->GetOutputPin(), Target);
			++RoutedLinks;
		}
	}
	for (UK2Node_Knot* Knot : Used)
	{
		Knot->PostReconstructNode();
	}

	for (int32 Index = ReuseIndex; Index < Reusable.Num(); ++Index)
	{
		UK2Node_Knot* Unused = Reusable[Index];
		Unused->BreakAllNodeLinks();
		Graph->RemoveNode(Unused);
	}

	if (RoutedLinks > 0 || Reusable.Num() > 0)
	{
		Graph->NotifyGraphChanged();
	}
	else
	{
		Transaction.Cancel();
	}
	return RoutedLinks;
}

#undef LOCTEXT_NAMESPACE
