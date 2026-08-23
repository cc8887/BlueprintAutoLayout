// BALCommentHandler.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "BALTypes.h"

/** Recomputes nested comment bounds after node layout and collision resolution. */
class BLUEPRINTAUTOLAYOUT_API FBALCommentHandler
{
public:
	static void UpdateBounds(TMap<UEdGraphNode*, FBALNode>& Proxies,
	                         const TArray<FBALCommentGroup>& Groups,
	                         const FBALSettings& Settings);
};
