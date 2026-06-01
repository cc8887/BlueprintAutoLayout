// DrivenId.cpp
// Copyright (c) 2026 ccc887. All Rights Reserved.

#include "DrivenId.h"
#include "DrivenHighlightLog.h"

#include "EdGraph/EdGraphNode.h"
#include "UObject/Object.h"
#include "Misc/Crc.h"

namespace
{
	const TCHAR* PrefixDriven    = TEXT("Driven.");
	const TCHAR* PrefixNode      = TEXT("Driven.Node.");
	const TCHAR* PrefixProp      = TEXT("Driven.Prop.");
	const TCHAR* PrefixPropAsset = TEXT("Driven.Prop.Asset.");

	/** Stable 32-hex-char digest of an asset's FullName. We use 4× CRC32 over
	 *  rotated copies of the input to get a 128-bit-ish digest while keeping
	 *  Engine-only dependencies. Collisions are non-fatal (only used for
	 *  diagnostic / lookup, never for security). */
	FString HashAssetPath(const FString& In)
	{
		const auto Crc = [](const FString& S)
		{
			return FCrc::StrCrc32<TCHAR>(*S);
		};
		const uint32 A = Crc(In);
		const uint32 B = Crc(In + TEXT("|1"));
		const uint32 C = Crc(In + TEXT("|2"));
		const uint32 D = Crc(In + TEXT("|3"));
		return FString::Printf(TEXT("%08x%08x%08x%08x"), A, B, C, D);
	}
}

namespace DrivenId
{
	const TCHAR* ToString(EDrivenChangeType ChangeType)
	{
		switch (ChangeType)
		{
			case EDrivenChangeType::Added:    return TEXT("Added");
			case EDrivenChangeType::Modified: return TEXT("Modified");
			case EDrivenChangeType::Removed:  return TEXT("Removed");
		}
		return TEXT("Modified");
	}

	bool TryParseChangeType(const FString& Token, EDrivenChangeType& Out)
	{
		if (Token.Equals(TEXT("Added"),    ESearchCase::CaseSensitive)) { Out = EDrivenChangeType::Added;    return true; }
		if (Token.Equals(TEXT("Modified"), ESearchCase::CaseSensitive)) { Out = EDrivenChangeType::Modified; return true; }
		if (Token.Equals(TEXT("Removed"),  ESearchCase::CaseSensitive)) { Out = EDrivenChangeType::Removed;  return true; }
		return false;
	}

	// ── Construction ─────────────────────────────────────────────────────────

	FName MakeNode(const FGuid& NodeGuid)
	{
		return FName(*FString::Printf(TEXT("%s%s"),
			PrefixNode,
			*NodeGuid.ToString(EGuidFormats::Digits)));
	}

	FName MakeNode(const FGuid& NodeGuid, EDrivenChangeType ChangeType)
	{
		return FName(*FString::Printf(TEXT("%s%s.%s"),
			PrefixNode,
			*NodeGuid.ToString(EGuidFormats::Digits),
			ToString(ChangeType)));
	}

	FName MakeNode(const UEdGraphNode* Node)
	{
		if (!Node) return NAME_None;
		return MakeNode(Node->NodeGuid);
	}

	FName MakeNode(const UEdGraphNode* Node, EDrivenChangeType ChangeType)
	{
		if (!Node) return NAME_None;
		return MakeNode(Node->NodeGuid, ChangeType);
	}

	FName MakeNodeProp(const FGuid& NodeGuid, FName PropertyDisplayName)
	{
		if (PropertyDisplayName.IsNone()) return NAME_None;
		return FName(*FString::Printf(TEXT("%s%s.%s"),
			PrefixProp,
			*NodeGuid.ToString(EGuidFormats::Digits),
			*PropertyDisplayName.ToString()));
	}

	FName MakeNodeProp(const UEdGraphNode* Node, FName PropertyDisplayName)
	{
		if (!Node) return NAME_None;
		return MakeNodeProp(Node->NodeGuid, PropertyDisplayName);
	}

	FName MakeAssetProp(const UObject* Asset, FName PropertyDisplayName)
	{
		if (!Asset || PropertyDisplayName.IsNone()) return NAME_None;
		return FName(*FString::Printf(TEXT("%s%s.%s"),
			PrefixPropAsset,
			*HashAssetPath(Asset->GetPathName()),
			*PropertyDisplayName.ToString()));
	}

	// ── Decoding ─────────────────────────────────────────────────────────────

	bool Parse(FName DrivenId, FParts& Out)
	{
		Out = FParts();
		if (DrivenId.IsNone()) return false;

		const FString S = DrivenId.ToString();
		if (!S.StartsWith(PrefixDriven, ESearchCase::CaseSensitive)) return false;

		// AssetProp comes BEFORE NodeProp in the prefix table because it is a
		// strict refinement of the "Driven.Prop." prefix.
		if (S.StartsWith(PrefixPropAsset, ESearchCase::CaseSensitive))
		{
			// Driven.Prop.Asset.<hash>.<PropName>
			const int32 PrefixLen = FCString::Strlen(PrefixPropAsset);
			const FString Tail = S.Mid(PrefixLen);

			int32 DotIdx;
			if (!Tail.FindChar('.', DotIdx) || DotIdx <= 0) return false;

			Out.Kind                 = EKind::AssetProp;
			Out.AssetHash            = Tail.Left(DotIdx);
			Out.PropertyDisplayName  = FName(*Tail.Mid(DotIdx + 1));
			return Out.AssetHash.Len() > 0 && !Out.PropertyDisplayName.IsNone();
		}

		if (S.StartsWith(PrefixProp, ESearchCase::CaseSensitive))
		{
			// Driven.Prop.<guid>.<PropName>
			const int32 PrefixLen = FCString::Strlen(PrefixProp);
			const FString Tail = S.Mid(PrefixLen);

			// First 32 chars are GUID digits.
			if (Tail.Len() < 33) return false;            // 32 + '.' + at least 1 char
			const FString GuidStr = Tail.Left(32);
			if (Tail[32] != TEXT('.'))               return false;
			if (!FGuid::ParseExact(GuidStr, EGuidFormats::Digits, Out.NodeGuid)) return false;

			Out.Kind                = EKind::NodeProp;
			Out.PropertyDisplayName = FName(*Tail.Mid(33));
			return !Out.PropertyDisplayName.IsNone();
		}

		if (S.StartsWith(PrefixNode, ESearchCase::CaseSensitive))
		{
			// Driven.Node.<guid>[.<ChangeType>]
			const int32 PrefixLen = FCString::Strlen(PrefixNode);
			const FString Tail = S.Mid(PrefixLen);

			if (Tail.Len() < 32) return false;
			const FString GuidStr = Tail.Left(32);
			if (!FGuid::ParseExact(GuidStr, EGuidFormats::Digits, Out.NodeGuid)) return false;

			Out.Kind = EKind::Node;
			if (Tail.Len() == 32) return true;            // bare form, no change-type
			if (Tail[32] != TEXT('.')) return false;

			const FString ChangeTok = Tail.Mid(33);
			if (!TryParseChangeType(ChangeTok, Out.ChangeType)) return false;
			Out.bHasChangeType = true;
			return true;
		}

		return false;
	}

	FName GetDetailRowTagForPropertyId(FName DrivenId)
	{
		FParts Parts;
		if (!Parse(DrivenId, Parts)) return NAME_None;
		if (Parts.Kind != EKind::NodeProp && Parts.Kind != EKind::AssetProp) return NAME_None;
		return FName(*FString::Printf(TEXT("DetailRowItem.%s"),
			*Parts.PropertyDisplayName.ToString()));
	}
}
