// BALLayoutSolver.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALLayoutSolver.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

namespace
{
	template<typename ElementType>
	ElementType LayoutSolverPopNoShrink(TArray<ElementType>& Items)
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
		return Items.Pop(EAllowShrinking::No);
#else
		return Items.Pop(false);
#endif
	}

	bool IsMainDataNode(const FBALNode& Node)
	{
		return Node.Role == EBALNodeRole::Pure && Node.PureOwner == nullptr;
	}

	float EffectiveGap(float Requested, float Margin)
	{
		return FMath::Max(Requested, Margin * 2.f);
	}

	float Median(TArray<float>& Values, float Fallback)
	{
		if (Values.Num() == 0)
		{
			return Fallback;
		}
		Values.Sort();
		const int32 Middle = Values.Num() / 2;
		return Values.Num() % 2 == 0
			? (Values[Middle - 1] + Values[Middle]) * 0.5f
			: Values[Middle];
	}

	float EstimatePinOffsetLocal(const FBALNode& Node, const UEdGraphPin* Pin)
	{
		if (!Pin || !Node.GraphNode)
		{
			return Node.Size.Y * 0.5f;
		}
		int32 Count = 0;
		int32 Index = 0;
		for (const UEdGraphPin* Candidate : Node.GraphNode->Pins)
		{
			if (!Candidate || Candidate->Direction != Pin->Direction) continue;
			if (Candidate == Pin) Index = Count;
			++Count;
		}
		if (Count <= 0) return Node.Size.Y * 0.5f;
		const float Header = FMath::Min(36.f, Node.Size.Y * 0.35f);
		const float RowHeight = FMath::Max(18.f, (Node.Size.Y - Header) / static_cast<float>(Count));
		return FMath::Clamp(
			Header + (Index + 0.5f) * RowHeight,
			8.f,
			FMath::Max(8.f, Node.Size.Y - 8.f));
	}

	bool ContainsNode(const TSet<UEdGraphNode*>& Nodes, const FBALEdge& Edge)
	{
		return Nodes.Contains(Edge.Source) && Nodes.Contains(Edge.Target);
	}

	void MarkDataBackEdgesIterative(
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
				LayoutSolverPopNoShrink(Stack);
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

	void PlaceUnownedDataSinks(
		TMap<UEdGraphNode*, FBALNode>& Proxies,
		const TArray<FBALEdge>& Edges,
		const FBALSettings& Settings)
	{
		TMap<UEdGraphNode*, TArray<int32>> DataOutgoing;
		TMap<UEdGraphNode*, TArray<int32>> DataIncoming;
		TMap<UEdGraphNode*, TArray<int32>> ExternalIncoming;
		TMap<UEdGraphNode*, TArray<UEdGraphNode*>> WeakAdjacent;
		TArray<UEdGraphNode*> Seeds;
		TSet<UEdGraphNode*> SeedSet;
		for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); ++EdgeIndex)
		{
			const FBALEdge& Edge = Edges[EdgeIndex];
			const FBALNode* Source = Proxies.Find(Edge.Source);
			const FBALNode* Target = Proxies.Find(Edge.Target);
			if (Edge.Kind != EBALEdgeKind::Data || Edge.bBackEdge || !Source || !Target
				|| Target->Role != EBALNodeRole::Pure || Target->PureOwner != nullptr)
			{
				continue;
			}

			const bool bSourceIsUnowned =
				Source->Role == EBALNodeRole::Pure && Source->PureOwner == nullptr;
			if (bSourceIsUnowned)
			{
				DataOutgoing.FindOrAdd(Edge.Source).Add(EdgeIndex);
				DataIncoming.FindOrAdd(Edge.Target).Add(EdgeIndex);
				WeakAdjacent.FindOrAdd(Edge.Source).Add(Edge.Target);
				WeakAdjacent.FindOrAdd(Edge.Target).Add(Edge.Source);
			}
			else if (Source->Role == EBALNodeRole::Exec
				|| (Source->Role == EBALNodeRole::Pure && Source->PureOwner != nullptr))
			{
				ExternalIncoming.FindOrAdd(Edge.Target).Add(EdgeIndex);
				DataOutgoing.FindOrAdd(Edge.Target);
				DataIncoming.FindOrAdd(Edge.Target);
				WeakAdjacent.FindOrAdd(Edge.Target);
				if (!SeedSet.Contains(Edge.Target))
				{
					SeedSet.Add(Edge.Target);
					Seeds.Add(Edge.Target);
				}
			}
		}

		for (TPair<UEdGraphNode*, TArray<int32>>& Pair : DataOutgoing)
		{
			Pair.Value.Sort([&Edges, &Proxies](int32 AIndex, int32 BIndex)
			{
				const FBALEdge& A = Edges[AIndex];
				const FBALEdge& B = Edges[BIndex];
				const int32 AStable = Proxies.FindChecked(A.Target).StableIndex;
				const int32 BStable = Proxies.FindChecked(B.Target).StableIndex;
				if (AStable != BStable) return AStable < BStable;
				if (A.SourcePinIndex != B.SourcePinIndex) return A.SourcePinIndex < B.SourcePinIndex;
				if (A.TargetPinIndex != B.TargetPinIndex) return A.TargetPinIndex < B.TargetPinIndex;
				return AIndex < BIndex;
			});
		}
		for (TPair<UEdGraphNode*, TArray<int32>>& Pair : DataIncoming)
		{
			Pair.Value.Sort([&Edges, &Proxies](int32 AIndex, int32 BIndex)
			{
				const FBALEdge& A = Edges[AIndex];
				const FBALEdge& B = Edges[BIndex];
				const int32 AStable = Proxies.FindChecked(A.Source).StableIndex;
				const int32 BStable = Proxies.FindChecked(B.Source).StableIndex;
				if (AStable != BStable) return AStable < BStable;
				if (A.SourcePinIndex != B.SourcePinIndex) return A.SourcePinIndex < B.SourcePinIndex;
				if (A.TargetPinIndex != B.TargetPinIndex) return A.TargetPinIndex < B.TargetPinIndex;
				return AIndex < BIndex;
			});
		}
		for (TPair<UEdGraphNode*, TArray<int32>>& Pair : ExternalIncoming)
		{
			Pair.Value.Sort([&Edges, &Proxies](int32 AIndex, int32 BIndex)
			{
				const FBALEdge& A = Edges[AIndex];
				const FBALEdge& B = Edges[BIndex];
				const int32 AStable = Proxies.FindChecked(A.Source).StableIndex;
				const int32 BStable = Proxies.FindChecked(B.Source).StableIndex;
				if (AStable != BStable) return AStable < BStable;
				if (A.SourcePinIndex != B.SourcePinIndex) return A.SourcePinIndex < B.SourcePinIndex;
				if (A.TargetPinIndex != B.TargetPinIndex) return A.TargetPinIndex < B.TargetPinIndex;
				return AIndex < BIndex;
			});
		}
		for (TPair<UEdGraphNode*, TArray<UEdGraphNode*>>& Pair : WeakAdjacent)
		{
			Pair.Value.Sort([&Proxies](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				return Proxies.FindChecked(const_cast<UEdGraphNode*>(&A)).StableIndex
					< Proxies.FindChecked(const_cast<UEdGraphNode*>(&B)).StableIndex;
			});
		}
		Seeds.Sort([&Proxies](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			return Proxies.FindChecked(const_cast<UEdGraphNode*>(&A)).StableIndex
				< Proxies.FindChecked(const_cast<UEdGraphNode*>(&B)).StableIndex;
		});

		struct FSinkDfsFrame
		{
			UEdGraphNode* Node;
			int32 NextEdge;
			FSinkDfsFrame(UEdGraphNode* InNode) : Node(InNode), NextEdge(0) {}
		};
		TSet<int32> LocalBackEdges;
		TSet<UEdGraphNode*> AssignedToRegion;
		const float Gap = EffectiveGap(Settings.GapX, Settings.NodeMargin);
		for (UEdGraphNode* Seed : Seeds)
		{
			if (AssignedToRegion.Contains(Seed)) continue;

			TArray<UEdGraphNode*> RegionNodes;
			TArray<UEdGraphNode*> RegionQueue;
			TSet<UEdGraphNode*> RegionSet;
			RegionQueue.Add(Seed);
			RegionSet.Add(Seed);
			AssignedToRegion.Add(Seed);
			for (int32 Head = 0; Head < RegionQueue.Num(); ++Head)
			{
				UEdGraphNode* GraphNode = RegionQueue[Head];
				RegionNodes.Add(GraphNode);
				const TArray<UEdGraphNode*>* Adjacent = WeakAdjacent.Find(GraphNode);
				if (!Adjacent) continue;
				for (UEdGraphNode* Other : *Adjacent)
				{
					if (!RegionSet.Contains(Other))
					{
						RegionSet.Add(Other);
						AssignedToRegion.Add(Other);
						RegionQueue.Add(Other);
					}
				}
			}
			RegionNodes.Sort([&Proxies](const UEdGraphNode& A, const UEdGraphNode& B)
			{
				return Proxies.FindChecked(const_cast<UEdGraphNode*>(&A)).StableIndex
					< Proxies.FindChecked(const_cast<UEdGraphNode*>(&B)).StableIndex;
			});

			// Exec-containing components do not pass through LayoutDataComponent, so
			// classify data-cycle back edges locally before topological layering.
			TMap<UEdGraphNode*, uint8> Color;
			TArray<UEdGraphNode*> DfsRoots;
			for (UEdGraphNode* GraphNode : RegionNodes)
			{
				Color.Add(GraphNode, 0);
				if (SeedSet.Contains(GraphNode)) DfsRoots.Add(GraphNode);
			}
			for (UEdGraphNode* GraphNode : RegionNodes)
			{
				if (!SeedSet.Contains(GraphNode)) DfsRoots.Add(GraphNode);
			}
			for (UEdGraphNode* Root : DfsRoots)
			{
				if (Color.FindRef(Root) != 0) continue;
				TArray<FSinkDfsFrame> Stack;
				Color.FindChecked(Root) = 1;
				Stack.Add(FSinkDfsFrame(Root));
				while (Stack.Num() > 0)
				{
					FSinkDfsFrame& Frame = Stack.Last();
					const TArray<int32>* Outgoing = DataOutgoing.Find(Frame.Node);
					if (!Outgoing || Frame.NextEdge >= Outgoing->Num())
					{
						Color.FindChecked(Frame.Node) = 2;
						LayoutSolverPopNoShrink(Stack);
						continue;
					}

					const int32 EdgeIndex = (*Outgoing)[Frame.NextEdge++];
					UEdGraphNode* TargetNode = Edges[EdgeIndex].Target;
					if (!RegionSet.Contains(TargetNode)) continue;
					const uint8 TargetColor = Color.FindRef(TargetNode);
					if (TargetColor == 1)
					{
						LocalBackEdges.Add(EdgeIndex);
					}
					else if (TargetColor == 0)
					{
						Color.FindChecked(TargetNode) = 1;
						Stack.Add(FSinkDfsFrame(TargetNode));
					}
				}
			}

			TMap<UEdGraphNode*, int32> Depths;
			TMap<UEdGraphNode*, int32> InDegree;
			for (UEdGraphNode* GraphNode : RegionNodes)
			{
				Depths.Add(GraphNode, 0);
				InDegree.Add(GraphNode, 0);
				const TArray<int32>* Incoming = DataIncoming.Find(GraphNode);
				if (!Incoming) continue;
				for (int32 EdgeIndex : *Incoming)
				{
					if (!LocalBackEdges.Contains(EdgeIndex)
						&& RegionSet.Contains(Edges[EdgeIndex].Source))
					{
						++InDegree.FindChecked(GraphNode);
					}
				}
			}

			TArray<UEdGraphNode*> Ready;
			for (UEdGraphNode* GraphNode : RegionNodes)
			{
				if (InDegree.FindRef(GraphNode) == 0) Ready.Add(GraphNode);
			}
			TArray<UEdGraphNode*> TopologicalOrder;
			while (Ready.Num() > 0)
			{
				Ready.Sort([&Proxies](const UEdGraphNode& A, const UEdGraphNode& B)
				{
					return Proxies.FindChecked(const_cast<UEdGraphNode*>(&A)).StableIndex
						< Proxies.FindChecked(const_cast<UEdGraphNode*>(&B)).StableIndex;
				});
				TArray<UEdGraphNode*> NextReady;
				for (UEdGraphNode* SourceNode : Ready)
				{
					TopologicalOrder.Add(SourceNode);
					const TArray<int32>* Outgoing = DataOutgoing.Find(SourceNode);
					if (!Outgoing) continue;
					for (int32 EdgeIndex : *Outgoing)
					{
						const FBALEdge& Edge = Edges[EdgeIndex];
						if (LocalBackEdges.Contains(EdgeIndex) || !RegionSet.Contains(Edge.Target)) continue;
						int32& TargetDepth = Depths.FindChecked(Edge.Target);
						TargetDepth = FMath::Max(TargetDepth, Depths.FindRef(SourceNode) + 1);
						int32& Degree = InDegree.FindChecked(Edge.Target);
						--Degree;
						if (Degree == 0) NextReady.Add(Edge.Target);
					}
				}
				Ready = MoveTemp(NextReady);
			}

			auto EffectivePosition = [](const FBALNode& Node)
			{
				// Locked regions are restored after component anchoring. Keep every
				// relative placement in the current layout coordinate space so that
				// restoration translates the connected region exactly once.
				if (Node.bLocked) return Node.OutPos;
				if (!Node.bConstrained || Node.ConstraintType != EBALConstraintType::Soft
					|| Node.MaxDrift < 0.f)
				{
					return Node.OutPos;
				}
				const FVector2D Delta = Node.OutPos - Node.OriginalPos;
				const float Distance = Delta.Size();
				return Distance > Node.MaxDrift && Distance > KINDA_SMALL_NUMBER
					? Node.OriginalPos + Delta * (Node.MaxDrift / Distance)
					: Node.OutPos;
			};

			float RegionRootX = -MAX_flt;
			for (UEdGraphNode* GraphNode : RegionNodes)
			{
				const TArray<int32>* Incoming = ExternalIncoming.Find(GraphNode);
				if (!Incoming) continue;
				for (int32 EdgeIndex : *Incoming)
				{
					const FBALNode& Source = Proxies.FindChecked(Edges[EdgeIndex].Source);
					RegionRootX = FMath::Max(
						RegionRootX,
						EffectivePosition(Source).X + Source.Size.X + Gap);
				}
			}

			for (UEdGraphNode* GraphNode : TopologicalOrder)
			{
				FBALNode& Node = Proxies.FindChecked(GraphNode);
				float X = Depths.FindRef(GraphNode) == 0 ? RegionRootX : -MAX_flt;
				TArray<float> AlignedY;
				const TArray<int32>* Incoming = DataIncoming.Find(GraphNode);
				if (Incoming)
				{
					for (int32 EdgeIndex : *Incoming)
					{
						if (LocalBackEdges.Contains(EdgeIndex)) continue;
						const FBALEdge& Edge = Edges[EdgeIndex];
						const int32* SourceDepth = Depths.Find(Edge.Source);
						if (!SourceDepth || *SourceDepth >= Depths.FindRef(GraphNode)) continue;
						const FBALNode& Source = Proxies.FindChecked(Edge.Source);
						const FVector2D SourcePosition = EffectivePosition(Source);
						X = FMath::Max(X, SourcePosition.X + Source.Size.X + Gap);
						AlignedY.Add(SourcePosition.Y + EstimatePinOffsetLocal(Source, Edge.SourcePin)
							- EstimatePinOffsetLocal(Node, Edge.TargetPin));
					}
				}
				const TArray<int32>* External = ExternalIncoming.Find(GraphNode);
				if (External)
				{
					for (int32 EdgeIndex : *External)
					{
						const FBALEdge& Edge = Edges[EdgeIndex];
						const FBALNode& Source = Proxies.FindChecked(Edge.Source);
						const FVector2D SourcePosition = EffectivePosition(Source);
						X = FMath::Max(X, SourcePosition.X + Source.Size.X + Gap);
						AlignedY.Add(SourcePosition.Y + EstimatePinOffsetLocal(Source, Edge.SourcePin)
							- EstimatePinOffsetLocal(Node, Edge.TargetPin));
					}
				}
				if (!Node.bLocked && X > -MAX_flt * 0.5f)
				{
					Node.OutPos = FVector2D(X, Median(AlignedY, Node.OriginalPos.Y));
					Node.OutPos = EffectivePosition(Node);
				}
			}
		}
	}

	void AnchorFreeRegionsToLockedNodes(TMap<UEdGraphNode*, FBALNode>& Proxies)
	{
		TArray<FBALNode*> Ordered;
		for (TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
		{
			if (!Pair.Value.bLocked && Pair.Value.Role != EBALNodeRole::Comment)
			{
				Ordered.Add(&Pair.Value);
			}
		}
		Ordered.Sort([](const FBALNode& A, const FBALNode& B)
		{
			return A.StableIndex < B.StableIndex;
		});

		TSet<UEdGraphNode*> Visited;
		for (FBALNode* Start : Ordered)
		{
			if (!Start || Visited.Contains(Start->GraphNode)) continue;
			TArray<FBALNode*> Region;
			TArray<FBALNode*> Queue;
			Queue.Add(Start);
			Visited.Add(Start->GraphNode);
			FBALNode* BestLocked = nullptr;
			float BestDistance = MAX_flt;
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				FBALNode* Node = Queue[Head];
				Region.Add(Node);
				for (UEdGraphPin* Pin : Node->GraphNode->Pins)
				{
					if (!Pin) continue;
					for (UEdGraphPin* Linked : Pin->LinkedTo)
					{
						FBALNode* Other = Proxies.Find(Linked ? Linked->GetOwningNode() : nullptr);
						if (!Other || Other->Role == EBALNodeRole::Comment) continue;
						if (Other->bLocked)
						{
							const FVector2D Difference = Node->OriginalPos - Other->OriginalPos;
							const float Distance = FMath::Abs(Difference.X) + FMath::Abs(Difference.Y);
							if (!BestLocked || Distance < BestDistance
								|| (FMath::IsNearlyEqual(Distance, BestDistance)
									&& Other->StableIndex < BestLocked->StableIndex))
							{
								BestLocked = Other;
								BestDistance = Distance;
							}
						}
						else if (!Visited.Contains(Other->GraphNode))
						{
							Visited.Add(Other->GraphNode);
							Queue.Add(Other);
						}
					}
				}
			}
			if (BestLocked)
			{
				const FVector2D Delta = BestLocked->OriginalPos - BestLocked->OutPos;
				for (FBALNode* Node : Region) Node->OutPos += Delta;
			}
		}
	}
}

void FBALLayoutSolver::Solve(const FSolverInput& Input)
{
	if (!Input.Proxies || !Input.Edges || !Input.Components || !Input.Settings)
	{
		return;
	}

	TMap<UEdGraphNode*, FBALNode>& Proxies = *Input.Proxies;
	TArray<FBALEdge>& Edges = *Input.Edges;
	TArray<FBALComponent>& Components = *Input.Components;
	const FBALSettings& Settings = *Input.Settings;
	TMap<UEdGraphNode*, TArray<FBALNode*>> OwnedPuresByConsumer;
	for (TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		FBALNode& Node = Pair.Value;
		if (Node.Role == EBALNodeRole::Pure && Node.PureOwner != nullptr)
		{
			OwnedPuresByConsumer.FindOrAdd(Node.PureOwner).Add(&Node);
		}
	}

	for (FBALComponent& Component : Components)
	{
		bool bHasExec = false;
		bool bHasData = false;
		for (UEdGraphNode* Node : Component.Nodes)
		{
			const FBALNode& Proxy = Proxies.FindChecked(Node);
			bHasExec = bHasExec || Proxy.Role == EBALNodeRole::Exec;
			bHasData = bHasData || IsMainDataNode(Proxy);
		}

		if (bHasExec)
		{
			LayoutExecComponent(
				Component,
				Proxies,
				Edges,
				OwnedPuresByConsumer,
				Input.PureDir,
				Settings);
		}
		else if (bHasData)
		{
			LayoutDataComponent(Component, Proxies, Edges, Settings);
		}
	}

	PlacePureGroups(Proxies, Edges, OwnedPuresByConsumer, Input.PureDir, Settings);
	PlaceUnownedDataSinks(Proxies, Edges, Settings);
	PlaceKnotNodes(Proxies, Settings);
	PreserveComponentAnchors(Components, Proxies, Settings);
	PackOverlappingComponents(Components, Proxies, Settings);
	AnchorFreeRegionsToLockedNodes(Proxies);

	for (TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		FBALNode& Node = Pair.Value;
		if (Node.bLocked)
		{
			Node.OutPos = Node.OriginalPos;
		}
		else
		{
			ClampSoftConstraint(Node);
		}
	}

	// A caller-supplied rigid group follows the layout as one translated body.
	// Use the mean desired translation so no individual member is privileged.
	TMap<int32, FVector2D> TranslationSums;
	TMap<int32, int32> TranslationCounts;
	TSet<int32> LockedGroups;
	TArray<FBALNode*> RigidOrder;
	for (TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		if (Pair.Value.GroupId != INDEX_NONE)
		{
			RigidOrder.Add(&Pair.Value);
		}
	}
	RigidOrder.Sort([](const FBALNode& A, const FBALNode& B)
	{
		return A.StableIndex < B.StableIndex;
	});
	for (const FBALNode* NodePtr : RigidOrder)
	{
		const FBALNode& Node = *NodePtr;
		if (Node.bLocked && Node.GroupId != INDEX_NONE)
		{
			LockedGroups.Add(Node.GroupId);
		}
		if (Node.bLocked || !Node.bConstrained
			|| Node.ConstraintType != EBALConstraintType::RigidGroup
			|| Node.GroupId == INDEX_NONE)
		{
			continue;
		}
		const FVector2D Delta = Node.OutPos - Node.OriginalPos;
		if (FVector2D* Sum = TranslationSums.Find(Node.GroupId))
		{
			*Sum += Delta;
		}
		else
		{
			TranslationSums.Add(Node.GroupId, Delta);
		}
		++TranslationCounts.FindOrAdd(Node.GroupId);
	}
	for (TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		FBALNode& Node = Pair.Value;
		const int32 Count = TranslationCounts.FindRef(Node.GroupId);
		if (Node.GroupId != INDEX_NONE && LockedGroups.Contains(Node.GroupId))
		{
			Node.OutPos = Node.OriginalPos;
		}
		else if (!Node.bLocked && Node.bConstrained
			&& Node.ConstraintType == EBALConstraintType::RigidGroup
			&& Node.GroupId != INDEX_NONE && Count > 0)
		{
			Node.OutPos = Node.OriginalPos + TranslationSums.FindRef(Node.GroupId) / static_cast<float>(Count);
		}
	}
}

void FBALLayoutSolver::LayoutExecComponent(
	const FBALComponent& Component,
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	TArray<FBALEdge>& Edges,
	const TMap<UEdGraphNode*, TArray<FBALNode*>>& OwnedPuresByConsumer,
	EBALPureDir PureDir,
	const FBALSettings& Settings)
{
	int32 MaxLayer = 0;
	TArray<FBALNode*> ExecNodes;
	for (UEdGraphNode* Node : Component.Nodes)
	{
		FBALNode& Proxy = Proxies.FindChecked(Node);
		if (Proxy.Role == EBALNodeRole::Exec)
		{
			ExecNodes.Add(&Proxy);
			MaxLayer = FMath::Max(MaxLayer, Proxy.Layer);
		}
	}
	if (ExecNodes.Num() == 0)
	{
		return;
	}

	TArray<TArray<FBALNode*>> Layers;
	Layers.SetNum(MaxLayer + 1);
	for (FBALNode* Node : ExecNodes)
	{
		Layers[Node->Layer].Add(Node);
	}
	for (TArray<FBALNode*>& Layer : Layers)
	{
		Layer.Sort([](const FBALNode& A, const FBALNode& B)
		{
			if (A.OriginalPos.Y != B.OriginalPos.Y)
			{
				return A.OriginalPos.Y < B.OriginalPos.Y;
			}
			return A.StableIndex < B.StableIndex;
		});
	}
	ReduceCrossings(Layers, Edges, Settings);

	TMap<UEdGraphNode*, float> ClusterHeights;
	TArray<float> LayerWidths;
	TArray<float> LeftExtensions;
	TArray<float> RightExtensions;
	LayerWidths.SetNumZeroed(Layers.Num());
	LeftExtensions.SetNumZeroed(Layers.Num());
	RightExtensions.SetNumZeroed(Layers.Num());
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		for (FBALNode* Node : Layers[LayerIndex])
		{
			LayerWidths[LayerIndex] = FMath::Max(LayerWidths[LayerIndex], Node->Size.X);
			const TArray<FBALNode*>* OwnedPures = OwnedPuresByConsumer.Find(Node->GraphNode);
			const float ClusterHeight = OwnedPures
				? PureClusterHeight(*Node, *OwnedPures, PureDir, Settings)
				: Node->Size.Y;
			const float Extension = OwnedPures
				? PureLeftExtension(*OwnedPures, Settings)
				: 0.f;
			ClusterHeights.Add(Node->GraphNode, ClusterHeight);
			if (PureDir == EBALPureDir::West)
			{
				LeftExtensions[LayerIndex] = FMath::Max(LeftExtensions[LayerIndex], Extension);
			}
			else if (PureDir == EBALPureDir::East)
			{
				RightExtensions[LayerIndex] = FMath::Max(RightExtensions[LayerIndex], Extension);
			}
		}
	}

	TArray<float> LayerX;
	LayerX.SetNumZeroed(Layers.Num());
	for (int32 LayerIndex = 1; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		LayerX[LayerIndex] = LayerX[LayerIndex - 1]
			+ LayerWidths[LayerIndex - 1]
			+ EffectiveGap(Settings.GapX, Settings.NodeMargin)
			+ RightExtensions[LayerIndex - 1]
			+ LeftExtensions[LayerIndex];
	}

	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		float CursorY = 0.f;
		for (int32 Order = 0; Order < Layers[LayerIndex].Num(); ++Order)
		{
			FBALNode* Node = Layers[LayerIndex][Order];
			const float ClusterHeight = ClusterHeights.FindRef(Node->GraphNode);
			Node->LayerOrder = Order;
			Node->OutPos = FVector2D(
				LayerX[LayerIndex],
				CursorY + (ClusterHeight - Node->Size.Y) * 0.5f);
			CursorY += ClusterHeight + EffectiveGap(Settings.GapY, Settings.NodeMargin);
		}
	}

	if (Settings.bAlignExecPins)
	{
		AlignLayers(Layers, Edges, ClusterHeights, Settings);
	}
}

void FBALLayoutSolver::LayoutDataComponent(
	const FBALComponent& Component,
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	TArray<FBALEdge>& Edges,
	const FBALSettings& Settings)
{
	TArray<FBALNode*> Nodes;
	TSet<UEdGraphNode*> NodeSet;
	for (UEdGraphNode* GraphNode : Component.Nodes)
	{
		FBALNode& Proxy = Proxies.FindChecked(GraphNode);
		if (IsMainDataNode(Proxy))
		{
			Nodes.Add(&Proxy);
			NodeSet.Add(GraphNode);
			Proxy.Layer = 0;
		}
	}
	if (Nodes.Num() == 0)
	{
		return;
	}

	TMap<UEdGraphNode*, TArray<int32>> Outgoing;
	TMap<UEdGraphNode*, uint8> Color;
	for (FBALNode* Node : Nodes)
	{
		Outgoing.Add(Node->GraphNode, TArray<int32>());
		Color.Add(Node->GraphNode, 0);
	}
	for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); ++EdgeIndex)
	{
		FBALEdge& Edge = Edges[EdgeIndex];
		if (Edge.Kind == EBALEdgeKind::Data && ContainsNode(NodeSet, Edge))
		{
			Outgoing.FindChecked(Edge.Source).Add(EdgeIndex);
			Edge.bPrimary = false;
		}
	}
	for (TPair<UEdGraphNode*, TArray<int32>>& Pair : Outgoing)
	{
		Pair.Value.Sort([&Edges, &Proxies](int32 AIndex, int32 BIndex)
		{
			const FBALEdge& A = Edges[AIndex];
			const FBALEdge& B = Edges[BIndex];
			if (A.SourcePinIndex != B.SourcePinIndex) return A.SourcePinIndex < B.SourcePinIndex;
			const int32 ATarget = Proxies.FindChecked(A.Target).StableIndex;
			const int32 BTarget = Proxies.FindChecked(B.Target).StableIndex;
			if (ATarget != BTarget) return ATarget < BTarget;
			if (A.TargetPinIndex != B.TargetPinIndex) return A.TargetPinIndex < B.TargetPinIndex;
			return AIndex < BIndex;
		});
	}
	Nodes.Sort([](const FBALNode& A, const FBALNode& B) { return A.StableIndex < B.StableIndex; });
	for (FBALNode* Node : Nodes)
	{
		if (Color.FindRef(Node->GraphNode) == 0)
		{
			MarkDataBackEdgesIterative(Node->GraphNode, Outgoing, Edges, Color);
		}
	}

	TMap<UEdGraphNode*, int32> InDegree;
	for (FBALNode* Node : Nodes)
	{
		InDegree.Add(Node->GraphNode, 0);
	}
	for (const FBALEdge& Edge : Edges)
	{
		if (Edge.Kind == EBALEdgeKind::Data && !Edge.bBackEdge && ContainsNode(NodeSet, Edge))
		{
			++InDegree.FindChecked(Edge.Target);
		}
	}

	TArray<UEdGraphNode*> Ready;
	TArray<UEdGraphNode*> TopologicalOrder;
	TopologicalOrder.Reserve(Nodes.Num());
	for (FBALNode* Node : Nodes)
	{
		if (InDegree.FindRef(Node->GraphNode) == 0)
		{
			Ready.Add(Node->GraphNode);
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
		TopologicalOrder.Add(Node);
		for (int32 EdgeIndex : Outgoing.FindChecked(Node))
		{
			const FBALEdge& Edge = Edges[EdgeIndex];
			if (Edge.bBackEdge) continue;
			FBALNode& Target = Proxies.FindChecked(Edge.Target);
			Target.Layer = FMath::Max(Target.Layer, Proxies.FindChecked(Node).Layer + 1);
			int32& Degree = InDegree.FindChecked(Edge.Target);
			--Degree;
			if (Degree == 0) Ready.Add(Edge.Target);
		}
	}

	// Blueprint Assist formats Material/data graphs from their result node back
	// through inputs. Reverse longest-path ranks keep direct sink inputs close to
	// the sink while preserving left-to-right direction for every data edge.
	TMap<UEdGraphNode*, int32> DistanceToSink;
	int32 MaxDistanceToSink = 0;
	for (int32 OrderIndex = TopologicalOrder.Num() - 1; OrderIndex >= 0; --OrderIndex)
	{
		UEdGraphNode* Node = TopologicalOrder[OrderIndex];
		int32 Distance = 0;
		for (int32 EdgeIndex : Outgoing.FindChecked(Node))
		{
			const FBALEdge& Edge = Edges[EdgeIndex];
			if (!Edge.bBackEdge)
			{
				Distance = FMath::Max(Distance, DistanceToSink.FindRef(Edge.Target) + 1);
			}
		}
		DistanceToSink.Add(Node, Distance);
		MaxDistanceToSink = FMath::Max(MaxDistanceToSink, Distance);
	}
	for (FBALNode* Node : Nodes)
	{
		Node->Layer = MaxDistanceToSink - DistanceToSink.FindRef(Node->GraphNode);
	}

	int32 MaxLayer = 0;
	for (FBALNode* Node : Nodes) MaxLayer = FMath::Max(MaxLayer, Node->Layer);
	TArray<TArray<FBALNode*>> Layers;
	Layers.SetNum(MaxLayer + 1);
	for (FBALNode* Node : Nodes) Layers[Node->Layer].Add(Node);

	TArray<float> Widths;
	TArray<float> X;
	Widths.SetNumZeroed(Layers.Num());
	X.SetNumZeroed(Layers.Num());
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		for (FBALNode* Node : Layers[LayerIndex]) Widths[LayerIndex] = FMath::Max(Widths[LayerIndex], Node->Size.X);
		if (LayerIndex > 0)
		{
			X[LayerIndex] = X[LayerIndex - 1] + Widths[LayerIndex - 1]
				+ EffectiveGap(Settings.GapX, Settings.NodeMargin);
		}
	}

	// Choose one downstream owner for each producer. The owner forest is only a
	// placement structure: shared DAG links remain in Edges, but a shared node is
	// assigned to exactly one branch envelope.
	TMap<UEdGraphNode*, int32> OwnerEdgeBySource;
	for (FBALNode* Node : Nodes)
	{
		int32 BestEdgeIndex = INDEX_NONE;
		for (int32 EdgeIndex : Outgoing.FindChecked(Node->GraphNode))
		{
			const FBALEdge& Candidate = Edges[EdgeIndex];
			if (Candidate.bBackEdge) continue;
			if (BestEdgeIndex == INDEX_NONE)
			{
				BestEdgeIndex = EdgeIndex;
				continue;
			}

			const FBALEdge& Existing = Edges[BestEdgeIndex];
			const FBALNode& CandidateTarget = Proxies.FindChecked(Candidate.Target);
			const FBALNode& ExistingTarget = Proxies.FindChecked(Existing.Target);
			const bool bSameTarget = Candidate.Target == Existing.Target;
			const bool bTake = CandidateTarget.Layer < ExistingTarget.Layer
				|| (CandidateTarget.Layer == ExistingTarget.Layer && bSameTarget
					&& Candidate.TargetPinIndex < Existing.TargetPinIndex)
				|| (CandidateTarget.Layer == ExistingTarget.Layer && bSameTarget
					&& Candidate.TargetPinIndex == Existing.TargetPinIndex
					&& Candidate.SourcePinIndex < Existing.SourcePinIndex)
				|| (CandidateTarget.Layer == ExistingTarget.Layer && !bSameTarget
					&& CandidateTarget.StableIndex < ExistingTarget.StableIndex);
			if (bTake) BestEdgeIndex = EdgeIndex;
		}
		if (BestEdgeIndex != INDEX_NONE) OwnerEdgeBySource.Add(Node->GraphNode, BestEdgeIndex);
	}

	TMap<UEdGraphNode*, TArray<int32>> OwnedChildren;
	for (FBALNode* Node : Nodes) OwnedChildren.Add(Node->GraphNode, TArray<int32>());
	for (const TPair<UEdGraphNode*, int32>& Pair : OwnerEdgeBySource)
	{
		const FBALEdge& Edge = Edges[Pair.Value];
		OwnedChildren.FindChecked(Edge.Target).Add(Pair.Value);
	}
	for (TPair<UEdGraphNode*, TArray<int32>>& Pair : OwnedChildren)
	{
		Pair.Value.Sort([&Edges, &Proxies](int32 AIndex, int32 BIndex)
		{
			const FBALEdge& A = Edges[AIndex];
			const FBALEdge& B = Edges[BIndex];
			if (A.TargetPinIndex != B.TargetPinIndex) return A.TargetPinIndex < B.TargetPinIndex;
			if (A.SourcePinIndex != B.SourcePinIndex) return A.SourcePinIndex < B.SourcePinIndex;
			const FBALNode& ASource = Proxies.FindChecked(A.Source);
			const FBALNode& BSource = Proxies.FindChecked(B.Source);
			if (!FMath::IsNearlyEqual(ASource.OriginalPos.Y, BSource.OriginalPos.Y))
			{
				return ASource.OriginalPos.Y < BSource.OriginalPos.Y;
			}
			return ASource.StableIndex < BSource.StableIndex;
		});
		if (Pair.Value.Num() > 0) Edges[Pair.Value[0]].bPrimary = true;
	}

	// Build branch envelopes bottom-up. Only the first input continues the same
	// row; every later input is shifted as one subtree below the preceding branch.
	TMap<UEdGraphNode*, float> SubtreeMinY;
	TMap<UEdGraphNode*, float> SubtreeMaxY;
	TMap<UEdGraphNode*, float> OffsetFromOwner;
	const float BranchGap = EffectiveGap(Settings.GapY, Settings.NodeMargin);
	for (UEdGraphNode* GraphNode : TopologicalOrder)
	{
		const FBALNode& Node = Proxies.FindChecked(GraphNode);
		float MinY = 0.f;
		float MaxY = Node.Size.Y;
		float BranchBottom = 0.f;
		bool bHasPlacedBranch = false;
		for (int32 EdgeIndex : OwnedChildren.FindChecked(GraphNode))
		{
			const FBALEdge& Edge = Edges[EdgeIndex];
			const FBALNode& Child = Proxies.FindChecked(Edge.Source);
			const float ChildMin = SubtreeMinY.FindRef(Edge.Source);
			const float ChildMax = SubtreeMaxY.FindRef(Edge.Source);
			float Offset = EstimatePinOffset(Node, Edge.TargetPin)
				- EstimatePinOffset(Child, Edge.SourcePin);
			if (bHasPlacedBranch)
			{
				Offset = FMath::Max(Offset, BranchBottom + BranchGap - ChildMin);
			}
			OffsetFromOwner.Add(Edge.Source, Offset);
			MinY = FMath::Min(MinY, Offset + ChildMin);
			MaxY = FMath::Max(MaxY, Offset + ChildMax);
			BranchBottom = bHasPlacedBranch
				? FMath::Max(BranchBottom, Offset + ChildMax)
				: Offset + ChildMax;
			bHasPlacedBranch = true;
		}
		SubtreeMinY.Add(GraphNode, MinY);
		SubtreeMaxY.Add(GraphNode, MaxY);
	}

	TArray<UEdGraphNode*> Roots;
	for (FBALNode* Node : Nodes)
	{
		if (!OwnerEdgeBySource.Contains(Node->GraphNode)) Roots.Add(Node->GraphNode);
	}
	Roots.Sort([&Proxies, &Component](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		const bool bAAnchor = &A == Component.Anchor;
		const bool bBAnchor = &B == Component.Anchor;
		if (bAAnchor != bBAnchor) return bAAnchor;
		const FBALNode& AProxy = Proxies.FindChecked(const_cast<UEdGraphNode*>(&A));
		const FBALNode& BProxy = Proxies.FindChecked(const_cast<UEdGraphNode*>(&B));
		if (!FMath::IsNearlyEqual(AProxy.OriginalPos.Y, BProxy.OriginalPos.Y))
		{
			return AProxy.OriginalPos.Y < BProxy.OriginalPos.Y;
		}
		return AProxy.StableIndex < BProxy.StableIndex;
	});

	float ForestBottom = 0.f;
	bool bFirstRoot = true;
	for (UEdGraphNode* Root : Roots)
	{
		const float RootY = bFirstRoot
			? 0.f
			: ForestBottom + BranchGap - SubtreeMinY.FindRef(Root);
		FBALNode& RootProxy = Proxies.FindChecked(Root);
		RootProxy.OutPos = FVector2D(X[RootProxy.Layer], RootY);

		TArray<UEdGraphNode*> Pending;
		Pending.Add(Root);
		for (int32 Head = 0; Head < Pending.Num(); ++Head)
		{
			UEdGraphNode* ParentNode = Pending[Head];
			const FBALNode& Parent = Proxies.FindChecked(ParentNode);
			for (int32 EdgeIndex : OwnedChildren.FindChecked(ParentNode))
			{
				const FBALEdge& Edge = Edges[EdgeIndex];
				FBALNode& Child = Proxies.FindChecked(Edge.Source);
				Child.OutPos = FVector2D(
					X[Child.Layer],
					Parent.OutPos.Y + OffsetFromOwner.FindRef(Edge.Source));
				Pending.Add(Edge.Source);
			}
		}

		ForestBottom = FMath::Max(ForestBottom, RootY + SubtreeMaxY.FindRef(Root));
		bFirstRoot = false;
	}

	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		Layers[LayerIndex].Sort([](const FBALNode& A, const FBALNode& B)
		{
			if (!FMath::IsNearlyEqual(A.OutPos.Y, B.OutPos.Y)) return A.OutPos.Y < B.OutPos.Y;
			return A.StableIndex < B.StableIndex;
		});
		for (int32 Order = 0; Order < Layers[LayerIndex].Num(); ++Order)
		{
			Layers[LayerIndex][Order]->LayerOrder = Order;
		}
	}
}

void FBALLayoutSolver::ReduceCrossings(
	TArray<TArray<FBALNode*>>& Layers,
	const TArray<FBALEdge>& Edges,
	const FBALSettings& Settings)
{
	TMap<UEdGraphNode*, FBALNode*> NodeMap;
	for (TArray<FBALNode*>& Layer : Layers)
	{
		for (int32 Index = 0; Index < Layer.Num(); ++Index)
		{
			Layer[Index]->LayerOrder = Index;
			NodeMap.Add(Layer[Index]->GraphNode, Layer[Index]);
		}
	}
	TMap<UEdGraphNode*, TArray<const FBALEdge*>> Incoming;
	TMap<UEdGraphNode*, TArray<const FBALEdge*>> Outgoing;
	for (const FBALEdge& Edge : Edges)
	{
		if (Edge.bBackEdge || !NodeMap.Contains(Edge.Source) || !NodeMap.Contains(Edge.Target))
		{
			continue;
		}
		Incoming.FindOrAdd(Edge.Target).Add(&Edge);
		Outgoing.FindOrAdd(Edge.Source).Add(&Edge);
	}
	for (TPair<UEdGraphNode*, TArray<const FBALEdge*>>& Pair : Incoming)
	{
		Pair.Value.Sort([&NodeMap](const FBALEdge& A, const FBALEdge& B)
		{
			const int32 AStable = NodeMap.FindChecked(A.Source)->StableIndex;
			const int32 BStable = NodeMap.FindChecked(B.Source)->StableIndex;
			if (AStable != BStable) return AStable < BStable;
			if (A.SourcePinIndex != B.SourcePinIndex) return A.SourcePinIndex < B.SourcePinIndex;
			return A.TargetPinIndex < B.TargetPinIndex;
		});
	}
	for (TPair<UEdGraphNode*, TArray<const FBALEdge*>>& Pair : Outgoing)
	{
		Pair.Value.Sort([&NodeMap](const FBALEdge& A, const FBALEdge& B)
		{
			const int32 AStable = NodeMap.FindChecked(A.Target)->StableIndex;
			const int32 BStable = NodeMap.FindChecked(B.Target)->StableIndex;
			if (AStable != BStable) return AStable < BStable;
			if (A.TargetPinIndex != B.TargetPinIndex) return A.TargetPinIndex < B.TargetPinIndex;
			return A.SourcePinIndex < B.SourcePinIndex;
		});
	}

	const int32 PassCount = FMath::Max(0, Settings.CrossingReductionPasses);
	for (int32 Pass = 0; Pass < PassCount; ++Pass)
	{
		const bool bForward = Pass % 2 == 0;
		const int32 Start = bForward ? 1 : Layers.Num() - 2;
		const int32 End = bForward ? Layers.Num() : -1;
		const int32 Step = bForward ? 1 : -1;
		for (int32 LayerIndex = Start; LayerIndex != End; LayerIndex += Step)
		{
			if (!Layers.IsValidIndex(LayerIndex)) continue;
			TMap<FBALNode*, int32> ScoreKeys;
			TMap<FBALNode*, int32> PreviousOrder;
			for (FBALNode* Node : Layers[LayerIndex])
			{
				float NeighborOrderSum = 0.f;
				int32 NeighborCount = 0;
				const TArray<const FBALEdge*>* NeighborEdges = bForward
					? Incoming.Find(Node->GraphNode)
					: Outgoing.Find(Node->GraphNode);
				if (NeighborEdges)
				{
					for (const FBALEdge* Edge : *NeighborEdges)
					{
						if (!Edge) continue;
						if (bForward)
						{
							const FBALNode* Source = NodeMap.FindRef(Edge->Source);
							if (Source && Source->Layer < Node->Layer)
							{
								NeighborOrderSum +=
									static_cast<float>(Source->LayerOrder) + Edge->SourcePinIndex * 0.01f;
								++NeighborCount;
							}
						}
						else
						{
							const FBALNode* Target = NodeMap.FindRef(Edge->Target);
							if (Target && Target->Layer > Node->Layer)
							{
								NeighborOrderSum +=
									static_cast<float>(Target->LayerOrder) + Edge->TargetPinIndex * 0.01f;
								++NeighborCount;
							}
						}
					}
				}
				const float Score = NeighborCount > 0
					? NeighborOrderSum / static_cast<float>(NeighborCount)
					: static_cast<float>(Node->LayerOrder);
				ScoreKeys.Add(Node, FMath::RoundToInt(Score * 1000.f));
				PreviousOrder.Add(Node, Node->LayerOrder);
			}

			Layers[LayerIndex].Sort([&ScoreKeys, &PreviousOrder](const FBALNode& A, const FBALNode& B)
			{
				FBALNode* AP = const_cast<FBALNode*>(&A);
				FBALNode* BP = const_cast<FBALNode*>(&B);
				const int32 AScore = ScoreKeys.FindRef(AP);
				const int32 BScore = ScoreKeys.FindRef(BP);
				if (AScore != BScore) return AScore < BScore;
				const int32 APrevious = PreviousOrder.FindRef(AP);
				const int32 BPrevious = PreviousOrder.FindRef(BP);
				if (APrevious != BPrevious) return APrevious < BPrevious;
				return A.StableIndex < B.StableIndex;
			});
			for (int32 Index = 0; Index < Layers[LayerIndex].Num(); ++Index)
			{
				Layers[LayerIndex][Index]->LayerOrder = Index;
			}
		}
	}
}

void FBALLayoutSolver::AlignLayers(
	TArray<TArray<FBALNode*>>& Layers,
	const TArray<FBALEdge>& Edges,
	const TMap<UEdGraphNode*, float>& ClusterHeights,
	const FBALSettings& Settings)
{
	TMap<UEdGraphNode*, FBALNode*> NodeMap;
	for (TArray<FBALNode*>& Layer : Layers)
	{
		for (FBALNode* Node : Layer) NodeMap.Add(Node->GraphNode, Node);
	}

	TMap<UEdGraphNode*, TArray<const FBALEdge*>> IncomingEdges;
	TMap<UEdGraphNode*, TArray<const FBALEdge*>> OutgoingEdges;
	for (const FBALEdge& Edge : Edges)
	{
		if (Edge.bBackEdge || !NodeMap.Contains(Edge.Source) || !NodeMap.Contains(Edge.Target))
		{
			continue;
		}
		IncomingEdges.FindOrAdd(Edge.Target).Add(&Edge);
		OutgoingEdges.FindOrAdd(Edge.Source).Add(&Edge);
	}

	for (int32 Pass = 0; Pass < FMath::Max(0, Settings.AlignmentPasses); ++Pass)
	{
		for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
		{
			TArray<float> DesiredTops;
			DesiredTops.Reserve(Layers[LayerIndex].Num());
			for (FBALNode* Node : Layers[LayerIndex])
			{
				TArray<float> Candidates;
				if (const TArray<const FBALEdge*>* Incoming = IncomingEdges.Find(Node->GraphNode))
				{
					Candidates.Reserve(Incoming->Num());
					for (const FBALEdge* Edge : *Incoming)
					{
						FBALNode* const Source = NodeMap.FindRef(Edge->Source);
						if (Source->Layer != Node->Layer)
						{
							const float AlignedTop = Source->OutPos.Y
								+ EstimatePinOffset(*Source, Edge->SourcePin)
								- EstimatePinOffset(*Node, Edge->TargetPin);
							Candidates.Add(AlignedTop);
							if (Edge->bPrimary) Candidates.Add(AlignedTop);
						}
					}
				}
				if (const TArray<const FBALEdge*>* Outgoing = OutgoingEdges.Find(Node->GraphNode))
				{
					Candidates.Reserve(Candidates.Num() + Outgoing->Num());
					for (const FBALEdge* Edge : *Outgoing)
					{
						FBALNode* const Target = NodeMap.FindRef(Edge->Target);
						if (Target->Layer != Node->Layer)
						{
							const float AlignedTop = Target->OutPos.Y
								+ EstimatePinOffset(*Target, Edge->TargetPin)
								- EstimatePinOffset(*Node, Edge->SourcePin);
							Candidates.Add(AlignedTop);
							if (Edge->bPrimary) Candidates.Add(AlignedTop);
						}
					}
				}
				const float Height = ClusterHeights.FindRef(Node->GraphNode);
				const float Offset = (Height - Node->Size.Y) * 0.5f;
				DesiredTops.Add(Median(Candidates, Node->OutPos.Y) - Offset);
			}

			float Cursor = DesiredTops.Num() > 0 ? DesiredTops[0] : 0.f;
			for (int32 Index = 0; Index < Layers[LayerIndex].Num(); ++Index)
			{
				FBALNode* Node = Layers[LayerIndex][Index];
				const float Height = ClusterHeights.FindRef(Node->GraphNode);
				const float Top = Index == 0 ? DesiredTops[Index] : FMath::Max(DesiredTops[Index], Cursor);
				Node->OutPos.Y = Top + (Height - Node->Size.Y) * 0.5f;
				Cursor = Top + Height + EffectiveGap(Settings.GapY, Settings.NodeMargin);
			}
		}
	}
}

void FBALLayoutSolver::PlacePureGroups(
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	const TArray<FBALEdge>& Edges,
	TMap<UEdGraphNode*, TArray<FBALNode*>>& OwnedPuresByConsumer,
	EBALPureDir PureDir,
	const FBALSettings& Settings)
{
	TMap<UEdGraphNode*, int32> RelevantTargetPins;
	for (const TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
	{
		if (Pair.Value.Role == EBALNodeRole::Pure && Pair.Value.PureOwner != nullptr)
		{
			RelevantTargetPins.Add(Pair.Key, MAX_int32);
		}
	}
	for (const FBALEdge& Edge : Edges)
	{
		const FBALNode* Source = Proxies.Find(Edge.Source);
		const FBALNode* Target = Proxies.Find(Edge.Target);
		int32* TargetPin = RelevantTargetPins.Find(Edge.Source);
		if (!Source || !TargetPin) continue;
		if (Edge.Target == Source->PureOwner
			|| (Target && Target->PureOwner == Source->PureOwner
				&& Target->PureDepth < Source->PureDepth))
		{
			*TargetPin = FMath::Min(*TargetPin, Edge.TargetPinIndex);
		}
	}

	for (TPair<UEdGraphNode*, FBALNode>& ConsumerPair : Proxies)
	{
		FBALNode& Consumer = ConsumerPair.Value;
		if (Consumer.Role != EBALNodeRole::Exec)
		{
			continue;
		}

		TArray<FBALNode*>* GroupPtr = OwnedPuresByConsumer.Find(Consumer.GraphNode);
		if (!GroupPtr || GroupPtr->Num() == 0)
		{
			continue;
		}
		TArray<FBALNode*>& Group = *GroupPtr;

		Group.Sort([&RelevantTargetPins](const FBALNode& A, const FBALNode& B)
		{
			if (A.PureDepth != B.PureDepth) return A.PureDepth < B.PureDepth;
			const int32 ATargetPin = RelevantTargetPins.FindChecked(A.GraphNode);
			const int32 BTargetPin = RelevantTargetPins.FindChecked(B.GraphNode);
			if (ATargetPin != BTargetPin) return ATargetPin < BTargetPin;
			if (A.OriginalPos.Y != B.OriginalPos.Y) return A.OriginalPos.Y < B.OriginalPos.Y;
			return A.StableIndex < B.StableIndex;
		});
		const int32 MaxDepth = Group.Last()->PureDepth;

		TArray<float> Widths;
		TArray<float> Heights;
		TArray<float> RowWidths;
		TArray<float> RowHeights;
		Widths.SetNumZeroed(MaxDepth + 1);
		Heights.SetNumZeroed(MaxDepth + 1);
		RowWidths.SetNumZeroed(MaxDepth + 1);
		RowHeights.SetNumZeroed(MaxDepth + 1);
		TArray<int32> Counts;
		Counts.SetNumZeroed(MaxDepth + 1);
		for (FBALNode* Pure : Group)
		{
			Widths[Pure->PureDepth] = FMath::Max(Widths[Pure->PureDepth], Pure->Size.X);
			Heights[Pure->PureDepth] += Pure->Size.Y;
			RowWidths[Pure->PureDepth] += Pure->Size.X;
			RowHeights[Pure->PureDepth] = FMath::Max(RowHeights[Pure->PureDepth], Pure->Size.Y);
			++Counts[Pure->PureDepth];
		}
		for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
		{
			if (Counts[Depth] > 1)
			{
				Heights[Depth] += (Counts[Depth] - 1) * EffectiveGap(Settings.PureGapY, Settings.NodeMargin);
				RowWidths[Depth] += (Counts[Depth] - 1) * EffectiveGap(Settings.PureGapX, Settings.NodeMargin);
			}
		}

		const float HorizontalGap = EffectiveGap(Settings.GapX, Settings.NodeMargin);
		const float LayerGap = EffectiveGap(Settings.PureGapX, Settings.NodeMargin);
		const float ConsumerCenterY = Consumer.OutPos.Y + Consumer.Size.Y * 0.5f;
		if (PureDir == EBALPureDir::West || PureDir == EBALPureDir::East)
		{
			float LayerX = PureDir == EBALPureDir::West
				? Consumer.OutPos.X - HorizontalGap - Widths[0]
				: Consumer.OutPos.X + Consumer.Size.X + HorizontalGap;
			int32 GroupIndex = 0;
			for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
			{
				float CursorY = ConsumerCenterY - Heights[Depth] * 0.5f;
				while (GroupIndex < Group.Num() && Group[GroupIndex]->PureDepth == Depth)
				{
					FBALNode* Pure = Group[GroupIndex++];
					const float X = PureDir == EBALPureDir::West
						? LayerX + Widths[Depth] - Pure->Size.X
						: LayerX;
					Pure->OutPos = FVector2D(X, CursorY);
					Pure->Layer = Consumer.Layer;
					CursorY += Pure->Size.Y + EffectiveGap(Settings.PureGapY, Settings.NodeMargin);
				}
				if (Depth < MaxDepth)
				{
					LayerX += PureDir == EBALPureDir::West
						? -(Widths[Depth + 1] + LayerGap)
						: Widths[Depth] + LayerGap;
				}
			}
		}
		else
		{
			float LayerY = PureDir == EBALPureDir::North
				? Consumer.OutPos.Y - EffectiveGap(Settings.GapY, Settings.NodeMargin) - RowHeights[0]
				: Consumer.OutPos.Y + Consumer.Size.Y + EffectiveGap(Settings.GapY, Settings.NodeMargin);
			int32 GroupIndex = 0;
			for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
			{
				const float TotalWidth = RowWidths[Depth];
				float CursorX = Consumer.OutPos.X + Consumer.Size.X * 0.5f - TotalWidth * 0.5f;
				while (GroupIndex < Group.Num() && Group[GroupIndex]->PureDepth == Depth)
				{
					FBALNode* Pure = Group[GroupIndex++];
					Pure->OutPos = FVector2D(CursorX, LayerY);
					Pure->Layer = Consumer.Layer;
					CursorX += Pure->Size.X + EffectiveGap(Settings.PureGapX, Settings.NodeMargin);
				}
				if (Depth < MaxDepth)
				{
					LayerY += PureDir == EBALPureDir::North
						? -(RowHeights[Depth + 1] + EffectiveGap(Settings.GapY, Settings.NodeMargin))
						: RowHeights[Depth] + EffectiveGap(Settings.GapY, Settings.NodeMargin);
				}
			}
		}
	}
}

void FBALLayoutSolver::PlaceKnotNodes(
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	const FBALSettings& Settings)
{
	for (int32 Iteration = 0; Iteration < 8; ++Iteration)
	{
		TMap<UEdGraphNode*, FVector2D> NextPositions;
		for (TPair<UEdGraphNode*, FBALNode>& Pair : Proxies)
		{
			FBALNode& Knot = Pair.Value;
			if (Knot.Role != EBALNodeRole::Knot)
			{
				continue;
			}
			FVector2D Sum = FVector2D::ZeroVector;
			int32 Count = 0;
			for (UEdGraphPin* KnotPin : Knot.GraphNode->Pins)
			{
				if (!KnotPin) continue;
				for (UEdGraphPin* Linked : KnotPin->LinkedTo)
				{
					FBALNode* Other = Proxies.Find(Linked ? Linked->GetOwningNode() : nullptr);
					if (!Other || Other->Role == EBALNodeRole::Comment) continue;
					FVector2D Anchor;
					if (Other->Role == EBALNodeRole::Knot)
					{
						Anchor = Other->OutPos + Other->Size * 0.5f;
					}
					else
					{
						Anchor.X = Linked->Direction == EGPD_Input
							? Other->OutPos.X
							: Other->OutPos.X + Other->Size.X;
						Anchor.Y = Other->OutPos.Y + EstimatePinOffset(*Other, Linked);
					}
					Sum += Anchor;
					++Count;
				}
			}
			if (Count > 0)
			{
				NextPositions.Add(Knot.GraphNode, Sum / static_cast<float>(Count) - Knot.Size * 0.5f);
			}
		}
		for (TPair<UEdGraphNode*, FVector2D>& Pair : NextPositions)
		{
			Proxies.FindChecked(Pair.Key).OutPos = Pair.Value;
		}
	}
	(void)Settings;
}

void FBALLayoutSolver::PreserveComponentAnchors(
	TArray<FBALComponent>& Components,
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	const FBALSettings& Settings)
{
	if (!Settings.bPreserveAnchors)
	{
		return;
	}
	for (FBALComponent& Component : Components)
	{
		FBALNode* Anchor = Proxies.Find(Component.Anchor);
		if (!Anchor)
		{
			continue;
		}
		const FVector2D Delta = Component.OriginalAnchor - Anchor->OutPos;
		for (UEdGraphNode* Node : Component.Nodes)
		{
			FBALNode* Proxy = Proxies.Find(Node);
			if (Proxy) Proxy->OutPos += Delta;
		}
	}
}

void FBALLayoutSolver::PackOverlappingComponents(
	TArray<FBALComponent>& Components,
	TMap<UEdGraphNode*, FBALNode>& Proxies,
	const FBALSettings& Settings)
{
	TArray<FBALComponent*> Ordered;
	for (FBALComponent& Component : Components) Ordered.Add(&Component);
	Ordered.Sort([](const FBALComponent& A, const FBALComponent& B)
	{
		if (A.bHasHardAnchor != B.bHasHardAnchor) return A.bHasHardAnchor;
		if (A.OriginalAnchor.Y != B.OriginalAnchor.Y) return A.OriginalAnchor.Y < B.OriginalAnchor.Y;
		if (A.OriginalAnchor.X != B.OriginalAnchor.X) return A.OriginalAnchor.X < B.OriginalAnchor.X;
		return A.Id < B.Id;
	});

	TArray<FBox2D> PlacedBounds;
	for (FBALComponent* Component : Ordered)
	{
		FBox2D Bounds = ComponentBounds(*Component, Proxies);
		if (!Component->bHasHardAnchor)
		{
			for (int32 Attempt = 0; Attempt < Components.Num() * 2 + 1; ++Attempt)
			{
				float PushDown = 0.f;
				float PushRight = 0.f;
				bool bOverlap = false;
				for (const FBox2D& Other : PlacedBounds)
				{
					const FBox2D Expanded(
						Other.Min - FVector2D(Settings.ComponentGapX, Settings.ComponentGapY),
						Other.Max + FVector2D(Settings.ComponentGapX, Settings.ComponentGapY));
					const bool bStrictOverlap =
						Bounds.Min.X < Expanded.Max.X && Expanded.Min.X < Bounds.Max.X
						&& Bounds.Min.Y < Expanded.Max.Y && Expanded.Min.Y < Bounds.Max.Y;
					if (!bStrictOverlap) continue;
					bOverlap = true;
					PushDown = FMath::Max(PushDown, Expanded.Max.Y - Bounds.Min.Y);
					PushRight = FMath::Max(PushRight, Expanded.Max.X - Bounds.Min.X);
				}
				if (!bOverlap) break;
				const FVector2D Delta = PushDown <= PushRight
					? FVector2D(0.f, PushDown)
					: FVector2D(PushRight, 0.f);
				for (UEdGraphNode* Node : Component->Nodes)
				{
					FBALNode* Proxy = Proxies.Find(Node);
					if (Proxy) Proxy->OutPos += Delta;
				}
				Bounds = ComponentBounds(*Component, Proxies);
			}
		}
		PlacedBounds.Add(Bounds);
	}
}

float FBALLayoutSolver::EstimatePinOffset(const FBALNode& Node, const UEdGraphPin* Pin)
{
	if (!Pin || !Node.GraphNode)
	{
		return Node.Size.Y * 0.5f;
	}
	int32 Count = 0;
	int32 Index = 0;
	for (const UEdGraphPin* Candidate : Node.GraphNode->Pins)
	{
		if (!Candidate || Candidate->Direction != Pin->Direction) continue;
		if (Candidate == Pin) Index = Count;
		++Count;
	}
	if (Count <= 0) return Node.Size.Y * 0.5f;
	const float Header = FMath::Min(36.f, Node.Size.Y * 0.35f);
	const float RowHeight = FMath::Max(18.f, (Node.Size.Y - Header) / static_cast<float>(Count));
	return FMath::Clamp(Header + (Index + 0.5f) * RowHeight, 8.f, FMath::Max(8.f, Node.Size.Y - 8.f));
}

float FBALLayoutSolver::PureClusterHeight(
	const FBALNode& Consumer,
	const TArray<FBALNode*>& OwnedPures,
	EBALPureDir PureDir,
	const FBALSettings& Settings)
{
	TMap<int32, float> Heights;
	TMap<int32, int32> Counts;
	int32 MaxDepth = -1;
	for (const FBALNode* Pure : OwnedPures)
	{
		if (PureDir == EBALPureDir::North || PureDir == EBALPureDir::South)
		{
			Heights.FindOrAdd(Pure->PureDepth) = FMath::Max(Heights.FindRef(Pure->PureDepth), Pure->Size.Y);
		}
		else
		{
			Heights.FindOrAdd(Pure->PureDepth) += Pure->Size.Y;
		}
		++Counts.FindOrAdd(Pure->PureDepth);
		MaxDepth = FMath::Max(MaxDepth, Pure->PureDepth);
	}
	if (MaxDepth < 0) return Consumer.Size.Y;
	if (PureDir == EBALPureDir::North || PureDir == EBALPureDir::South)
	{
		float Extension = EffectiveGap(Settings.GapY, Settings.NodeMargin);
		for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
		{
			Extension += Heights.FindRef(Depth);
			if (Depth < MaxDepth) Extension += EffectiveGap(Settings.GapY, Settings.NodeMargin);
		}
		return Consumer.Size.Y + Extension * 2.f;
	}
	float Result = Consumer.Size.Y;
	for (const TPair<int32, float>& Pair : Heights)
	{
		Result = FMath::Max(Result, Pair.Value
			+ FMath::Max(0, Counts.FindRef(Pair.Key) - 1) * EffectiveGap(Settings.PureGapY, Settings.NodeMargin));
	}
	return Result;
}

float FBALLayoutSolver::PureLeftExtension(
	const TArray<FBALNode*>& OwnedPures,
	const FBALSettings& Settings)
{
	TMap<int32, float> WidthByDepth;
	int32 MaxDepth = -1;
	for (const FBALNode* Pure : OwnedPures)
	{
		WidthByDepth.FindOrAdd(Pure->PureDepth) = FMath::Max(WidthByDepth.FindRef(Pure->PureDepth), Pure->Size.X);
		MaxDepth = FMath::Max(MaxDepth, Pure->PureDepth);
	}
	if (MaxDepth < 0) return 0.f;
	float Width = EffectiveGap(Settings.GapX, Settings.NodeMargin);
	for (int32 Depth = 0; Depth <= MaxDepth; ++Depth)
	{
		Width += WidthByDepth.FindRef(Depth);
		if (Depth < MaxDepth) Width += EffectiveGap(Settings.PureGapX, Settings.NodeMargin);
	}
	return Width;
}

void FBALLayoutSolver::ClampSoftConstraint(FBALNode& Node)
{
	if (!Node.bConstrained || Node.ConstraintType != EBALConstraintType::Soft || Node.MaxDrift < 0.f)
	{
		return;
	}
	const FVector2D Delta = Node.OutPos - Node.OriginalPos;
	const float Distance = Delta.Size();
	if (Distance > Node.MaxDrift && Distance > KINDA_SMALL_NUMBER)
	{
		Node.OutPos = Node.OriginalPos + Delta * (Node.MaxDrift / Distance);
	}
}

FBox2D FBALLayoutSolver::ComponentBounds(
	const FBALComponent& Component,
	const TMap<UEdGraphNode*, FBALNode>& Proxies)
{
	FBox2D Bounds(EForceInit::ForceInit);
	bool bHasNode = false;
	for (UEdGraphNode* Node : Component.Nodes)
	{
		const FBALNode* Proxy = Proxies.Find(Node);
		if (!Proxy || Proxy->Role == EBALNodeRole::Comment) continue;
		const FBox2D NodeBounds(Proxy->OutPos, Proxy->OutPos + Proxy->Size);
		Bounds = bHasNode ? Bounds + NodeBounds : NodeBounds;
		bHasNode = true;
	}
	return bHasNode ? Bounds : FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector);
}
