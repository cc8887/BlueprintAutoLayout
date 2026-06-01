// BlueprintAutoLayout.Build.cs
// Copyright (c) 2026 ccc887. All Rights Reserved.

using System.IO;
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
		});

		// ── Optional DSL-plugin integrations ──────────────────────────
		// Each of these plugins exposes an identical import-lifecycle hook
		// interface. They are optional: when a plugin is not present in the
		// project we drop the dependency and compile out the corresponding
		// hook via the WITH_<PLUGIN> macros below.
		AddOptionalPluginModule("BlueprintLisp", "WITH_BLUEPRINTLISP");
		AddOptionalPluginModule("AnimBP2FP",     "WITH_ANIMBP2FP");
		AddOptionalPluginModule("MatBP2FP",      "WITH_MATBP2FP");

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

	/// <summary>
	/// Adds <paramref name="ModuleName"/> as a public dependency and defines
	/// <paramref name="DefineName"/>=1 only if the corresponding plugin is
	/// discoverable; otherwise defines it =0 so the source can compile the
	/// integration out.
	/// </summary>
	private void AddOptionalPluginModule(string ModuleName, string DefineName)
	{
		if (IsPluginAvailable(ModuleName))
		{
			PublicDependencyModuleNames.Add(ModuleName);
			PublicDefinitions.Add(DefineName + "=1");
		}
		else
		{
			PublicDefinitions.Add(DefineName + "=0");
		}
	}

	/// <summary>
	/// Detects whether a sibling plugin (a directory containing
	/// "&lt;Name&gt;.uplugin") exists under the same Plugins root as this
	/// plugin. This keeps the check engine-version agnostic and avoids
	/// relying on UBT-internal plugin enumeration APIs.
	/// </summary>
	private bool IsPluginAvailable(string PluginName)
	{
		// ModuleDirectory = .../BlueprintAutoLayout/Source/BlueprintAutoLayout
		// Plugins root     = .../  (parent of the BlueprintAutoLayout plugin dir)
		DirectoryInfo PluginDir = new DirectoryInfo(
			Path.Combine(ModuleDirectory, "..", "..")); // BlueprintAutoLayout
		DirectoryInfo PluginsRoot = PluginDir.Parent;   // Plugins/
		if (PluginsRoot == null || !PluginsRoot.Exists)
		{
			return false;
		}

		// Search any descendant for "<PluginName>.uplugin" (plugins may be
		// nested in sub-folders).
		foreach (FileInfo Uplugin in PluginsRoot.GetFiles(
			PluginName + ".uplugin", SearchOption.AllDirectories))
		{
			return true;
		}
		return false;
	}
}
