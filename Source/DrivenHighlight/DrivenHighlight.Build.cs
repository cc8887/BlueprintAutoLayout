// DrivenHighlight.Build.cs
// Copyright (c) 2026 ccc887. All Rights Reserved.

using UnrealBuildTool;

public class DrivenHighlight : ModuleRules
{
	public DrivenHighlight(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
		});

		// Engine is needed only because UEdGraphNode lives in it. We keep this
		// in PublicDependency so headers exposing UEdGraphNode* compile cleanly
		// for downstream consumers (DrivenHighlightEditor, BlueprintAutoLayout).
		PublicDependencyModuleNames.Add("Engine");
	}
}
