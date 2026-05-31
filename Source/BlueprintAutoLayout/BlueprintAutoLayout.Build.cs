// BlueprintAutoLayout.Build.cs
// Copyright (c) 2026 ccc887. All Rights Reserved.

using UnrealBuildTool;

public class BlueprintAutoLayout : ModuleRules
{
	public BlueprintAutoLayout(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"BlueprintLisp",
			"AnimBP2FP",
			"MatBP2FP",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor foundation
			"UnrealEd",
			"EditorSubsystem",

			// Graph / Blueprint
			"BlueprintGraph",      // UEdGraphNode, UK2Node_*
			"GraphEditor",         // SGraphEditor, SGraphPanel
			"Kismet",              // FBlueprintEditor (the editor window class)
			"KismetCompiler",      // node size helpers

			// Material graph (MaterialGraph classes live in UnrealEd in UE5)
			"MaterialEditor",      // IMaterialEditor for graph access

			// UI & menus
			"Slate",
			"SlateCore",
			"EditorStyle",
			"ToolMenus",
			"InputCore",

			// Settings
			"DeveloperSettings",
		});
	}
}
