// DrivenHighlightService.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "IDrivenHighlightService.h"
#include "IDrivenHighlightInspector.h"
#include "DrivenHighlightEditorModule.h"
#include "DrivenHighlightLog.h"

#include "DrivenHighlightRegistry.h"
#include "NodeRegistry.h"
#include "DrivenId.h"
#include "Bootstrap/OverlayWindowInjector.h"

#include "EdGraph/EdGraphNode.h"
#include "UObject/Object.h"
#include "Containers/Ticker.h"
#include "Widgets/SWindow.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Concrete service. Holds the registries, drives expiry, exposes the API.
// ─────────────────────────────────────────────────────────────────────────────
class FDrivenHighlightService : public IDrivenHighlightService
{
public:
	FDrivenHighlightService(FDrivenHighlightRegistry* InRegistry,
	                         FDrivenNodeRegistry*      InNodeRegistry,
	                         FOverlayWindowInjector*   InInjector)
		: Registry(InRegistry)
		, NodeRegistry(InNodeRegistry)
		, Injector(InInjector)
	{
	}

	virtual FName ShowNode(const UEdGraphNode* Node,
	                       EDrivenChangeType ChangeType,
	                       float DurationSeconds) override
	{
		if (!Node) return NAME_None;

		// Make the node addressable for resolve-by-id lookups.
		NodeRegistry->Add(Node);

		FDrivenHighlightSpec Spec;
		Spec.DrivenId         = DrivenId::MakeNode(Node, ChangeType);
		Spec.AnchorType       = EDrivenAnchorType::BlueprintNode;
		Spec.OuterName        = Node->GetOutermostObject()
		                            ? Node->GetOutermostObject()->GetFullName()
		                            : FString();
		Spec.GUIDString       = Node->NodeGuid.ToString();
		Spec.SourceObject     = Node;
		Spec.DebugLabel       = FString::Printf(TEXT("Node:%s (%s)"),
			*Node->GetName(),
			DrivenId::ToString(ChangeType));
		Spec.DurationSeconds  = DurationSeconds;

		// BlueprintNode anchors cannot be resolved via Widget Tag; the overlay
		// only renders NamedWidget hits today. We still register the spec so
		// IsActive returns true and the inspector can confirm the request.
		// A future revision can add real GUID-based widget lookup.
		Registry->Upsert(Spec);
		UE_LOG(LogDrivenHighlight, Log,
			TEXT("ShowNode  id=%s  label='%s'"),
			*Spec.DrivenId.ToString(), *Spec.DebugLabel);
		return Spec.DrivenId;
	}

	virtual FName ShowNodeProperty(const UEdGraphNode* Node,
	                               FName PropertyDisplayName,
	                               float DurationSeconds) override
	{
		if (!Node || PropertyDisplayName.IsNone()) return NAME_None;

		FDrivenHighlightSpec Spec;
		Spec.DrivenId          = DrivenId::MakeNodeProp(Node, PropertyDisplayName);
		Spec.AnchorType        = EDrivenAnchorType::NamedWidget;
		Spec.WrappedIdentifier = FName(*FString::Printf(TEXT("DetailRowItem.%s"),
			*PropertyDisplayName.ToString()));
		Spec.SourceObject      = Node;
		Spec.DebugLabel        = FString::Printf(TEXT("NodeProp:%s.%s"),
			*Node->GetName(), *PropertyDisplayName.ToString());
		Spec.DurationSeconds   = DurationSeconds;

		Registry->Upsert(Spec);
		UE_LOG(LogDrivenHighlight, Log,
			TEXT("ShowNodeProperty  id=%s  tag=%s  label='%s'"),
			*Spec.DrivenId.ToString(),
			*Spec.WrappedIdentifier.ToString(),
			*Spec.DebugLabel);
		return Spec.DrivenId;
	}

	virtual FName ShowAssetProperty(const UObject* Asset,
	                                FName PropertyDisplayName,
	                                float DurationSeconds) override
	{
		if (!Asset || PropertyDisplayName.IsNone()) return NAME_None;

		FDrivenHighlightSpec Spec;
		Spec.DrivenId          = DrivenId::MakeAssetProp(Asset, PropertyDisplayName);
		Spec.AnchorType        = EDrivenAnchorType::NamedWidget;
		Spec.WrappedIdentifier = FName(*FString::Printf(TEXT("DetailRowItem.%s"),
			*PropertyDisplayName.ToString()));
		Spec.SourceObject      = Asset;
		Spec.DebugLabel        = FString::Printf(TEXT("AssetProp:%s.%s"),
			*Asset->GetName(), *PropertyDisplayName.ToString());
		Spec.DurationSeconds   = DurationSeconds;

		Registry->Upsert(Spec);
		UE_LOG(LogDrivenHighlight, Log,
			TEXT("ShowAssetProperty  id=%s  tag=%s  label='%s'"),
			*Spec.DrivenId.ToString(),
			*Spec.WrappedIdentifier.ToString(),
			*Spec.DebugLabel);
		return Spec.DrivenId;
	}

	virtual void Show(const FDrivenHighlightSpec& Spec) override
	{
		Registry->Upsert(Spec);
	}

	virtual void Hide(FName DrivenId) override
	{
		if (Registry->Remove(DrivenId))
		{
			if (FDrivenHighlightEditorModule::IsAvailable())
			{
				FDrivenHighlightEditorModule::Get().GetInspector()
					.OnHighlightEnded().Broadcast(DrivenId);
			}
		}
	}

	virtual void HideAll() override
	{
		const TArray<FName> All = Registry->GetAllIds();
		Registry->Clear();
		if (FDrivenHighlightEditorModule::IsAvailable())
		{
			auto& Insp = FDrivenHighlightEditorModule::Get().GetInspector();
			for (FName Id : All) Insp.OnHighlightEnded().Broadcast(Id);
		}
	}

	virtual void AttachToWindow(TSharedRef<SWindow> Window) override
	{
		if (Injector) Injector->Attach(Window);
	}

	virtual void DetachFromWindow(TSharedRef<SWindow> Window) override
	{
		if (Injector) Injector->Detach(Window);
	}

private:
	FDrivenHighlightRegistry* Registry     = nullptr;
	FDrivenNodeRegistry*      NodeRegistry = nullptr;
	FOverlayWindowInjector*   Injector     = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Inspector — read-only view onto the same registries the service mutates.
// ─────────────────────────────────────────────────────────────────────────────
class FDrivenHighlightInspector : public IDrivenHighlightInspector
{
public:
	explicit FDrivenHighlightInspector(FDrivenHighlightRegistry* InRegistry)
		: Registry(InRegistry) {}

	virtual bool IsActive(FName DrivenId) const override
	{
		return Registry->Find(DrivenId) != nullptr;
	}

	virtual TWeakPtr<SWidget> GetHighlightedWidget(FName DrivenId) const override
	{
		const FDrivenActiveHighlight* H = Registry->Find(DrivenId);
		return H ? H->BoundWidget : TWeakPtr<SWidget>();
	}

	virtual TArray<FName> GetAllActive() const override
	{
		return Registry->GetAllIds();
	}

	virtual FOnHighlightStarted& OnHighlightStarted() override { return StartedDelegate; }
	virtual FOnHighlightEnded&   OnHighlightEnded()   override { return EndedDelegate;   }

private:
	FDrivenHighlightRegistry* Registry = nullptr;
	FOnHighlightStarted       StartedDelegate;
	FOnHighlightEnded         EndedDelegate;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Static accessors — defer to the module so call sites don't need to include
//  the module header.
// ─────────────────────────────────────────────────────────────────────────────
IDrivenHighlightService& IDrivenHighlightService::Get()
{
	return FDrivenHighlightEditorModule::Get().GetService();
}
bool IDrivenHighlightService::IsAvailable()
{
	return FDrivenHighlightEditorModule::IsAvailable();
}

IDrivenHighlightInspector& IDrivenHighlightInspector::Get()
{
	return FDrivenHighlightEditorModule::Get().GetInspector();
}
bool IDrivenHighlightInspector::IsAvailable()
{
	return FDrivenHighlightEditorModule::IsAvailable();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Module impl — owns the registries, the injector, the service, the inspector,
//  and a Slate ticker that handles auto-expiry.
// ─────────────────────────────────────────────────────────────────────────────
struct FDrivenHighlightEditorModule::FImpl
{
	FDrivenHighlightRegistry  Registry;
	FDrivenNodeRegistry       NodeRegistry;
	FOverlayWindowInjector    Injector;

	TUniquePtr<FDrivenHighlightService>   Service;
	TUniquePtr<FDrivenHighlightInspector> Inspector;

	FTSTicker::FDelegateHandle TickerHandle;

	bool Tick(float /*DeltaTime*/)
	{
		const double Now = FPlatformTime::Seconds();
		const TArray<FName> Expired = Registry.ExpireBefore(Now);
		if (!Expired.IsEmpty() && Inspector.IsValid())
		{
			for (FName Id : Expired)
			{
				Inspector->OnHighlightEnded().Broadcast(Id);
			}
		}

		// Pick up any newly-opened editor windows since last tick. Cheap: most
		// turns this is a no-op constant-size walk over visible top-level
		// windows.
		Injector.RescanAndAttach();
		return true; // keep ticking
	}
};

FDrivenHighlightEditorModule& FDrivenHighlightEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FDrivenHighlightEditorModule>(
		TEXT("DrivenHighlightEditor"));
}

bool FDrivenHighlightEditorModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("DrivenHighlightEditor"));
}

void FDrivenHighlightEditorModule::StartupModule()
{
	Impl = MakeUnique<FImpl>();

	Impl->Service   = MakeUnique<FDrivenHighlightService>(
		&Impl->Registry, &Impl->NodeRegistry, &Impl->Injector);
	Impl->Inspector = MakeUnique<FDrivenHighlightInspector>(&Impl->Registry);

	Impl->Injector.Init(&Impl->Registry, &Impl->NodeRegistry);

	Impl->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(Impl.Get(), &FImpl::Tick),
		/*Delay=*/ 0.25f /* expiry resolution */);

	UE_LOG(LogDrivenHighlight, Log, TEXT("DrivenHighlightEditor module started."));
}

void FDrivenHighlightEditorModule::ShutdownModule()
{
	if (!Impl) return;

	if (Impl->TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Impl->TickerHandle);
	}
	Impl->Injector.Shutdown();
	Impl->Service.Reset();
	Impl->Inspector.Reset();
	Impl.Reset();

	UE_LOG(LogDrivenHighlight, Log, TEXT("DrivenHighlightEditor module shut down."));
}

IDrivenHighlightService& FDrivenHighlightEditorModule::GetService()
{
	check(Impl && Impl->Service);
	return *Impl->Service;
}

IDrivenHighlightInspector& FDrivenHighlightEditorModule::GetInspector()
{
	check(Impl && Impl->Inspector);
	return *Impl->Inspector;
}

IMPLEMENT_MODULE(FDrivenHighlightEditorModule, DrivenHighlightEditor)
