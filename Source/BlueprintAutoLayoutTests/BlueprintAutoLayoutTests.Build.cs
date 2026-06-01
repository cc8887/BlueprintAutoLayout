// BlueprintAutoLayoutTests.Build.cs
// Copyright (c) 2026 ccc887. All Rights Reserved.

using UnrealBuildTool;

public class BlueprintAutoLayoutTests : ModuleRules
{
	public BlueprintAutoLayoutTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Only build in editor configurations
		if (!Target.bBuildEditor)
		{
			Type = ModuleType.External;
			return;
		}

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// The plugin under test
			"BlueprintAutoLayout",

			// Sister modules (highlight subsystem under test)
			"DrivenHighlight",
			"DrivenHighlightEditor",

			// Graph infrastructure (for UEdGraph, UEdGraphNode, UEdGraphPin, UEdGraphNode_Comment)
			"UnrealEd",
			"BlueprintGraph",
			"GraphEditor",
			"Kismet",
			"KismetCompiler",

			// Automation framework
			"AutomationController",
		});
	}
}
