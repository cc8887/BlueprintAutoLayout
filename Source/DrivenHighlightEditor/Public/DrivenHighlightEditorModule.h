// DrivenHighlightEditorModule.h
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class IDrivenHighlightService;
class IDrivenHighlightInspector;

class DRIVENHIGHLIGHTEDITOR_API FDrivenHighlightEditorModule : public IModuleInterface
{
public:
	static FDrivenHighlightEditorModule& Get();
	static bool IsAvailable();

	// IModuleInterface
	virtual void StartupModule()  override;
	virtual void ShutdownModule() override;

	/** Service-side interface (used by hooks / consumers). */
	IDrivenHighlightService&   GetService();

	/** Inspector-side interface (used by automation tests). */
	IDrivenHighlightInspector& GetInspector();

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
