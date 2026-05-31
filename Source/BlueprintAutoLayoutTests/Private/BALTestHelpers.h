// BALTestHelpers.h — Lightweight mock helpers for BAL unit tests
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "BALTypes.h"
#include "BALConstraintCollector.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers to build a minimal in-memory graph without requiring a full editor
//  session or asset loading.
//
//  All objects are created in the Transient package so they are GC-safe and
//  never written to disk.
// ─────────────────────────────────────────────────────────────────────────────

namespace BALTest
{

// ── Graph factory ──────────────────────────────────────────────────────────

/** Create a new empty transient EdGraph. */
inline UEdGraph* MakeGraph()
{
	return NewObject<UEdGraph>(GetTransientPackage(), NAME_None,
	                           RF_Transient | RF_NoFlags);
}

// ── Node factory ───────────────────────────────────────────────────────────

/**
 * Create a node and add it to Graph.
 * @param bWithExec   If true, attach one input + one output exec pin.
 * @param NumDataIn   Extra data input pins.
 * @param NumDataOut  Extra data output pins.
 */
inline UEdGraphNode* MakeNode(UEdGraph* Graph,
                               bool bWithExec = false,
                               int32 NumDataIn  = 0,
                               int32 NumDataOut = 1,
                               int32 PosX = 0, int32 PosY = 0)
{
	UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NAME_None,
	                                             RF_Transient | RF_NoFlags);
	Node->NodePosX = PosX;
	Node->NodePosY = PosY;
	Graph->Nodes.Add(Node);

	auto AddPin = [&](EEdGraphPinDirection Dir, FName Category, FName PinName)
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = Category;
		UEdGraphPin* Pin = Node->CreatePin(Dir, PinType, PinName);
		return Pin;
	};

	if (bWithExec)
	{
		AddPin(EGPD_Input,  FName("exec"), FName("ExecIn"));
		AddPin(EGPD_Output, FName("exec"), FName("ExecOut"));
	}

	for (int32 i = 0; i < NumDataIn;  ++i)
		AddPin(EGPD_Input,  FName("object"), FName(*FString::Printf(TEXT("DataIn%d"),  i)));
	for (int32 i = 0; i < NumDataOut; ++i)
		AddPin(EGPD_Output, FName("object"), FName(*FString::Printf(TEXT("DataOut%d"), i)));

	return Node;
}

/** Create a comment node. */
inline UEdGraphNode_Comment* MakeComment(UEdGraph* Graph,
                                          int32 X, int32 Y,
                                          int32 W = 400, int32 H = 300)
{
	UEdGraphNode_Comment* C = NewObject<UEdGraphNode_Comment>(Graph, NAME_None,
	                                                          RF_Transient | RF_NoFlags);
	C->NodePosX  = X;
	C->NodePosY  = Y;
	C->NodeWidth  = W;
	C->NodeHeight = H;
	Graph->Nodes.Add(C);
	return C;
}

// ── Pin wiring ─────────────────────────────────────────────────────────────

/** Wire an output pin of FromNode (index OutIdx) to an input pin of ToNode (index InIdx). */
inline void Wire(UEdGraphNode* FromNode, int32 OutIdx,
                 UEdGraphNode* ToNode,   int32 InIdx)
{
	// Find Nth output pin
	int32 OCount = 0;
	UEdGraphPin* OutPin = nullptr;
	for (UEdGraphPin* P : FromNode->Pins)
	{
		if (P && P->Direction == EGPD_Output)
		{
			if (OCount++ == OutIdx) { OutPin = P; break; }
		}
	}

	int32 ICount = 0;
	UEdGraphPin* InPin = nullptr;
	for (UEdGraphPin* P : ToNode->Pins)
	{
		if (P && P->Direction == EGPD_Input)
		{
			if (ICount++ == InIdx) { InPin = P; break; }
		}
	}

	if (OutPin && InPin)
	{
		OutPin->LinkedTo.AddUnique(InPin);
		InPin->LinkedTo.AddUnique(OutPin);
	}
}

/** Wire exec output of From → exec input of To (exec pins are always index 0 output / input). */
inline void WireExec(UEdGraphNode* From, UEdGraphNode* To)
{
	Wire(From, 0, To, 0);
}

/** Wire data output of From (OutIdx among data-only pins) → data input of To (InIdx). */
inline void WireData(UEdGraphNode* From, int32 OutIdx,
                     UEdGraphNode* To,   int32 InIdx)
{
	// Skip exec pins when counting
	int32 OCount = 0;
	UEdGraphPin* OutPin = nullptr;
	for (UEdGraphPin* P : From->Pins)
	{
		if (!P || P->Direction != EGPD_Output) continue;
		if (P->PinType.PinCategory == FName("exec")) continue;
		if (OCount++ == OutIdx) { OutPin = P; break; }
	}

	int32 ICount = 0;
	UEdGraphPin* InPin = nullptr;
	for (UEdGraphPin* P : To->Pins)
	{
		if (!P || P->Direction != EGPD_Input) continue;
		if (P->PinType.PinCategory == FName("exec")) continue;
		if (ICount++ == InIdx) { InPin = P; break; }
	}

	if (OutPin && InPin)
	{
		OutPin->LinkedTo.AddUnique(InPin);
		InPin->LinkedTo.AddUnique(OutPin);
	}
}

// ── Proxy builder (bypass Analyze for direct solver tests) ─────────────────

/** Build a minimal FBALNode proxy manually (for LayoutSolver / CollisionResolver unit tests). */
inline FBALNode MakeProxy(UEdGraphNode* Node,
                           EBALNodeRole Role = EBALNodeRole::Exec,
                           FVector2D Size    = FVector2D(200.f, 80.f),
                           FVector2D Pos     = FVector2D::ZeroVector,
                           bool bLocked      = false)
{
	FBALNode P;
	P.GraphNode   = Node;
	P.Role        = Role;
	P.Size        = Size;
	P.OutPos      = Pos;
	P.OriginalPos = Pos;
	P.bLocked     = bLocked;
	return P;
}

/** Build a FBALExecNode tree from a proxy map entry (single leaf node). */
inline FBALExecNode* MakeExecNode(FBALNode* Proxy,
                                   TArray<TUniquePtr<FBALExecNode>>& Pool)
{
	TUniquePtr<FBALExecNode> EN = MakeUnique<FBALExecNode>();
	EN->Proxy = Proxy;
	FBALExecNode* Ptr = EN.Get();
	Pool.Add(MoveTemp(EN));
	return Ptr;
}

// ── AABB overlap check ─────────────────────────────────────────────────────

/** Returns true if node A and B (with given margin) overlap in 2D.
 *  Nodes that are merely touching (edge-to-edge) are NOT considered overlapping. */
inline bool NodesOverlap(const FBALNode& A, const FBALNode& B, float Margin = 0.f)
{
	// Strict overlap: exclude boundary-touching (use < not <=)
	const FVector2D MinA = A.OutPos - FVector2D(Margin, Margin);
	const FVector2D MaxA = A.OutPos + A.Size + FVector2D(Margin, Margin);
	const FVector2D MinB = B.OutPos - FVector2D(Margin, Margin);
	const FVector2D MaxB = B.OutPos + B.Size + FVector2D(Margin, Margin);

	return (MinA.X < MaxB.X) && (MinB.X < MaxA.X)
	    && (MinA.Y < MaxB.Y) && (MinB.Y < MaxA.Y);
}

/** Returns true if a value is snapped to the given grid. */
inline bool IsSnapped(float Value, float Grid)
{
	if (Grid <= 0.f) return true;
	float Mod = FMath::Fmod(FMath::Abs(Value), Grid);
	return Mod < 0.5f || Mod > (Grid - 0.5f);
}

} // namespace BALTest
