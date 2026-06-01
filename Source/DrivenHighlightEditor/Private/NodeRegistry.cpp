// NodeRegistry.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "NodeRegistry.h"
#include "EdGraph/EdGraphNode.h"

void FDrivenNodeRegistry::Add(const UEdGraphNode* Node)
{
	if (!Node) return;
	Map.Add(Node->NodeGuid, Node);
}

void FDrivenNodeRegistry::Remove(const FGuid& Guid)
{
	Map.Remove(Guid);
}

void FDrivenNodeRegistry::Clear()
{
	Map.Empty();
}

const UEdGraphNode* FDrivenNodeRegistry::Find(const FGuid& Guid) const
{
	const TWeakObjectPtr<const UEdGraphNode>* Found = Map.Find(Guid);
	if (!Found) return nullptr;
	return Found->Get();
}
