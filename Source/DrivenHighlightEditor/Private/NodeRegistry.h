// NodeRegistry.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Lightweight bookkeeping table populated by lifecycle hooks: NodeGuid →
// WeakObjectPtr<UEdGraphNode>. Lets the highlight service answer
// "which node does this DrivenId.Node.<guid> point at?" without scanning every
// open editor's UEdGraph.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UEdGraphNode;

class FDrivenNodeRegistry
{
public:
	void Add(const UEdGraphNode* Node);
	void Remove(const FGuid& Guid);
	void Clear();

	/** Returns null if the GUID is unknown or the node has been GC'd. */
	const UEdGraphNode* Find(const FGuid& Guid) const;

	int32 Num() const { return Map.Num(); }

private:
	TMap<FGuid, TWeakObjectPtr<const UEdGraphNode>> Map;
};
