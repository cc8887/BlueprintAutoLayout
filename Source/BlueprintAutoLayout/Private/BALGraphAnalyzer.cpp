// BALGraphAnalyzer.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALGraphAnalyzer.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"

namespace
{
	template<typename ElementType>
	ElementType GraphAnalyzerPopNoShrink(TArray<ElementType>& Items)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
		return Items.Pop(EAllowShrinking::No);
#else
		return Items.Pop(false);
#endif
	}

	int32 ConstraintPriority(EBALConstraintType Type)
	{
		switch (Type)
		{
		case EBALConstraintType::Hard: return 3;
		case EBALConstraintType::RigidGroup: return 2;
		case EBALConstraintType::Soft: return 1;
		default: return 0;
		}
	}

	int32 PinIndex(const UEdGraphPin* Pin)
	{
		if (!Pin || !Pin->GetOwningNode())
		{
			return 0;
		}

		int32 Index = 0;
		for (const UEdGraphPin* Candidate : Pin->GetOwningNode()->Pins)
		{
			if (!Candidate || Candidate->Direction != Pin->Direction)
			{
				continue;
			}
			if (Candidate == Pin)
			{
				return Index;
			}
			++Index;
		}
		return Index;
	}

	bool IsInsideComment(const FBALNode& Node, const FBALNode& Comment)
	{
		if (Node.Role == EBALNodeRole::Comment)
		{
			const FVector2D NodeMax = Node.OriginalPos + Node.OriginalSize;
			const FVector2D CommentMax = Comment.OriginalPos + Comment.OriginalSize;
			return Node.OriginalPos.X >= Comment.OriginalPos.X
				&& Node.OriginalPos.Y >= Comment.OriginalPos.Y
				&& NodeMax.X <= CommentMax.X
				&& NodeMax.Y <= CommentMax.Y;
		}
		const FVector2D Center = Node.OriginalPos + Node.OriginalSize * 0.5f;
		return Center.X >= Comment.OriginalPos.X
			&& Center.X <= Comment.OriginalPos.X + Comment.OriginalSize.X
			&& Center.Y >= Comment.OriginalPos.Y
			&& Center.Y <= Comment.OriginalPos.Y + Comment.OriginalSize.Y;
	}

	void AddLogicalEdgesFromPin(
		UEdGraphNode* Source,
		UEdGraphPin* SourcePin,
		UEdGraphPin* CurrentOutput,
		const TMap<UEdGraphNode*, FBALNode>& Proxies,
		TSet<UEdGraphNode*>& VisitedKnots,
		TArray<FBALEdge>& OutEdges)
	{
		if (!Source || !SourcePin || !CurrentOutput)
		{
			return;
		}

		TArray<UEdGraphPin*> PendingOutputs;
		PendingOutputs.Add(CurrentOutput);
		while (PendingOutputs.Num() > 0)
		{
			UEdGraphPin* Output = GraphAnalyzerPopNoShrink(PendingOutputs);
			if (!Output) continue;
			for (UEdGraphPin* LinkedPin : Output->LinkedTo)
			{
				if (!LinkedPin) continue;

				UEdGraphNode* Target = LinkedPin->GetOwningNode();
				const FBALNode* TargetProxy = Proxies.Find(Target);
				if (!Target || !TargetProxy || TargetProxy->Role == EBALNodeRole::Comment)
				{
					continue;
				}

				if (TargetProxy->Role == EBALNodeRole::Knot)
				{
					if (VisitedKnots.Contains(Target)) continue;
					VisitedKnots.Add(Target);
					for (int32 PinIndex = Target->Pins.Num() - 1; PinIndex >= 0; --PinIndex)
					{
						UEdGraphPin* KnotPin = Target->Pins[PinIndex];
						if (KnotPin && KnotPin->Direction == EGPD_Output)
						{
							PendingOutputs.Add(KnotPin);
						}
					}
					continue;
				}

				const bool bDuplicate = OutEdges.ContainsByPredicate(
					[Source, Target, SourcePin, LinkedPin](const FBALEdge& Existing)
					{
						return Existing.Source == Source
							&& Existing.Target == Target
							&& Existing.SourcePin == SourcePin
							&& Existing.TargetPin == LinkedPin;
					});
				if (bDuplicate)
				{
					continue;
				}

				FBALEdge Edge;
				Edge.Source = Source;
				Edge.Target = Target;
				Edge.SourcePin = SourcePin;
				Edge.TargetPin = LinkedPin;
				Edge.Kind = FBALGraphAnalyzer::IsExecPin(SourcePin)
					? EBALEdgeKind::Exec
					: EBALEdgeKind::Data;
				Edge.SourcePinIndex = PinIndex(SourcePin);
				Edge.TargetPinIndex = PinIndex(LinkedPin);
				OutEdges.Add(Edge);
			}
		}
	}

	void MarkExecBackEdgesIterative(
		UEdGraphNode* Root,
		const TMap<UEdGraphNode*, TArray<int32>>& Outgoing,
		TArray<FBALEdge>& Edges,
		TMap<UEdGraphNode*, uint8>& Color)
	{
		struct FFrame
		{
			UEdGraphNode* Node;
			int32 NextEdge;
			FFrame(UEdGraphNode* InNode) : Node(InNode), NextEdge(0) {}
		};

		TArray<FFrame> Stack;
		Color.FindChecked(Root) = 1;
		Stack.Add(FFrame(Root));
		while (Stack.Num() > 0)
		{
			FFrame& Frame = Stack.Last();
			const TArray<int32>& NodeEdges = Outgoing.FindChecked(Frame.Node);
			if (Frame.NextEdge >= NodeEdges.Num())
			{
				Color.FindChecked(Frame.Node) = 2;
				GraphAnalyzerPopNoShrink(Stack);
				continue;
			}

			FBALEdge& Edge = Edges[NodeEdges[Frame.NextEdge++]];
			const uint8 TargetColor = Color.FindRef(Edge.Target);
			if (TargetColor == 1)
			{
				Edge.bBackEdge = true;
			}
			else if (TargetColor == 0)
			{
				Color.FindChecked(Edge.Target) = 1;
				Stack.Add(FFrame(Edge.Target));
			}
		}
	}

	float NodeDistance(const FBALNode& A, const FBALNode& B)
	{
		const FVector2D Delta = A.OriginalPos - B.OriginalPos;
		return FMath::Abs(Delta.X) + FMath::Abs(Delta.Y);
	}
}

FBALGraphAnalyzer::FAnalysisResult FBALGraphAnalyzer::Analyze(
	UEdGraph* Graph,
	const FBALSettings& Settings,
	const TArray<FBALConstraint>& Constraints)
{
	FAnalysisResult Result;
	if (!Graph)
	{
		return Result;
	}

	TMap<UEdGraphNode*, FBALConstraint> ConstraintMap;
	for (const FBALConstraint& Constraint : Constraints)
	{
		if (!Constraint.Node
			|| (Constraint.bCommentGroup && !Settings.bPreserveCommentContents))
		{
			continue;
		}
		FBALConstraint* Existing = ConstraintMap.Find(Constraint.Node);
		if (!Existing)
		{
			ConstraintMap.Add(Constraint.Node, Constraint);
		}
		else
		{
			FBALConstraint Merged = ConstraintPriority(Constraint.Type) > ConstraintPriority(Existing->Type)
				? Constraint : *Existing;
			const FBALConstraint& Other = ConstraintPriority(Constraint.Type) > ConstraintPriority(Existing->Type)
				? *Existing : Constraint;
			if (Merged.GroupId == INDEX_NONE && Other.GroupId != INDEX_NONE)
			{
				Merged.GroupId = Other.GroupId;
				Merged.bCommentGroup = Other.bCommentGroup;
			}
			ConstraintMap.Add(Constraint.Node, Merged);
		}
	}

	ClassifyNodes(Graph, Settings, ConstraintMap, Result.Proxies);
	BuildLogicalEdges(Graph, Result.Proxies, Result.Edges);
	BuildExecForest(Result.Proxies, Result.Edges, Result.ExecRoots, Result.ExecNodePool);
	AssignPureSubtrees(Result.Proxies, Result.Edges, Result.ExecRoots);
	BuildComponents(Result.Proxies, Result.Edges, Result.Components);
	BuildCommentHierarchy(Result.Proxies, Result.CommentGroups);

	for (TPair<UEdGraphNode*, FBALNode>& Pair : Result.Proxies)
	{
		FBALNode& Proxy = Pair.Value;
		if (Proxy.Role == EBALNodeRole::Isolated)
		{
			Result.IsolatedNodes.Add(&Proxy);
		}
		else if (Proxy.Role == EBALNodeRole::Pure && Proxy.PureOwner == nullptr)
		{
			Result.IsolatedPures.Add(&Proxy);
		}
	}

	return Result;
}

bool FBALGraphAnalyzer::HasExecPin(const UEdGraphNode* Node)
{
	if (!Node)
	{
		return false;
	}
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && IsExecPin(Pin))
		{
			return true;
		}
	}
	return false;
}

bool FBALGraphAnalyzer::IsPureNode(const UEdGraphNode* Node)
{
	return Node && !HasExecPin(Node);
}

bool FBALGraphAnalyzer::IsKnotNode(const UEdGraphNode* Node)
{
	if (!Node)
	{
		return false;
	}
	const FString ClassName = Node->GetClass()->GetName();
	return ClassName.Contains(TEXT("Knot")) || ClassName.Contains(TEXT("Reroute"));
}

bool FBALGraphAnalyzer::IsCommentNode(const UEdGraphNode* Node)
{
	return Node && Node->IsA<UEdGraphNode_Comment>();
}

FVector2D FBALGraphAnalyzer::EstimateNodeSize(const UEdGraphNode* Node)
{
	if (!Node)
	{
		return FVector2D(200.f, 80.f);
	}
	if (IsCommentNode(Node))
	{
		return FVector2D(
			static_cast<float>(FMath::Max(1, Node->NodeWidth)),
			static_cast<float>(FMath::Max(1, Node->NodeHeight)));
	}
	if (IsKnotNode(Node))
	{
		return FVector2D(24.f, 24.f);
	}
	if (Node->NodeWidth > 0 && Node->NodeHeight > 0)
	{
		return FVector2D(static_cast<float>(Node->NodeWidth), static_cast<float>(Node->NodeHeight));
	}

	int32 InputPins = 0;
	int32 OutputPins = 0;
	int32 MaxNameLength = Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Len();
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin)
		{
			continue;
		}
		if (Pin->Direction == EGPD_Input)
		{
			++InputPins;
		}
		else if (Pin->Direction == EGPD_Output)
		{
			++OutputPins;
		}
		MaxNameLength = FMath::Max(MaxNameLength, Pin->PinName.ToString().Len());
	}

	const float Width = FMath::Clamp(160.f + MaxNameLength * 4.f, 160.f, 420.f);
	const float Height = 48.f + FMath::Max(InputPins, OutputPins) * 24.f;
	return FVector2D(Width, Height);
}

bool FBALGraphAnalyzer::IsExecPin(const UEdGraphPin* Pin)
{
	return Pin && Pin->PinType.PinCategory == FName(TEXT("exec"));
}

void FBALGraphAnalyzer::ClassifyNodes(
	UEdGraph* Graph,
	const FBALSettings& Settings,
	const TMap<UEdGraphNode*, FBALConstraint>& ConstraintMap,
	TMap<UEdGraphNode*, FBALNode>& OutProxies)
{
	for (int32 Index = 0; Index < Graph->Nodes.Num(); ++Index)
	{
		UEdGraphNode* Node = Graph->Nodes[Index];
		if (!Node)
		{
			continue;
		}

		FBALNode Proxy;
		Proxy.GraphNode = Node;
		Proxy.StableIndex = Index;
		Proxy.OriginalPos = FVector2D(static_cast<float>(Node->NodePosX), static_cast<float>(Node->NodePosY));
		Proxy.OutPos = Proxy.OriginalPos;
		Proxy.Size = EstimateNodeSize(Node);
		Proxy.OriginalSize = Proxy.Size;

		if (const FBALConstraint* Constraint = ConstraintMap.Find(Node))
		{
			Proxy.bConstrained = true;
			Proxy.ConstraintType = Constraint->Type;
			Proxy.MaxDrift = Constraint->MaxDrift;
			Proxy.GroupId = Constraint->GroupId;
			Proxy.bCommentGroup = Constraint->bCommentGroup;
			if (Constraint->Type != EBALConstraintType::Hard)
			{
				Proxy.OriginalPos = Constraint->OriginalPos;
				Proxy.OutPos = Proxy.OriginalPos;
			}
			Proxy.bLocked = Constraint->Type == EBALConstraintType::Hard
				|| (Constraint->Type == EBALConstraintType::RigidGroup
					&& Constraint->bCommentGroup && Settings.bPreserveCommentContents);
		}

		if (IsCommentNode(Node))
		{
			Proxy.Role = EBALNodeRole::Comment;
		}
		else if (IsKnotNode(Node))
		{
			Proxy.Role = EBALNodeRole::Knot;
		}
		else if (HasExecPin(Node))
		{
			Proxy.Role = EBALNodeRole::Exec;
		}
		else
		{
			bool bConnected = false;
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->LinkedTo.Num() > 0)
				{
					bConnected = true;
					break;
				}
			}
			Proxy.Role = bConnected ? EBALNodeRole::Pure : EBALNodeRole::Isolated;
		}

		OutProxies.Add(Node, Proxy);
	}
}

void FBALGraphAnalyzer::BuildLogicalEdges(
	UEdGraph* Graph,
	const TMap<UEdGraphNode*, FBALNode>& Proxies,
	TArray<FBALEdge>& OutEdges)
{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		const FBALNode* Proxy = Proxies.Find(Node);
		if (!Node || !Proxy || Proxy->Role == EBALNodeRole::Comment || Proxy->Role == EBALNodeRole::Knot)
		{
			continue;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output || Pin->LinkedTo.Num() == 0)
			{
				continue;
			}
			TSet<UEdGraphNode*> VisitedKnots;
			AddLogicalEdgesFromPin(Node, Pin, Pin, Proxies, VisitedKnots, OutEdges);
		}
	}
}

void FBALGraphAnalyzer::BuildExecForest(
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	TArray<FBALEdge>& Edges,
	TArray<FBALExecNode*>& OutRoots,
	TArray<TUniquePtr<FBALExecNode>>& Pool)
{
	TArray<UEdGraphNode*> ExecNodes;
	for (TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		if (Pair.Value.Role == EBALNodeRole::Exec)
		{
			ExecNodes.Add(Pair.Key);
		}
	}
	ExecNodes.Sort([&Proxies](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		return Proxies.FindChecked(const_cast<UEdGraphNode*>(&A)).StableIndex
			< Proxies.FindChecked(const_cast<UEdGraphNode*>(&B)).StableIndex;
	});
	if (ExecNodes.Num() == 0)
	{
		return;
	}

	TMap<UEdGraphNode*, TArray<int32>> Outgoing;
	TMap<UEdGraphNode*, uint8> Color;
	for (UEdGraphNode* Node : ExecNodes)
	{
		Outgoing.Add(Node, TArray<int32>());
		Color.Add(Node, 0);
		Proxies.FindChecked(Node).Layer = 0;
	}
	for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); ++EdgeIndex)
	{
		FBALEdge& Edge = Edges[EdgeIndex];
		if (Edge.Kind != EBALEdgeKind::Exec
			|| !Outgoing.Contains(Edge.Source)
			|| !Outgoing.Contains(Edge.Target))
		{
			continue;
		}
		Outgoing.FindChecked(Edge.Source).Add(EdgeIndex);
	}
	for (TPair<UEdGraphNode*, TArray<int32>>& Pair : Outgoing)
	{
		Pair.Value.Sort([&Edges, &Proxies](int32 AIndex, int32 BIndex)
		{
			const FBALEdge& A = Edges[AIndex];
			const FBALEdge& B = Edges[BIndex];
			if (A.SourcePinIndex != B.SourcePinIndex)
			{
				return A.SourcePinIndex < B.SourcePinIndex;
			}
			const FBALNode& AP = Proxies.FindChecked(A.Target);
			const FBALNode& BP = Proxies.FindChecked(B.Target);
			if (!FMath::IsNearlyEqual(AP.OriginalPos.Y, BP.OriginalPos.Y))
			{
				return AP.OriginalPos.Y < BP.OriginalPos.Y;
			}
			return AP.StableIndex < BP.StableIndex;
		});
	}

	for (UEdGraphNode* Node : ExecNodes)
	{
		if (Color.FindRef(Node) == 0)
		{
			MarkExecBackEdgesIterative(Node, Outgoing, Edges, Color);
		}
	}

	TMap<UEdGraphNode*, int32> InDegree;
	for (UEdGraphNode* Node : ExecNodes)
	{
		InDegree.Add(Node, 0);
	}
	for (const FBALEdge& Edge : Edges)
	{
		if (Edge.Kind == EBALEdgeKind::Exec && !Edge.bBackEdge && InDegree.Contains(Edge.Target))
		{
			++InDegree.FindChecked(Edge.Target);
		}
	}

	TArray<UEdGraphNode*> Ready;
	for (UEdGraphNode* Node : ExecNodes)
	{
		if (InDegree.FindRef(Node) == 0)
		{
			Ready.Add(Node);
		}
	}
	while (Ready.Num() > 0)
	{
		Ready.Sort([&Proxies](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			return Proxies.FindChecked(const_cast<UEdGraphNode*>(&A)).StableIndex
				< Proxies.FindChecked(const_cast<UEdGraphNode*>(&B)).StableIndex;
		});
		UEdGraphNode* Node = Ready[0];
		Ready.RemoveAt(0);
		for (int32 EdgeIndex : Outgoing.FindChecked(Node))
		{
			const FBALEdge& Edge = Edges[EdgeIndex];
			if (Edge.bBackEdge)
			{
				continue;
			}
			FBALNode& TargetProxy = Proxies.FindChecked(Edge.Target);
			TargetProxy.Layer = FMath::Max(TargetProxy.Layer, Proxies.FindChecked(Node).Layer + 1);
			int32& Degree = InDegree.FindChecked(Edge.Target);
			--Degree;
			if (Degree == 0)
			{
				Ready.Add(Edge.Target);
			}
		}
	}

	TMap<UEdGraphNode*, FBALExecNode*> ExecNodeMap;
	for (UEdGraphNode* Node : ExecNodes)
	{
		TUniquePtr<FBALExecNode> ExecNode = MakeUnique<FBALExecNode>();
		ExecNode->Proxy = &Proxies.FindChecked(Node);
		ExecNode->Proxy->ExecDepth = ExecNode->Proxy->Layer;
		ExecNodeMap.Add(Node, ExecNode.Get());
		Pool.Add(MoveTemp(ExecNode));
	}

	TMap<UEdGraphNode*, int32> ParentEdgeByTarget;
	for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); ++EdgeIndex)
	{
		FBALEdge& Edge = Edges[EdgeIndex];
		if (Edge.Kind != EBALEdgeKind::Exec || Edge.bBackEdge
			|| !ExecNodeMap.Contains(Edge.Source) || !ExecNodeMap.Contains(Edge.Target))
		{
			continue;
		}

		const int32* ExistingIndex = ParentEdgeByTarget.Find(Edge.Target);
		bool bTake = ExistingIndex == nullptr;
		if (ExistingIndex)
		{
			const FBALEdge& Existing = Edges[*ExistingIndex];
			const int32 CandidateLayer = Proxies.FindChecked(Edge.Source).Layer;
			const int32 ExistingLayer = Proxies.FindChecked(Existing.Source).Layer;
			bTake = CandidateLayer > ExistingLayer
				|| (CandidateLayer == ExistingLayer && Edge.TargetPinIndex < Existing.TargetPinIndex)
				|| (CandidateLayer == ExistingLayer && Edge.TargetPinIndex == Existing.TargetPinIndex
					&& Proxies.FindChecked(Edge.Source).StableIndex < Proxies.FindChecked(Existing.Source).StableIndex);
		}
		if (bTake)
		{
			ParentEdgeByTarget.Add(Edge.Target, EdgeIndex);
		}
	}

	for (TPair<UEdGraphNode*, int32>& Pair : ParentEdgeByTarget)
	{
		FBALEdge& Edge = Edges[Pair.Value];
		Edge.bPrimary = true;
		FBALExecNode* Parent = ExecNodeMap.FindChecked(Edge.Source);
		FBALExecNode* Child = ExecNodeMap.FindChecked(Edge.Target);
		Parent->Children.Add(Child);
		Child->Parent = Parent;
	}
	for (TPair<UEdGraphNode*, FBALExecNode*>& Pair : ExecNodeMap)
	{
		FBALExecNode* Parent = Pair.Value;
		Parent->Children.Sort([&Edges, Parent](const FBALExecNode& A, const FBALExecNode& B)
		{
			int32 APin = MAX_int32;
			int32 BPin = MAX_int32;
			for (const FBALEdge& Edge : Edges)
			{
				if (!Edge.bPrimary || Edge.Source != Parent->Proxy->GraphNode)
				{
					continue;
				}
				if (Edge.Target == A.Proxy->GraphNode) APin = Edge.SourcePinIndex;
				if (Edge.Target == B.Proxy->GraphNode) BPin = Edge.SourcePinIndex;
			}
			if (APin != BPin) return APin < BPin;
			if (!FMath::IsNearlyEqual(A.Proxy->OriginalPos.Y, B.Proxy->OriginalPos.Y))
			{
				return A.Proxy->OriginalPos.Y < B.Proxy->OriginalPos.Y;
			}
			return A.Proxy->StableIndex < B.Proxy->StableIndex;
		});
		for (int32 ChildIndex = 0; ChildIndex < Parent->Children.Num(); ++ChildIndex)
		{
			Parent->Children[ChildIndex]->Proxy->BranchIndex = ChildIndex;
		}
		if (!Parent->Parent)
		{
			OutRoots.Add(Parent);
		}
	}
	OutRoots.Sort([](const FBALExecNode& A, const FBALExecNode& B)
	{
		if (!FMath::IsNearlyEqual(A.Proxy->OriginalPos.Y, B.Proxy->OriginalPos.Y))
		{
			return A.Proxy->OriginalPos.Y < B.Proxy->OriginalPos.Y;
		}
		return A.Proxy->StableIndex < B.Proxy->StableIndex;
	});
}

void FBALGraphAnalyzer::AssignPureSubtrees(
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	const TArray<FBALEdge>& Edges,
	TArray<FBALExecNode*>& ExecRoots)
{
	TMap<UEdGraphNode*, FBALExecNode*> ExecNodeMap;
	TSet<FBALExecNode*> SeenExecNodes;
	TArray<FBALExecNode*> PendingExecNodes;
	for (int32 RootIndex = ExecRoots.Num() - 1; RootIndex >= 0; --RootIndex)
	{
		PendingExecNodes.Add(ExecRoots[RootIndex]);
	}
	while (PendingExecNodes.Num() > 0)
	{
		FBALExecNode* ExecNode = GraphAnalyzerPopNoShrink(PendingExecNodes);
		if (!ExecNode || SeenExecNodes.Contains(ExecNode)) continue;
		SeenExecNodes.Add(ExecNode);
		ExecNodeMap.Add(ExecNode->Proxy->GraphNode, ExecNode);
		for (int32 ChildIndex = ExecNode->Children.Num() - 1; ChildIndex >= 0; --ChildIndex)
		{
			PendingExecNodes.Add(ExecNode->Children[ChildIndex]);
		}
	}

	TMap<UEdGraphNode*, TArray<int32>> IncomingData;
	for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); ++EdgeIndex)
	{
		const FBALEdge& Edge = Edges[EdgeIndex];
		if (Edge.Kind == EBALEdgeKind::Data)
		{
			IncomingData.FindOrAdd(Edge.Target).Add(EdgeIndex);
		}
	}

	struct FOwnerCandidate
	{
		UEdGraphNode* Owner;
		int32 Distance;
		int32 ExecDepth;
		float OriginalDistance;
		FOwnerCandidate()
			: Owner(nullptr), Distance(MAX_int32), ExecDepth(MAX_int32), OriginalDistance(MAX_flt)
		{
		}
	};
	TMap<UEdGraphNode*, FOwnerCandidate> Candidates;

	for (TPair<UEdGraphNode*, FBALExecNode*>& ExecPair : ExecNodeMap)
	{
		UEdGraphNode* Consumer = ExecPair.Key;
		TArray<TPair<UEdGraphNode*, int32>> Queue;
		TMap<UEdGraphNode*, int32> BestDistance;
		Queue.Add(TPair<UEdGraphNode*, int32>(Consumer, 0));
		int32 Head = 0;
		while (Head < Queue.Num())
		{
			const TPair<UEdGraphNode*, int32> Current = Queue[Head++];
			const TArray<int32>* Incoming = IncomingData.Find(Current.Key);
			if (!Incoming)
			{
				continue;
			}
			for (int32 EdgeIndex : *Incoming)
			{
				UEdGraphNode* Source = Edges[EdgeIndex].Source;
				FBALNode* SourceProxy = Proxies.Find(Source);
				if (!SourceProxy || SourceProxy->Role != EBALNodeRole::Pure)
				{
					continue;
				}
				const int32 Distance = Current.Value + 1;
				const int32* PreviousDistance = BestDistance.Find(Source);
				if (PreviousDistance && *PreviousDistance <= Distance)
				{
					continue;
				}
				BestDistance.Add(Source, Distance);
				Queue.Add(TPair<UEdGraphNode*, int32>(Source, Distance));

				FOwnerCandidate& Candidate = Candidates.FindOrAdd(Source);
				const FBALNode& ConsumerProxy = Proxies.FindChecked(Consumer);
				const int32 ExecDepth = ConsumerProxy.ExecDepth;
				const float OriginalDistance = NodeDistance(*SourceProxy, ConsumerProxy);
				const bool bBetter = Distance < Candidate.Distance
					|| (Distance == Candidate.Distance && ExecDepth < Candidate.ExecDepth)
					|| (Distance == Candidate.Distance && ExecDepth == Candidate.ExecDepth
						&& OriginalDistance < Candidate.OriginalDistance)
					|| (Distance == Candidate.Distance && ExecDepth == Candidate.ExecDepth
						&& FMath::IsNearlyEqual(OriginalDistance, Candidate.OriginalDistance)
						&& Candidate.Owner
						&& ConsumerProxy.StableIndex < Proxies.FindChecked(Candidate.Owner).StableIndex);
				if (!Candidate.Owner || bBetter)
				{
					Candidate.Owner = Consumer;
					Candidate.Distance = Distance;
					Candidate.ExecDepth = ExecDepth;
					Candidate.OriginalDistance = OriginalDistance;
				}
			}
		}
	}

	for (TPair<UEdGraphNode*, FOwnerCandidate>& Pair : Candidates)
	{
		FBALNode* PureProxy = Proxies.Find(Pair.Key);
		FBALExecNode** OwnerExec = ExecNodeMap.Find(Pair.Value.Owner);
		if (!PureProxy || !OwnerExec)
		{
			continue;
		}
		PureProxy->PureOwner = Pair.Value.Owner;
		(*OwnerExec)->PureGroup.Add(PureProxy);
	}

	for (TPair<UEdGraphNode*, FBALExecNode*>& ExecPair : ExecNodeMap)
	{
		UEdGraphNode* Consumer = ExecPair.Key;
		FBALExecNode* ExecNode = ExecPair.Value;
		TMap<UEdGraphNode*, int32> Depths;
		TArray<TPair<UEdGraphNode*, int32>> Queue;
		Queue.Add(TPair<UEdGraphNode*, int32>(Consumer, -1));
		int32 Head = 0;
		while (Head < Queue.Num())
		{
			const TPair<UEdGraphNode*, int32> Current = Queue[Head++];
			const TArray<int32>* Incoming = IncomingData.Find(Current.Key);
			if (!Incoming)
			{
				continue;
			}
			for (int32 EdgeIndex : *Incoming)
			{
				UEdGraphNode* Source = Edges[EdgeIndex].Source;
				FBALNode* SourceProxy = Proxies.Find(Source);
				if (!SourceProxy || SourceProxy->Role != EBALNodeRole::Pure || SourceProxy->PureOwner != Consumer)
				{
					continue;
				}
				const int32 Depth = Current.Value + 1;
				const int32* Existing = Depths.Find(Source);
				if (Existing && *Existing <= Depth)
				{
					continue;
				}
				Depths.Add(Source, Depth);
				Queue.Add(TPair<UEdGraphNode*, int32>(Source, Depth));
			}
		}

		for (FBALNode* Pure : ExecNode->PureGroup)
		{
			Pure->PureDepth = Depths.FindRef(Pure->GraphNode);
		}
		ExecNode->PureGroup.Sort([](const FBALNode& A, const FBALNode& B)
		{
			if (A.PureDepth != B.PureDepth) return A.PureDepth < B.PureDepth;
			if (!FMath::IsNearlyEqual(A.OriginalPos.Y, B.OriginalPos.Y)) return A.OriginalPos.Y < B.OriginalPos.Y;
			return A.StableIndex < B.StableIndex;
		});
		TMap<int32, int32> SlotByDepth;
		for (FBALNode* Pure : ExecNode->PureGroup)
		{
			Pure->PureSlot = SlotByDepth.FindOrAdd(Pure->PureDepth)++;
		}
	}
}

void FBALGraphAnalyzer::BuildComponents(
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	const TArray<FBALEdge>& Edges,
	TArray<FBALComponent>& OutComponents)
{
	TMap<UEdGraphNode*, int32> LogicalInDegree;
	TMap<UEdGraphNode*, int32> ExecInDegree;
	for (const FBALEdge& Edge : Edges)
	{
		if (!Edge.bBackEdge)
		{
			++LogicalInDegree.FindOrAdd(Edge.Target);
			if (Edge.Kind == EBALEdgeKind::Exec)
			{
				++ExecInDegree.FindOrAdd(Edge.Target);
			}
		}
	}

	TArray<UEdGraphNode*> OrderedNodes;
	for (TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		if (Pair.Value.Role != EBALNodeRole::Comment)
		{
			OrderedNodes.Add(Pair.Key);
		}
	}
	OrderedNodes.Sort([&Proxies](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		return Proxies.FindChecked(const_cast<UEdGraphNode*>(&A)).StableIndex
			< Proxies.FindChecked(const_cast<UEdGraphNode*>(&B)).StableIndex;
	});

	TSet<UEdGraphNode*> Visited;
	for (UEdGraphNode* Start : OrderedNodes)
	{
		if (Visited.Contains(Start))
		{
			continue;
		}

		FBALComponent Component;
		Component.Id = OutComponents.Num();
		TArray<UEdGraphNode*> Queue;
		Queue.Add(Start);
		Visited.Add(Start);
		int32 Head = 0;
		while (Head < Queue.Num())
		{
			UEdGraphNode* Node = Queue[Head++];
			Component.Nodes.Add(Node);
			Proxies.FindChecked(Node).ComponentId = Component.Id;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) continue;
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					UEdGraphNode* Other = Linked ? Linked->GetOwningNode() : nullptr;
					FBALNode* OtherProxy = Proxies.Find(Other);
					if (!OtherProxy || OtherProxy->Role == EBALNodeRole::Comment || Visited.Contains(Other))
					{
						continue;
					}
					Visited.Add(Other);
					Queue.Add(Other);
				}
			}
		}

		Component.Nodes.Sort([&Proxies](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			return Proxies.FindChecked(const_cast<UEdGraphNode*>(&A)).StableIndex
				< Proxies.FindChecked(const_cast<UEdGraphNode*>(&B)).StableIndex;
		});

		int32 BestClass = MAX_int32;
		float BestX = MAX_flt;
		float BestY = MAX_flt;
		int32 BestStable = MAX_int32;
		for (UEdGraphNode* Candidate : Component.Nodes)
		{
			const FBALNode& Proxy = Proxies.FindChecked(Candidate);
			int32 CandidateClass = 4;
			if (Proxy.bLocked) CandidateClass = 0;
			else if (Proxy.Role == EBALNodeRole::Exec && ExecInDegree.FindRef(Candidate) == 0) CandidateClass = 1;
			else if (LogicalInDegree.FindRef(Candidate) == 0) CandidateClass = 2;
			else if (Proxy.Role == EBALNodeRole::Exec) CandidateClass = 3;
			const bool bBetter = CandidateClass < BestClass
				|| (CandidateClass == BestClass && Proxy.OriginalPos.X < BestX)
				|| (CandidateClass == BestClass && FMath::IsNearlyEqual(Proxy.OriginalPos.X, BestX) && Proxy.OriginalPos.Y < BestY)
				|| (CandidateClass == BestClass && FMath::IsNearlyEqual(Proxy.OriginalPos.X, BestX)
					&& FMath::IsNearlyEqual(Proxy.OriginalPos.Y, BestY) && Proxy.StableIndex < BestStable);
			if (bBetter)
			{
				Component.Anchor = Candidate;
				Component.OriginalAnchor = Proxy.OriginalPos;
				Component.bHasHardAnchor = Proxy.bLocked;
				BestClass = CandidateClass;
				BestX = Proxy.OriginalPos.X;
				BestY = Proxy.OriginalPos.Y;
				BestStable = Proxy.StableIndex;
			}
		}
		OutComponents.Add(Component);
	}
}

void FBALGraphAnalyzer::BuildCommentHierarchy(
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	TArray<FBALCommentGroup>& OutGroups)
{
	TArray<UEdGraphNode*> Comments;
	for (TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		if (Pair.Value.Role == EBALNodeRole::Comment)
		{
			Comments.Add(Pair.Key);
			FBALCommentGroup Group;
			Group.Comment = Pair.Key;
			OutGroups.Add(Group);
		}
	}
	if (Comments.Num() == 0)
	{
		return;
	}
	Comments.Sort([&Proxies](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		return Proxies.FindChecked(const_cast<UEdGraphNode*>(&A)).StableIndex
			< Proxies.FindChecked(const_cast<UEdGraphNode*>(&B)).StableIndex;
	});
	OutGroups.Sort([&Proxies](const FBALCommentGroup& A, const FBALCommentGroup& B)
	{
		return Proxies.FindChecked(A.Comment).StableIndex
			< Proxies.FindChecked(B.Comment).StableIndex;
	});

	TMap<UEdGraphNode*, int32> GroupIndex;
	for (int32 Index = 0; Index < OutGroups.Num(); ++Index)
	{
		GroupIndex.Add(OutGroups[Index].Comment, Index);
	}

	for (TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		UEdGraphNode* Node = Pair.Key;
		FBALNode& Proxy = Pair.Value;
		UEdGraphNode* BestComment = nullptr;
		float BestArea = MAX_flt;
		int32 BestStableIndex = MAX_int32;
		const float NodeArea = Proxy.OriginalSize.X * Proxy.OriginalSize.Y;
		for (UEdGraphNode* CommentNode : Comments)
		{
			if (CommentNode == Node)
			{
				continue;
			}
			const FBALNode& CommentProxy = Proxies.FindChecked(CommentNode);
			const float CommentArea = CommentProxy.OriginalSize.X * CommentProxy.OriginalSize.Y;
			if (Proxy.Role == EBALNodeRole::Comment && CommentArea <= NodeArea)
			{
				continue;
			}
			if (IsInsideComment(Proxy, CommentProxy)
				&& (CommentArea < BestArea
					|| (FMath::IsNearlyEqual(CommentArea, BestArea)
						&& CommentProxy.StableIndex < BestStableIndex)))
			{
				BestComment = CommentNode;
				BestArea = CommentArea;
				BestStableIndex = CommentProxy.StableIndex;
			}
		}

		Proxy.DirectComment = BestComment;
		if (BestComment)
		{
			OutGroups[GroupIndex.FindChecked(BestComment)].DirectMembers.Add(Node);
			if (Proxy.Role == EBALNodeRole::Comment)
			{
				OutGroups[GroupIndex.FindChecked(Node)].ParentComment = BestComment;
			}
		}
	}

	for (FBALCommentGroup& Group : OutGroups)
	{
		TSet<UEdGraphNode*> Seen;
		UEdGraphNode* Parent = Group.ParentComment;
		while (Parent && !Seen.Contains(Parent))
		{
			Seen.Add(Parent);
			++Group.Depth;
			const int32* ParentIndex = GroupIndex.Find(Parent);
			Parent = ParentIndex ? OutGroups[*ParentIndex].ParentComment : nullptr;
		}
		Proxies.FindChecked(Group.Comment).CommentDepth = Group.Depth;
	}
}
