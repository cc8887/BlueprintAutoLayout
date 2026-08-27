// BALKnotTrackRouter.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UEdGraph;
struct FBALSettings;

/** Creates deterministic reroute tracks for long, shared Pure outputs. */
class BLUEPRINTAUTOLAYOUT_API FBALKnotTrackRouter
{
public:
	/** Rebuild plugin-owned tracks after node positions have been committed. */
	static int32 RouteSharedPureOutputs(UEdGraph* Graph, const FBALSettings& Settings);
};
