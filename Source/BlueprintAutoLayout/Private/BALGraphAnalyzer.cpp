// BALGraphAnalyzer.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BALGraphAnalyzer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"

// ─────────────────────────────────────────────────────────────
//  Public: main entry
// ─────────────────────────────────────────────────────────────

FBALGraphAnalyzer::FAnalysisResult FBALGraphAnalyzer::Analyze(
    UEdGraph* Graph,
    const FBALSettings& Settings,
    const TArray<FBALConstraint>& Constraints)
{
	FAnalysisResult Result;
	if (!Graph) return Result;

	// Build a fast lookup: node → constraint type
	TMap<UEdGraphNode*, int32> ConstraintMap; // 0=none,1=soft,2=hard/rigid
	for (const FBALConstraint& C : Constraints)
	{
		int32 Val = (C.Type == EBALConstraintType::Hard || C.Type == EBALConstraintType::RigidGroup) ? 2 : 1;
		ConstraintMap.Add(C.Node, Val);
	}

	ClassifyNodes(Graph, ConstraintMap, Result.Proxies);
	BuildExecTree(Result.Proxies, Result.ExecRoots, Result.ExecNodePool);
	AssignPureSubtrees(Result.Proxies, Result.ExecRoots);

	// Gather isolated lists
	for (auto& KV : Result.Proxies)
	{
		FBALNode& Proxy = KV.Value;
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

// ─────────────────────────────────────────────────────────────
//  Public: node classification utilities
// ─────────────────────────────────────────────────────────────

bool FBALGraphAnalyzer::HasExecPin(const UEdGraphNode* Node)
{
	if (!Node) return false;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && IsExecPin(Pin))
			return true;
	}
	return false;
}

bool FBALGraphAnalyzer::IsPureNode(const UEdGraphNode* Node)
{
	if (!Node) return false;
	// Has any data pins but NO exec pins
	return !HasExecPin(Node);
}

bool FBALGraphAnalyzer::IsKnotNode(const UEdGraphNode* Node)
{
	if (!Node) return false;
	FString ClassName = Node->GetClass()->GetName();
	return ClassName.Contains(TEXT("Knot")) || ClassName.Contains(TEXT("Reroute"));
}

bool FBALGraphAnalyzer::IsCommentNode(const UEdGraphNode* Node)
{
	return Node && Node->IsA<UEdGraphNode_Comment>();
}

FVector2D FBALGraphAnalyzer::EstimateNodeSize(const UEdGraphNode* Node)
{
	if (!Node) return FVector2D(200.f, 80.f);

	if (IsCommentNode(Node))
	{
		const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node);
		return FVector2D((float)Comment->NodeWidth, (float)Comment->NodeHeight);
	}

	int32 InputPins  = 0;
	int32 OutputPins = 0;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin) continue;
		if (Pin->Direction == EGPD_Input)  InputPins++;
		if (Pin->Direction == EGPD_Output) OutputPins++;
	}

	// Width: base 180 + extra for long pin names (heuristic)
	int32 MaxNameLen = 0;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin)
		{
			int32 Len = Pin->PinName.ToString().Len() + Pin->GetDisplayName().ToString().Len();
			MaxNameLen = FMath::Max(MaxNameLen, Len);
		}
	}

	float Width  = FMath::Clamp(180.f + MaxNameLen * 2.5f, 160.f, 400.f);
	float Height = 48.f + FMath::Max(InputPins, OutputPins) * 24.f;

	return FVector2D(Width, Height);
}

bool FBALGraphAnalyzer::IsExecPin(const UEdGraphPin* Pin)
{
	if (!Pin) return false;
	// Exec pins have PinType.PinCategory == "exec"
	// This name is consistent across K2, AnimGraph, etc.
	return Pin->PinType.PinCategory == FName(TEXT("exec")) ||
	       Pin->PinType.PinCategory == FName(TEXT("delegate")); // also flow-control
}

// ─────────────────────────────────────────────────────────────
//  Private: classify all nodes into proxies
// ─────────────────────────────────────────────────────────────

void FBALGraphAnalyzer::ClassifyNodes(UEdGraph* Graph,
                                      const TMap<UEdGraphNode*, int32>& ConstraintMap,
                                      TMap<UEdGraphNode*, FBALNode>& OutProxies)
{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		FBALNode Proxy;
		Proxy.GraphNode   = Node;
		Proxy.OriginalPos = FVector2D((float)Node->NodePosX, (float)Node->NodePosY);
		Proxy.OutPos      = Proxy.OriginalPos;
		Proxy.Size        = EstimateNodeSize(Node);

		const int32* ConstraintVal = ConstraintMap.Find(Node);
		Proxy.bLocked = (ConstraintVal && *ConstraintVal == 2);

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
			// Check if this node has any connections at all
			bool bConnected = false;
			for (UEdGraphPin* Pin : Node->Pins)
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

// ─────────────────────────────────────────────────────────────
//  Private: build exec tree (DAG → tree, handle cycles)
// ─────────────────────────────────────────────────────────────

void FBALGraphAnalyzer::BuildExecTree(TMap<UEdGraphNode*, FBALNode>& Proxies,
                                      TArray<FBALExecNode*>& OutRoots,
                                      TArray<TUniquePtr<FBALExecNode>>& Pool)
{
	// Collect only exec-role nodes
	TArray<UEdGraphNode*> ExecNodes;
	for (auto& KV : Proxies)
	{
		if (KV.Value.Role == EBALNodeRole::Exec)
			ExecNodes.Add(KV.Key);
	}

	if (ExecNodes.IsEmpty()) return;

	// For each exec node, compute exec-in-degree (how many exec nodes feed into it)
	TMap<UEdGraphNode*, int32> ExecInDegree;
	TMap<UEdGraphNode*, TArray<UEdGraphNode*>> ExecChildren; // exec outgoing edges only

	for (UEdGraphNode* Node : ExecNodes)
	{
		if (!ExecInDegree.Contains(Node)) ExecInDegree.Add(Node, 0);
		if (!ExecChildren.Contains(Node)) ExecChildren.Add(Node, TArray<UEdGraphNode*>());

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || !IsExecPin(Pin)) continue;
			if (Pin->Direction != EGPD_Output) continue;

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
				UEdGraphNode* Target = LinkedPin->GetOwningNode();

				if (Proxies.Contains(Target) && Proxies[Target].Role == EBALNodeRole::Exec)
				{
					ExecChildren[Node].AddUnique(Target);
					ExecInDegree.FindOrAdd(Target, 0)++;
				}
			}
		}
	}

	// Nodes with exec-in-degree 0 are entry points (roots)
	TSet<UEdGraphNode*> Visited;
	// Allocate FBALExecNode for every exec node
	TMap<UEdGraphNode*, FBALExecNode*> ExecNodeMap;
	for (UEdGraphNode* Node : ExecNodes)
	{
		TUniquePtr<FBALExecNode> EN = MakeUnique<FBALExecNode>();
		EN->Proxy = &Proxies[Node];
		ExecNodeMap.Add(Node, EN.Get());
		Pool.Add(MoveTemp(EN));
	}

	// Link parent/child
	for (UEdGraphNode* Node : ExecNodes)
	{
		FBALExecNode* ParentEN = ExecNodeMap[Node];
		for (UEdGraphNode* Child : ExecChildren[Node])
		{
			FBALExecNode* ChildEN = ExecNodeMap[Child];
			// Cycle detection: if Child is already an ancestor of Node, skip
			if (ChildEN->Children.Contains(ParentEN)) continue; // crude cycle check

			if (!ParentEN->Children.Contains(ChildEN))
			{
				ParentEN->Children.Add(ChildEN);
				ChildEN->Parent = ParentEN;
			}
		}
	}

	// Collect true roots: nodes with no parent in the exec tree
	for (UEdGraphNode* Node : ExecNodes)
	{
		FBALExecNode* EN = ExecNodeMap[Node];
		if (EN->Parent == nullptr)
		{
			OutRoots.Add(EN);
		}
	}

	// Assign ExecDepth BFS from each root
	for (FBALExecNode* Root : OutRoots)
	{
		TArray<FBALExecNode*> Queue;
		Queue.Add(Root);
		Root->Proxy->ExecDepth = 0;

		int32 Head = 0;
		while (Head < Queue.Num())
		{
			FBALExecNode* Cur = Queue[Head++];
			for (int32 i = 0; i < Cur->Children.Num(); ++i)
			{
				FBALExecNode* Child = Cur->Children[i];
				Child->Proxy->ExecDepth   = Cur->Proxy->ExecDepth + 1;
				Child->Proxy->BranchIndex = i;
				Queue.Add(Child);
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────
//  Private: assign Pure subtrees
// ─────────────────────────────────────────────────────────────

void FBALGraphAnalyzer::AssignPureSubtrees(TMap<UEdGraphNode*, FBALNode>& Proxies,
                                           TArray<FBALExecNode*>& ExecRoots)
{
	// Collect pure nodes per exec consumer
	// Key: exec node, Value: pure nodes reachable from it
	TMap<UEdGraphNode*, TArray<FBALNode*>> PuresByExec;
	TMap<UEdGraphNode*, TSet<UEdGraphNode*>> VisitedPerExec;

	// For each exec node, do a BFS on its input DATA pins
	for (auto& KV : Proxies)
	{
		if (KV.Value.Role != EBALNodeRole::Exec) continue;

		UEdGraphNode* ExecNode = KV.Key;
		TArray<FBALNode*>& Group = PuresByExec.FindOrAdd(ExecNode);
		TSet<UEdGraphNode*>& Visited = VisitedPerExec.FindOrAdd(ExecNode);

		CollectPureAncestors(ExecNode, Proxies, Group, Visited);
	}

	// A Pure node may be referenced by multiple Exec nodes.
	// Assign it to the exec node with the smallest exec depth (closest to root = LCA heuristic).
	// Track assignment count per pure node
	TMap<UEdGraphNode*, UEdGraphNode*> PureAssignment; // pure → exec owner

	// Process in order of exec depth (shallow first)
	TArray<TPair<int32, UEdGraphNode*>> DepthSorted;
	for (auto& KV : PuresByExec)
	{
		int32 Depth = Proxies.Contains(KV.Key) ? Proxies[KV.Key].ExecDepth : 0;
		DepthSorted.Add({Depth, KV.Key});
	}
	DepthSorted.Sort([](const TPair<int32,UEdGraphNode*>& A, const TPair<int32,UEdGraphNode*>& B){
		return A.Key < B.Key;
	});

	for (auto& [Depth, ExecNode] : DepthSorted)
	{
		TArray<FBALNode*>& Group = PuresByExec[ExecNode];
		for (FBALNode* PureProxy : Group)
		{
			if (!PureAssignment.Contains(PureProxy->GraphNode))
			{
				PureAssignment.Add(PureProxy->GraphNode, ExecNode);
			}
		}
	}

	// Apply assignments and build per-ExecNode groups
	// We need to find the FBALExecNode for each exec node
	TMap<UEdGraphNode*, FBALExecNode*> ExecNodeMap;
	TFunction<void(FBALExecNode*)> BuildMap = [&](FBALExecNode* EN)
	{
		ExecNodeMap.Add(EN->Proxy->GraphNode, EN);
		for (FBALExecNode* Child : EN->Children) BuildMap(Child);
	};
	for (FBALExecNode* Root : ExecRoots) BuildMap(Root);

	// Assign PureDepth and group membership
	for (auto& [PureNode, ExecNode] : PureAssignment)
	{
		if (!Proxies.Contains(PureNode)) continue;
		FBALNode& PureProxy = Proxies[PureNode];
		PureProxy.PureOwner = ExecNode;

		if (FBALExecNode** EN = ExecNodeMap.Find(ExecNode))
		{
			(*EN)->PureGroup.Add(&PureProxy);
		}
	}

	// Assign PureDepth (0 = directly connected to exec, deeper = further upstream)
	// and PureSlot (slot index within the same depth layer of the same exec node)
	for (auto& [ExecNode, ExecEN] : ExecNodeMap)
	{
		// BFS within the pure group to assign depths
		// Start from nodes directly connected to ExecNode's input data pins
		TMap<UEdGraphNode*, int32> PureDepthMap;
		TArray<UEdGraphNode*> Queue;

		// Direct inputs to ExecNode
		for (UEdGraphPin* Pin : ExecNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) continue;
			if (IsExecPin(Pin)) continue;

			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked) continue;
				UEdGraphNode* Src = Linked->GetOwningNode();
				if (!Src || !Proxies.Contains(Src)) continue;
				if (Proxies[Src].Role != EBALNodeRole::Pure) continue;
				if (Proxies[Src].PureOwner != ExecNode) continue;

				if (!PureDepthMap.Contains(Src))
				{
					PureDepthMap.Add(Src, 0);
					Queue.Add(Src);
				}
			}
		}

		// BFS outward
		int32 Head = 0;
		while (Head < Queue.Num())
		{
			UEdGraphNode* Cur = Queue[Head++];
			int32 CurDepth = PureDepthMap[Cur];

			for (UEdGraphPin* Pin : Cur->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input) continue;
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked) continue;
					UEdGraphNode* Src = Linked->GetOwningNode();
					if (!Src || !Proxies.Contains(Src)) continue;
					if (Proxies[Src].Role != EBALNodeRole::Pure) continue;
					if (Proxies[Src].PureOwner != ExecNode) continue;

					if (!PureDepthMap.Contains(Src))
					{
						PureDepthMap.Add(Src, CurDepth + 1);
						Queue.Add(Src);
					}
				}
			}
		}

		// Write depths back to proxies
		for (auto& [PureNode, Depth] : PureDepthMap)
		{
			if (Proxies.Contains(PureNode))
				Proxies[PureNode].PureDepth = Depth;
		}

		// Assign slot indices within each depth layer
		TMap<int32, int32> SlotCounter;
		// Sort group by depth then by original Y for stable ordering
		ExecEN->PureGroup.Sort([&](const FBALNode& A, const FBALNode& B)
		{
			if (A.PureDepth != B.PureDepth) return A.PureDepth < B.PureDepth;
			return A.OriginalPos.Y < B.OriginalPos.Y;
		});

		for (FBALNode* P : ExecEN->PureGroup)
		{
			int32 Slot = SlotCounter.FindOrAdd(P->PureDepth, 0);
			P->PureSlot = Slot;
			SlotCounter[P->PureDepth]++;
		}
	}
}

void FBALGraphAnalyzer::CollectPureAncestors(UEdGraphNode* ExecNode,
                                             TMap<UEdGraphNode*, FBALNode>& Proxies,
                                             TArray<FBALNode*>& OutPures,
                                             TSet<UEdGraphNode*>& Visited)
{
	for (UEdGraphPin* Pin : ExecNode->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Input) continue;
		if (IsExecPin(Pin)) continue;

		for (UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (!Linked) continue;
			UEdGraphNode* Src = Linked->GetOwningNode();
			if (!Src || !Proxies.Contains(Src)) continue;
			if (Visited.Contains(Src)) continue;

			FBALNode& SrcProxy = Proxies[Src];
			if (SrcProxy.Role == EBALNodeRole::Pure)
			{
				Visited.Add(Src);
				OutPures.Add(&SrcProxy);
				// Recurse into this Pure node's inputs
				CollectPureAncestors(Src, Proxies, OutPures, Visited);
			}
		}
	}
}
