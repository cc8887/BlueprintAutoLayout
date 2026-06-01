// DrivenHighlightRegistry.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "DrivenHighlightRegistry.h"

void FDrivenHighlightRegistry::Upsert(const FDrivenHighlightSpec& Spec)
{
	if (Spec.DrivenId.IsNone()) return;

	FDrivenActiveHighlight& Slot = Active.FindOrAdd(Spec.DrivenId);
	Slot.Spec        = Spec;
	Slot.StartTime   = FPlatformTime::Seconds();
	Slot.BoundWidget.Reset();
	Slot.bHasFiredStarted = false;
}

bool FDrivenHighlightRegistry::Remove(FName DrivenId)
{
	return Active.Remove(DrivenId) > 0;
}

void FDrivenHighlightRegistry::Clear()
{
	Active.Empty();
}

const FDrivenActiveHighlight* FDrivenHighlightRegistry::Find(FName DrivenId) const
{
	return Active.Find(DrivenId);
}

FDrivenActiveHighlight* FDrivenHighlightRegistry::Find(FName DrivenId)
{
	return Active.Find(DrivenId);
}

TArray<FName> FDrivenHighlightRegistry::GetAllIds() const
{
	TArray<FName> Out;
	Out.Reserve(Active.Num());
	for (const auto& Pair : Active) Out.Add(Pair.Key);
	return Out;
}

TArray<FName> FDrivenHighlightRegistry::ExpireBefore(double NowSeconds)
{
	TArray<FName> Removed;
	for (auto It = Active.CreateIterator(); It; ++It)
	{
		const FDrivenActiveHighlight& H = It.Value();
		const float D = H.Spec.DurationSeconds;
		if (D > 0.f && (NowSeconds - H.StartTime) >= D)
		{
			Removed.Add(It.Key());
			It.RemoveCurrent();
		}
	}
	return Removed;
}
