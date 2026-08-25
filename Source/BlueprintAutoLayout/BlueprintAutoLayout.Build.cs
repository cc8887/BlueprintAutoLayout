// BlueprintAutoLayout.Build.cs
// Copyright (c) 2026 ccc887. All Rights Reserved.

using System.Collections.Generic;
using UnrealBuildTool;
#if UE_5_0_OR_LATER
using EpicGames.Core;
#else
using Tools.DotNETCommon;
#endif

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
		HashSet<string> EnabledPlugins = GetEnabledOptionalPluginNames(Target);
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
	/// Returns optional integration plugins available to this target. Project
	/// plugins must be enabled; compatible engine plugins are available through
	/// this plugin's enabled optional references.
	///
	/// UBT discovery avoids stray *.uplugin copies under packaged-output or
	/// intermediate folders, and also covers dependencies temporarily installed
	/// under Engine/Plugins by BuildPlugin-based CI.
	/// </summary>
	private HashSet<string> GetEnabledOptionalPluginNames(ReadOnlyTargetRules Target)
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

		// BuildPlugin dependencies are exposed under Engine/Plugins while the
		// foreign plugin is compiled in UAT's generated host project. They are
		// enabled by this plugin's optional references, but are not returned by
		// ReadProjectPlugins, so include compatible engine plugins as well.
		foreach (PluginInfo Plugin in Plugins.ReadEnginePlugins(new DirectoryReference(EngineDirectory)))
		{
			if (Plugin.Descriptor.SupportsTargetPlatform(Target.Platform))
			{
				Result.Add(Plugin.Name);
			}
		}

		return Result;
	}
}
