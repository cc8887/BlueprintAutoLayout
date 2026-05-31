// BALTypes.h — Core data structures for Blueprint Auto Layout
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"

// ─────────────────────────────────────────────────────────────
//  Constraint
// ─────────────────────────────────────────────────────────────

/** How strongly a node's position is protected from modification. */
enum class EBALConstraintType : uint8
{
	/** Position must not change at all. */
	Hard,
	/** Position may drift up to MaxDrift pixels to resolve collisions. */
	Soft,
	/** Node belongs to a rigid group (e.g. Comment Box); relative offsets preserved. */
	RigidGroup,
};

struct FBALConstraint
{
	UEdGraphNode*        Node        = nullptr;
	EBALConstraintType   Type        = EBALConstraintType::Hard;
	/** For Soft constraints: maximum allowed displacement in pixels. */
	float                MaxDrift    = 0.f;
	FVector2D            OriginalPos = FVector2D::ZeroVector;
	/** Optional group ID for RigidGroup (all nodes with same ID move together). */
	int32                GroupId     = INDEX_NONE;
};

// ─────────────────────────────────────────────────────────────
//  Node classification
// ─────────────────────────────────────────────────────────────

enum class EBALNodeRole : uint8
{
	/** Has exec pins — participates in the execution chain. */
	Exec,
	/** Pure function / data node — no exec pins. */
	Pure,
	/** Isolated node with no connections (no exec, no data edges). */
	Isolated,
	/** Comment box node. */
	Comment,
	/** Wire reroute / knot node. */
	Knot,
};

// ─────────────────────────────────────────────────────────────
//  Layout node proxy
// ─────────────────────────────────────────────────────────────

struct FBALNode
{
	UEdGraphNode* GraphNode  = nullptr;
	EBALNodeRole  Role       = EBALNodeRole::Exec;

	/** Estimated size in graph-space pixels. */
	FVector2D     Size       = FVector2D(200.f, 80.f);

	/** Computed target position (pre-commit). */
	FVector2D     OutPos     = FVector2D::ZeroVector;

	/** Original position (for diff filtering). */
	FVector2D     OriginalPos= FVector2D::ZeroVector;

	/** Is this node locked (Hard/RigidGroup constraint)? */
	bool          bLocked    = false;

	// --- ExecTree fields ---
	/** Topological depth in the exec tree (0 = entry node). */
	int32         ExecDepth  = 0;
	/** Index among siblings at the same branch level. */
	int32         BranchIndex= 0;
	/** Allocated Y-band height for this subtree (Exec nodes only). */
	float         SubtreeH   = 0.f;

	// --- Pure group fields ---
	/** Owning Exec node that consumes this Pure node's output. */
	UEdGraphNode* PureOwner  = nullptr;
	/** Topological depth within the Pure subtree (0 = direct input to Exec). */
	int32         PureDepth  = 0;
	/** Stacking slot index within the same Pure depth layer. */
	int32         PureSlot   = 0;
};

// ─────────────────────────────────────────────────────────────
//  Global layout settings
// ─────────────────────────────────────────────────────────────

/** Direction in which Pure node groups are placed relative to their consumer. */
enum class EBALPureDir : uint8
{
	West,   // left  (default — reads left-to-right)
	East,   // right
	North,  // above
	South,  // below
};

struct FBALSettings
{
	/** Horizontal gap between consecutive Exec nodes. */
	float GapX           = 32.f;
	/** Vertical gap between branches at a fork. */
	float GapY           = 48.f;
	/** Additional horizontal gap between Pure depth layers. */
	float PureGapX       = 8.f;
	/** Per-node collision margin (added to AABB on all sides). */
	float NodeMargin     = 8.f;
	/** Grid snap size for final coordinates (0 = no snap). */
	float GridSnap       = 8.f;
	/** Nodes whose position changed less than this are NOT written (VCS safety). */
	float MoveThreshold  = 2.f;
	/** Maximum collision-resolution iterations. */
	int32 CollisionIter  = 30;
	/** Skip voting and force a specific Pure side. */
	bool  bForceDir      = false;
	EBALPureDir ForcedDir= EBALPureDir::West;
};

// ─────────────────────────────────────────────────────────────
//  Exec tree node (internal layout tree)
// ─────────────────────────────────────────────────────────────

struct FBALExecNode
{
	FBALNode*              Proxy    = nullptr;
	FBALExecNode*          Parent   = nullptr;
	TArray<FBALExecNode*>  Children;

	/** Subtree height computed bottom-up. */
	float SubtreeH = 0.f;

	/** Pure proxies that are consumed directly by this exec node. */
	TArray<FBALNode*>  PureGroup;
};
