// BlueprintAutoLayoutModule.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Commands/Commands.h"
#include "BlueprintLispModule.h"
#include "AnimBP2FPModule.h"
#include "MatBP2FPModule.h"

class UEdGraph;
class UEdGraphNode;

// ─────────────────────────────────────────────────────────────
//  Commands
// ─────────────────────────────────────────────────────────────

class FBALCommands : public TCommands<FBALCommands>
{
public:
	FBALCommands();

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> LayoutGraph;
	TSharedPtr<FUICommandInfo> LayoutSelection;
};

// ─────────────────────────────────────────────────────────────
//  Module
// ─────────────────────────────────────────────────────────────

class BLUEPRINTAUTOLAYOUT_API FBlueprintAutoLayoutModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	struct FHookRegistrationState;

	void RegisterMenuExtensions();
	void UnregisterMenuExtensions();
	void RegisterImportHooks();
	void UnregisterImportHooks();

	// Graph editor toolbar / right-click integration
	void ExtendGraphEditorContextMenu();

	// Command handlers
	void OnLayoutGraph();
	void OnLayoutSelection();

	/** Get the currently focused UEdGraph from any Blueprint/Material/Anim editor. */
	UEdGraph* GetActiveGraph() const;
	TSet<UEdGraphNode*> GetSelectedNodes(UEdGraph* Graph) const;

	TSharedPtr<FUICommandList> CommandList;
	FDelegateHandle MenuExtenderHandle;
	FDelegateHandle AssetEditorOpenedHandle;
	bool bUiRegistered = false;
	TUniquePtr<FHookRegistrationState> HookRegistrationState;

	void OnAssetEditorOpened(UObject* Asset);
};
