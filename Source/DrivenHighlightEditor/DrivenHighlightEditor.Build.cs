// DrivenHighlightEditor.Build.cs
// Copyright (c) 2026 ccc887. All Rights Reserved.

using UnrealBuildTool;

public class DrivenHighlightEditor : ModuleRules
{
	public DrivenHighlightEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Editor-only — never link in non-editor builds.
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
			"Slate",
			"SlateCore",

			// Sister runtime module that defines DrivenId / FDrivenHighlightSpec.
			"DrivenHighlight",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"EditorSubsystem",
			"GraphEditor",
			"BlueprintGraph",
			"Kismet",            // FKismetEditorUtilities
			"PropertyEditor",    // detail panel tag conventions (DetailRowItem.<X>)
			"Projects",
			"InputCore",
			"ApplicationCore",
		});
	}
}
