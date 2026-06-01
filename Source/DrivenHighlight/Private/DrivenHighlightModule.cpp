// DrivenHighlightModule.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.
//
// Runtime module entrypoint. The Runtime side carries no state — it just
// declares the log category and provides the type / DrivenId facilities. The
// editor-side service module (DrivenHighlightEditor) does all the heavy
// lifting (Slate overlay, registry, inspector).

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "DrivenHighlightLog.h"

DEFINE_LOG_CATEGORY(LogDrivenHighlight);

class FDrivenHighlightModule : public IModuleInterface
{
public:
	virtual void StartupModule()  override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FDrivenHighlightModule, DrivenHighlight)
