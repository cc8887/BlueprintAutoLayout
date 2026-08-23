// BALTypes.h — Core data structures for Blueprint Auto Layout
// Copyright (c) 2026 ccc887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"

class UEdGraphPin;

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
	/** Soft/RigidGroup anchor supplied by the caller; Hard always uses the node's current position. */
	FVector2D            OriginalPos = FVector2D::ZeroVector;
	/** Optional group ID for RigidGroup (all nodes with same ID move together). */
	int32                GroupId     = INDEX_NONE;
	/** True for groups inferred from comment containment rather than supplied by a caller. */
	bool                 bCommentGroup = false;
};

// -----------------------------------------------------------------------------
// Graph topology
// -----------------------------------------------------------------------------

enum class EBALEdgeKind : uint8
{
	Exec,
	Data,
};

/** A pin-level logical edge. Knot chains are collapsed for layout purposes. */
struct FBALEdge
{
	UEdGraphNode* Source = nullptr;
	UEdGraphNode* Target = nullptr;
	UEdGraphPin* SourcePin = nullptr;
	UEdGraphPin* TargetPin = nullptr;
	EBALEdgeKind Kind = EBALEdgeKind::Data;
	int32 SourcePinIndex = 0;
	int32 TargetPinIndex = 0;
	bool bBackEdge = false;
	bool bPrimary = false;
};

/** Geometry-derived comment hierarchy used for bottom-up comment resizing. */
struct FBALCommentGroup
{
	UEdGraphNode* Comment = nullptr;
	UEdGraphNode* ParentComment = nullptr;
	TArray<UEdGraphNode*> DirectMembers;
	int32 Depth = 0;
};

/** One undirected connected component, excluding comment boxes. */
struct FBALComponent
{
	int32 Id = INDEX_NONE;
	TArray<UEdGraphNode*> Nodes;
	UEdGraphNode* Anchor = nullptr;
	FVector2D OriginalAnchor = FVector2D::ZeroVector;
	bool bHasHardAnchor = false;
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
	/** Original size. Comment nodes may receive a different target Size. */
	FVector2D     OriginalSize = FVector2D(200.f, 80.f);

	/** Hard constraints are immovable. Rigid groups are movable unless requested otherwise. */
	bool          bLocked    = false;
	bool          bConstrained = false;
	EBALConstraintType ConstraintType = EBALConstraintType::Hard;
	float         MaxDrift = 0.f;
	int32         GroupId = INDEX_NONE;
	bool          bCommentGroup = false;

	/** Deterministic graph order and layered-layout metadata. */
	int32         StableIndex = INDEX_NONE;
	int32         ComponentId = INDEX_NONE;
	int32         Layer = 0;
	int32         LayerOrder = 0;

	/** Smallest containing comment, if any. */
	UEdGraphNode* DirectComment = nullptr;
	int32         CommentDepth = 0;

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
	/** Vertical gap between parameter nodes in the same layer. */
	float PureGapY       = 8.f;
	/** Per-node collision margin (added to AABB on all sides). */
	float NodeMargin     = 8.f;
	/** Grid snap size for final coordinates (0 = no snap). */
	float GridSnap       = 8.f;
	/** Nodes whose position changed less than this are NOT written (VCS safety). */
	float MoveThreshold  = 2.f;
	/** Maximum collision-resolution iterations. */
	int32 CollisionIter  = 30;
	/** Barycentric forward/backward sweeps used to reduce wire crossings. */
	int32 CrossingReductionPasses = 6;
	/** Median-alignment passes used to straighten connected rows. */
	int32 AlignmentPasses = 4;
	/** Spacing between disconnected graph components. */
	float ComponentGapX = 256.f;
	float ComponentGapY = 128.f;
	/** Padding used when comments are resized around their contents. */
	float CommentPaddingX = 40.f;
	float CommentPaddingY = 32.f;
	float CommentHeaderHeight = 32.f;
	/** Keep one stable root/locked node fixed when formatting each component. */
	bool bPreserveAnchors = true;
	/** Resize comment boxes bottom-up after their contents have moved. */
	bool bAutoSizeComments = true;
	/** Preserve comment contents as an old-style rigid group instead of formatting them. */
	bool bPreserveCommentContents = false;
	/** Align connected execution pins when branch spacing allows it. */
	bool bAlignExecPins = true;
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
