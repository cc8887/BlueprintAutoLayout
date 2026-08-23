// BlueprintAutoLayoutModule.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "BlueprintAutoLayoutModule.h"
#include "BlueprintAutoLayoutEngine.h"
#include "BALConstraintCollector.h"

#include "Highlight/BALHighlightAdapter.h"

#if WITH_BLUEPRINTLISP
#include "BlueprintLispModule.h"
#endif
#if WITH_ANIMBP2FP
#include "AnimBP2FPModule.h"
#endif
#if WITH_MATBP2FP
#include "MatBP2FPModule.h"
#endif

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"

#include "ToolMenus.h"
#include "ToolMenuContext.h"

#include "GraphEditor.h"
#include "EditorSubsystem.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"

#include "IMaterialEditor.h"
#include "MaterialGraph/MaterialGraph.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"

#include "Misc/MessageDialog.h"
#include "Runtime/Launch/Resources/Version.h"
#if ENGINE_MAJOR_VERSION >= 5
#include "Styling/AppStyle.h"
#else
#include "EditorStyleSet.h"
#endif
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectHash.h"

#define LOCTEXT_NAMESPACE "BlueprintAutoLayout"

#define BAL_ANY_DSL_INTEGRATION (WITH_BLUEPRINTLISP || WITH_ANIMBP2FP || WITH_MATBP2FP)

namespace
{
	FName GetBALStyleSetName()
	{
#if ENGINE_MAJOR_VERSION >= 5
		return FAppStyle::GetAppStyleSetName();
#else
		return FEditorStyle::GetStyleSetName();
#endif
	}

#if BAL_ANY_DSL_INTEGRATION
	const FName AutoLayoutBehaviorName(TEXT("AutoLayout"));
	constexpr int32 AutoLayoutEarlyPriority = 100;
#endif // BAL_ANY_DSL_INTEGRATION

#if WITH_BLUEPRINTLISP
	TSet<UEdGraphNode*> CollectChangedGraphNodes(const TArray<BlueprintLispImportLifecycle::FImportNodeChange>& Changes)
	{
		TSet<UEdGraphNode*> Nodes;
		for (const BlueprintLispImportLifecycle::FImportNodeChange& Change : Changes)
		{
			if (Change.Node)
			{
				Nodes.Add(Change.Node);
			}
		}
		return Nodes;
	}
#endif // WITH_BLUEPRINTLISP

#if WITH_ANIMBP2FP
	TSet<UEdGraphNode*> CollectChangedGraphNodes(const TArray<AnimBP2FPImportLifecycle::FImportNodeChange>& Changes)
	{
		TSet<UEdGraphNode*> Nodes;
		for (const AnimBP2FPImportLifecycle::FImportNodeChange& Change : Changes)
		{
			if (Change.Node)
			{
				Nodes.Add(Change.Node);
			}
		}
		return Nodes;
	}
#endif // WITH_ANIMBP2FP

#if WITH_MATBP2FP
	TSet<UEdGraphNode*> CollectChangedGraphNodes(const TArray<MatBP2FPImportLifecycle::FImportNodeChange>& Changes)
	{
		TSet<UEdGraphNode*> Nodes;
		for (const MatBP2FPImportLifecycle::FImportNodeChange& Change : Changes)
		{
			if (Change.Node)
			{
				Nodes.Add(Change.Node);
			}
		}
		return Nodes;
	}
#endif // WITH_MATBP2FP

#if BAL_ANY_DSL_INTEGRATION
	template <typename ContextType>
	void RunGraphLayout(const ContextType& Context, const TSet<UEdGraphNode*>& ChangedNodes)
	{
		if (!Context.TargetGraph)
		{
			return;
		}
		if (!Context.RequestedBehaviors.Contains(AutoLayoutBehaviorName))
		{
			return;
		}
		if (ChangedNodes.Num() == 0)
		{
			FBlueprintAutoLayoutEngine::Layout(Context.TargetGraph);
			return;
		}
		FBlueprintAutoLayoutEngine::LayoutSelection(Context.TargetGraph, ChangedNodes);
	}
#endif // BAL_ANY_DSL_INTEGRATION

#if WITH_BLUEPRINTLISP
	class FBlueprintLispAutoLayoutHook : public BlueprintLispImportLifecycle::IImportLifecycleHook
	{
	public:
		virtual int32 GetPriority(BlueprintLispImportLifecycle::EImportLifecyclePhase Phase) const override
		{
			return Phase == BlueprintLispImportLifecycle::EImportLifecyclePhase::PostNodeChanges ? AutoLayoutEarlyPriority : 0;
		}

		virtual void OnNodePhase(const BlueprintLispImportLifecycle::FImportNodePhaseEvent& Event) override
		{
			if (Event.Phase != BlueprintLispImportLifecycle::EImportLifecyclePhase::PostNodeChanges)
			{
				return;
			}
			RunGraphLayout(Event.Context, CollectChangedGraphNodes(Event.Changes));
		}
	};
#endif // WITH_BLUEPRINTLISP

#if WITH_ANIMBP2FP
	class FAnimBP2FPAutoLayoutHook : public AnimBP2FPImportLifecycle::IImportLifecycleHook
	{
	public:
		virtual int32 GetPriority(AnimBP2FPImportLifecycle::EImportLifecyclePhase Phase) const override
		{
			return Phase == AnimBP2FPImportLifecycle::EImportLifecyclePhase::PostNodeChanges ? AutoLayoutEarlyPriority : 0;
		}

		virtual void OnNodePhase(const AnimBP2FPImportLifecycle::FImportNodePhaseEvent& Event) override
		{
			if (Event.Phase != AnimBP2FPImportLifecycle::EImportLifecyclePhase::PostNodeChanges)
			{
				return;
			}
			RunGraphLayout(Event.Context, CollectChangedGraphNodes(Event.Changes));
		}
	};
#endif // WITH_ANIMBP2FP

#if WITH_MATBP2FP
	class FMatBP2FPAutoLayoutHook : public MatBP2FPImportLifecycle::IImportLifecycleHook
	{
	public:
		virtual int32 GetPriority(MatBP2FPImportLifecycle::EImportLifecyclePhase Phase) const override
		{
			return Phase == MatBP2FPImportLifecycle::EImportLifecyclePhase::PostNodeChanges ? AutoLayoutEarlyPriority : 0;
		}

		virtual void OnNodePhase(const MatBP2FPImportLifecycle::FImportNodePhaseEvent& Event) override
		{
			if (Event.Phase != MatBP2FPImportLifecycle::EImportLifecyclePhase::PostNodeChanges)
			{
				return;
			}
			RunGraphLayout(Event.Context, CollectChangedGraphNodes(Event.Changes));
		}
	};
#endif // WITH_MATBP2FP
}

struct FBlueprintAutoLayoutModule::FHookRegistrationState
{
#if WITH_BLUEPRINTLISP
	TSharedPtr<FBlueprintLispAutoLayoutHook> BlueprintLispHook;
	BlueprintLispImportLifecycle::FImportLifecycleHookHandle BlueprintLispHandle;

	TSharedPtr<FBALBlueprintLispHighlightHook> BlueprintLispHighlightHook;
	BlueprintLispImportLifecycle::FImportLifecycleHookHandle BlueprintLispHighlightHandle;
#endif
#if WITH_ANIMBP2FP
	TSharedPtr<FAnimBP2FPAutoLayoutHook> AnimBP2FPHook;
	AnimBP2FPImportLifecycle::FImportLifecycleHookHandle AnimBP2FPHandle;

	TSharedPtr<FBALAnimBP2FPHighlightHook> AnimBP2FPHighlightHook;
	AnimBP2FPImportLifecycle::FImportLifecycleHookHandle AnimBP2FPHighlightHandle;
#endif
#if WITH_MATBP2FP
	TSharedPtr<FMatBP2FPAutoLayoutHook> MatBP2FPHook;
	MatBP2FPImportLifecycle::FImportLifecycleHookHandle MatBP2FPHandle;

	TSharedPtr<FBALMatBP2FPHighlightHook> MatBP2FPHighlightHook;
	MatBP2FPImportLifecycle::FImportLifecycleHookHandle MatBP2FPHighlightHandle;
#endif
};

FBALCommands::FBALCommands()
	: TCommands<FBALCommands>(
		TEXT("BlueprintAutoLayout"),
		LOCTEXT("BlueprintAutoLayout", "Blueprint Auto Layout"),
		NAME_None,
		GetBALStyleSetName())
{}

void FBALCommands::RegisterCommands()
{
	UI_COMMAND(LayoutGraph,
		"Auto Layout Graph",
		"Automatically arrange all nodes in the current graph",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::L));

	UI_COMMAND(LayoutSelection,
		"Auto Layout Selection",
		"Arrange only the selected nodes, keeping others in place",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Control | EModifierKey::Shift | EModifierKey::Alt, EKeys::L));
}

void FBlueprintAutoLayoutModule::StartupModule()
{
	RegisterImportHooks();

	if (IsRunningCommandlet() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	FBALCommands::Register();

	CommandList = MakeShared<FUICommandList>();
	CommandList->MapAction(
		FBALCommands::Get().LayoutGraph,
		FExecuteAction::CreateRaw(this, &FBlueprintAutoLayoutModule::OnLayoutGraph));
	CommandList->MapAction(
		FBALCommands::Get().LayoutSelection,
		FExecuteAction::CreateRaw(this, &FBlueprintAutoLayoutModule::OnLayoutSelection));

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FBlueprintAutoLayoutModule::RegisterMenuExtensions));

	if (GEditor)
	{
		if (UAssetEditorSubsystem* AESub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditorOpenedHandle = AESub->OnAssetEditorOpened().AddRaw(
				this, &FBlueprintAutoLayoutModule::OnAssetEditorOpened);
		}
	}

	bUiRegistered = true;
}

void FBlueprintAutoLayoutModule::RegisterImportHooks()
{
	HookRegistrationState = MakeUnique<FHookRegistrationState>();

#if WITH_BLUEPRINTLISP
	if (FBlueprintLispModule::IsAvailable())
	{
		HookRegistrationState->BlueprintLispHook = MakeShared<FBlueprintLispAutoLayoutHook>();
		HookRegistrationState->BlueprintLispHandle = FBlueprintLispModule::Get().RegisterImportLifecycleHook(
			HookRegistrationState->BlueprintLispHook.ToSharedRef());

		HookRegistrationState->BlueprintLispHighlightHook = MakeShared<FBALBlueprintLispHighlightHook>();
		HookRegistrationState->BlueprintLispHighlightHandle = FBlueprintLispModule::Get().RegisterImportLifecycleHook(
			HookRegistrationState->BlueprintLispHighlightHook.ToSharedRef());
	}
#endif

#if WITH_ANIMBP2FP
	if (FAnimBP2FPModule::IsAvailable())
	{
		HookRegistrationState->AnimBP2FPHook = MakeShared<FAnimBP2FPAutoLayoutHook>();
		HookRegistrationState->AnimBP2FPHandle = FAnimBP2FPModule::Get().RegisterImportLifecycleHook(
			HookRegistrationState->AnimBP2FPHook.ToSharedRef());

		HookRegistrationState->AnimBP2FPHighlightHook = MakeShared<FBALAnimBP2FPHighlightHook>();
		HookRegistrationState->AnimBP2FPHighlightHandle = FAnimBP2FPModule::Get().RegisterImportLifecycleHook(
			HookRegistrationState->AnimBP2FPHighlightHook.ToSharedRef());
	}
#endif

#if WITH_MATBP2FP
	if (FMatBP2FPModule::IsAvailable())
	{
		HookRegistrationState->MatBP2FPHook = MakeShared<FMatBP2FPAutoLayoutHook>();
		HookRegistrationState->MatBP2FPHandle = FMatBP2FPModule::Get().RegisterImportLifecycleHook(
			HookRegistrationState->MatBP2FPHook.ToSharedRef());

		HookRegistrationState->MatBP2FPHighlightHook = MakeShared<FBALMatBP2FPHighlightHook>();
		HookRegistrationState->MatBP2FPHighlightHandle = FMatBP2FPModule::Get().RegisterImportLifecycleHook(
			HookRegistrationState->MatBP2FPHighlightHook.ToSharedRef());
	}
#endif
}

void FBlueprintAutoLayoutModule::UnregisterImportHooks()
{
	if (!HookRegistrationState)
	{
		return;
	}

#if WITH_BLUEPRINTLISP
	if (HookRegistrationState->BlueprintLispHandle.IsValid() && FBlueprintLispModule::IsAvailable())
	{
		FBlueprintLispModule::Get().UnregisterImportLifecycleHook(HookRegistrationState->BlueprintLispHandle);
	}
	if (HookRegistrationState->BlueprintLispHighlightHandle.IsValid() && FBlueprintLispModule::IsAvailable())
	{
		FBlueprintLispModule::Get().UnregisterImportLifecycleHook(HookRegistrationState->BlueprintLispHighlightHandle);
	}
#endif

#if WITH_ANIMBP2FP
	if (HookRegistrationState->AnimBP2FPHandle.IsValid() && FAnimBP2FPModule::IsAvailable())
	{
		FAnimBP2FPModule::Get().UnregisterImportLifecycleHook(HookRegistrationState->AnimBP2FPHandle);
	}
	if (HookRegistrationState->AnimBP2FPHighlightHandle.IsValid() && FAnimBP2FPModule::IsAvailable())
	{
		FAnimBP2FPModule::Get().UnregisterImportLifecycleHook(HookRegistrationState->AnimBP2FPHighlightHandle);
	}
#endif

#if WITH_MATBP2FP
	if (HookRegistrationState->MatBP2FPHandle.IsValid() && FMatBP2FPModule::IsAvailable())
	{
		FMatBP2FPModule::Get().UnregisterImportLifecycleHook(HookRegistrationState->MatBP2FPHandle);
	}
	if (HookRegistrationState->MatBP2FPHighlightHandle.IsValid() && FMatBP2FPModule::IsAvailable())
	{
		FMatBP2FPModule::Get().UnregisterImportLifecycleHook(HookRegistrationState->MatBP2FPHighlightHandle);
	}
#endif

	HookRegistrationState.Reset();
}

void FBlueprintAutoLayoutModule::OnAssetEditorOpened(UObject* /*Asset*/)
{
	RegisterMenuExtensions();
}

void FBlueprintAutoLayoutModule::ShutdownModule()
{
	if (bUiRegistered && GEditor)
	{
		if (UAssetEditorSubsystem* AESub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AESub->OnAssetEditorOpened().Remove(AssetEditorOpenedHandle);
		}
	}

	if (bUiRegistered)
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		FBALCommands::Unregister();
		bUiRegistered = false;
	}

	UnregisterImportHooks();
}

void FBlueprintAutoLayoutModule::RegisterMenuExtensions()
{
	if (!bUiRegistered)
	{
		return;
	}

	FToolMenuOwnerScoped OwnerScoped(this);

	FUIAction ActionLayoutGraph(
		FExecuteAction::CreateRaw(this, &FBlueprintAutoLayoutModule::OnLayoutGraph));
	FUIAction ActionLayoutSelection(
		FExecuteAction::CreateRaw(this, &FBlueprintAutoLayoutModule::OnLayoutSelection));

	static const TCHAR* ToolbarCandidates[] = {
		TEXT("AssetEditor.BlueprintEditor.ToolBar"),
		TEXT("AssetEditor.DefaultToolBar"),
	};
	for (const TCHAR* Name : ToolbarCandidates)
	{
		if (UToolMenus::Get()->IsMenuRegistered(FName(Name)))
		{
			if (UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(FName(Name)))
			{
				FToolMenuSection& Section = Toolbar->FindOrAddSection("BlueprintAutoLayout");
				Section.AddEntry(FToolMenuEntry::InitToolBarButton(
					"BAL_LayoutGraph",
					ActionLayoutGraph,
					LOCTEXT("LayoutGraph", "Auto Layout"),
					LOCTEXT("LayoutGraphTip", "Auto-arrange all nodes in the current graph (Ctrl+Shift+L)"),
					FSlateIcon(GetBALStyleSetName(), "BlueprintEditor.AutoArrange")));
				Section.AddEntry(FToolMenuEntry::InitToolBarButton(
					"BAL_LayoutSelection",
					ActionLayoutSelection,
					LOCTEXT("LayoutSel", "Layout Selection"),
					LOCTEXT("LayoutSelTip", "Auto-arrange only the selected nodes"),
					FSlateIcon(GetBALStyleSetName(), "BlueprintEditor.AutoArrange")));
			}
		}
	}

	if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("GraphEditor.GraphNodeContextMenu"))
	{
		FToolMenuSection& Section = Menu->FindOrAddSection("BlueprintAutoLayout");
		Section.Label = LOCTEXT("BALSection", "Auto Layout");
		Section.AddMenuEntry("BAL_LayoutGraph2",
			LOCTEXT("LayoutGraph2", "Auto Layout Graph"),
			LOCTEXT("LayoutGraphTip2", "Auto-arrange all nodes"),
			FSlateIcon(GetBALStyleSetName(), "BlueprintEditor.AutoArrange"),
			ActionLayoutGraph);
		Section.AddMenuEntry("BAL_LayoutSelection2",
			LOCTEXT("LayoutSel2", "Auto Layout Selection"),
			LOCTEXT("LayoutSelTip2", "Auto-arrange selected nodes only"),
			FSlateIcon(GetBALStyleSetName(), "BlueprintEditor.AutoArrange"),
			ActionLayoutSelection);
	}
}

void FBlueprintAutoLayoutModule::UnregisterMenuExtensions()
{
}

void FBlueprintAutoLayoutModule::ExtendGraphEditorContextMenu()
{
}

void FBlueprintAutoLayoutModule::OnLayoutGraph()
{
	UEdGraph* Graph = GetActiveGraph();
	if (!Graph)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoGraph", "No graph is currently focused. Please open a Blueprint, Material, or Animation Blueprint editor."));
		return;
	}

	FBlueprintAutoLayoutEngine::Layout(Graph);
}

void FBlueprintAutoLayoutModule::OnLayoutSelection()
{
	UEdGraph* Graph = GetActiveGraph();
	if (!Graph)
	{
		return;
	}

	TSet<UEdGraphNode*> Selection = GetSelectedNodes(Graph);
	if (Selection.Num() == 0)
	{
		FBlueprintAutoLayoutEngine::Layout(Graph);
		return;
	}

	FBlueprintAutoLayoutEngine::LayoutSelection(Graph, Selection);
}

UEdGraph* FBlueprintAutoLayoutModule::GetActiveGraph() const
{
	if (!GEditor || !FSlateApplication::IsInitialized())
	{
		return nullptr;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem =
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return nullptr;
	}

	TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
	for (UObject* Asset : EditedAssets)
	{
		if (!Asset)
		{
			continue;
		}

		IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Asset, false);
		if (!EditorInstance)
		{
			continue;
		}

		if (EditorInstance->GetEditorName() == FName("BlueprintEditor"))
		{
			if (FBlueprintEditor* BPEditor = static_cast<FBlueprintEditor*>(EditorInstance))
			{
				if (UEdGraph* Graph = BPEditor->GetFocusedGraph())
				{
					return Graph;
				}
			}
		}

		if (EditorInstance->GetEditorName() == FName("MaterialEditor"))
		{
			TArray<UObject*> SubObjects;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
			GetObjectsWithOuter(Asset, SubObjects, EGetObjectsFlags::None);
#else
			GetObjectsWithOuter(Asset, SubObjects, false);
#endif
			for (UObject* Sub : SubObjects)
			{
				if (UMaterialGraph* MatGraph = Cast<UMaterialGraph>(Sub))
				{
					return MatGraph;
				}
			}
		}
	}

	return nullptr;
}

TSet<UEdGraphNode*> FBlueprintAutoLayoutModule::GetSelectedNodes(UEdGraph* Graph) const
{
	TSet<UEdGraphNode*> Selected;
	if (!Graph || !GEditor)
	{
		return Selected;
	}

	UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!Sub)
	{
		return Selected;
	}

	for (UObject* Asset : Sub->GetAllEditedAssets())
	{
		if (!Asset)
		{
			continue;
		}
		IAssetEditorInstance* Inst = Sub->FindEditorForAsset(Asset, false);
		if (!Inst)
		{
			continue;
		}

		if (Inst->GetEditorName() == FName("BlueprintEditor"))
		{
			FBlueprintEditor* BPEditor = static_cast<FBlueprintEditor*>(Inst);
			UEdGraph* FocusedGraph = BPEditor->GetFocusedGraph();
			if (FocusedGraph == Graph)
			{
				FGraphPanelSelectionSet RawSet = BPEditor->GetSelectedNodes();
				for (UObject* Obj : RawSet)
				{
					if (UEdGraphNode* N = Cast<UEdGraphNode>(Obj))
					{
						Selected.Add(N);
					}
				}
				return Selected;
			}
		}
	}

	return Selected;
}

#undef LOCTEXT_NAMESPACE
#undef BAL_ANY_DSL_INTEGRATION

IMPLEMENT_MODULE(FBlueprintAutoLayoutModule, BlueprintAutoLayout)
