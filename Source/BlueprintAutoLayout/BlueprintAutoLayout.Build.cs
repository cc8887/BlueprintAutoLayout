// BlueprintAutoLayout.Build.cs
// Copyright (c) 2026 ccc887. All Rights Reserved.

using System.Collections.Generic;
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
		// interface. They are optional: when a plugin is not enabled for this
		// target we drop the dependency and compile out the corresponding
		// hook via the WITH_<PLUGIN> macros below.
		HashSet<string> EnabledPlugins = GetEnabledProjectPluginNames(Target);
		AddOptionalPluginModule(EnabledPlugins, "BlueprintLisp", "WITH_BLUEPRINTLISP");
		AddOptionalPluginModule(EnabledPlugins, "AnimBP2FP",     "WITH_ANIMBP2FP");
		AddOptionalPluginModule(EnabledPlugins, "MatBP2FP",      "WITH_MATBP2FP");

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

			// Sister modules (DSL-agnostic highlight subsystem)
			"DrivenHighlight",
			"DrivenHighlightEditor",
		});
	}

	/// <summary>
	/// Adds <paramref name="ModuleName"/> as a public dependency and defines
	/// <paramref name="DefineName"/>=1 only if the corresponding plugin is
	/// enabled for the target; otherwise defines it =0 so the source can
	/// compile the integration out.
	/// </summary>
	private void AddOptionalPluginModule(HashSet<string> EnabledPlugins, string ModuleName, string DefineName)
	{
		if (EnabledPlugins.Contains(ModuleName))
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
	/// Returns the set of project plugin names that are actually enabled for
	/// this target, using UBT's own plugin discovery and enablement logic.
	///
	/// This is authoritative: it honours the .uproject "Enabled" flag,
	/// per-platform / per-target / per-configuration filtering, and — unlike a
	/// naive recursive file search — it ignores stray *.uplugin copies that
	/// live under packaged-output or intermediate folders (UBT stops
	/// descending a directory subtree once it finds a .uplugin there).
	/// </summary>
	private static HashSet<string> GetEnabledProjectPluginNames(ReadOnlyTargetRules Target)
	{
		HashSet<string> Result = new HashSet<string>(System.StringComparer.OrdinalIgnoreCase);

		// A project file is required to resolve project plugins; if there is
		// none (e.g. building an engine-only target) treat all optional
		// integrations as absent.
		if (Target.ProjectFile == null)
		{
			return Result;
		}

		ProjectDescriptor Project = ProjectDescriptor.FromFile(Target.ProjectFile);
		var ProjectDir = Target.ProjectFile.Directory;

		foreach (PluginInfo Plugin in Plugins.ReadProjectPlugins(ProjectDir))
		{
			if (Plugins.IsPluginEnabledForTarget(
				Plugin, Project, Target.Platform, Target.Configuration, Target.Type))
			{
				Result.Add(Plugin.Name);
			}
		}

		return Result;
	}
}
