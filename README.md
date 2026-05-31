# BlueprintAutoLayout

BlueprintAutoLayout 是一个 Unreal Editor 插件，对任意 Blueprint 图（Logic / Material / Animation）的节点做**自动排版**：约束感知、避免重叠、紧凑布局。它既可手动触发，也能在其他 DSL 插件导入节点后自动整理图形。

## 功能

- **通用图排版**：支持任意 `UEdGraph` 子类（EventGraph、MaterialGraph、AnimGraph 等）
- **三种入口**：
  - `Layout`：整图排版
  - `LayoutSelection`：仅排版选中节点，其余节点视为硬约束（Hard）保持原位
  - `LayoutWithConstraints`：外部传入约束，便于脚本化调用
- **约束模型**：`Hard`（不可动）/ `Soft`（可在 MaxDrift 内漂移）/ `RigidGroup`（成组整体平移，如 Comment Box）
- **VCS 友好**：位移小于 `MoveThreshold` 的节点不写回，减少无意义 diff
- **编辑器集成**：工具栏按钮 + 图右键菜单；快捷键 `Ctrl+Shift+L`（整图）/ `Ctrl+Shift+Alt+L`（选中）

## 排版管线

`FBlueprintAutoLayoutEngine` 是统一门面，按六个阶段编排：

```
ConstraintCollector → GraphAnalyzer → StyleVoter → LayoutSolver → CollisionResolver → Committer
```

- **ConstraintCollector**：收集锁定节点 / 成组约束
- **GraphAnalyzer**：识别节点角色（Exec / Pure / Isolated / Comment / Knot）、构建 exec 树与 pure 分组
- **StyleVoter**：决定 Pure 节点相对消费者的摆放方向（West/East/North/South）
- **LayoutSolver**：计算目标坐标
- **CollisionResolver**：迭代消除重叠
- **Committer**：按阈值过滤后写回节点位置

## 支持范围

- 模块类型：Editor-only（`PostEngineInit` 加载）
- 平台：Win64 / Mac / Linux
- 引擎版本：UE 5.6+

## 与其他插件的关系

BlueprintAutoLayout 位于 FP（Functional Programming）DSL 工具链的**末端**，是它们共享的排版服务。它依赖 `BlueprintLisp`、`AnimBP2FP`、`MatBP2FP` 三个模块（见 `BlueprintAutoLayout.Build.cs` 的 `PublicDependencyModuleNames`）。

```
BlueprintLisp ─┐
AnimBP2FP     ─┼──(import lifecycle hook)──▶ BlueprintAutoLayout ──▶ 自动排版图节点
MatBP2FP      ─┘
```

### 工作机制：导入生命周期钩子

模块启动时，`RegisterImportHooks()` 会向每个**可用**的 DSL 插件注册一个 `IImportLifecycleHook`：

- `FBlueprintLispAutoLayoutHook` → 注册到 `FBlueprintLispModule`
- `FAnimBP2FPAutoLayoutHook` → 注册到 `FAnimBP2FPModule`
- `FMatBP2FPAutoLayoutHook` → 注册到 `FMatBP2FPModule`

当某个 DSL 插件完成节点导入（`EImportLifecyclePhase::PostNodeChanges` 阶段）时，钩子被回调：

1. 检查导入上下文是否请求了 `AutoLayout` 行为
2. 若有变更节点 → 调用 `LayoutSelection` 仅排版这些新节点
3. 若无具体变更 → 调用 `Layout` 整图排版

这样，AI 通过 DSL 写回蓝图后，图节点会被自动整理成可读布局，无需手动点击。

### 解耦设计

- 钩子注册前用 `FXxxModule::IsAvailable()` 做存在性检查，**任一 DSL 插件缺失都不会导致崩溃**——只是不挂接对应钩子。
- 手动入口（菜单 / 快捷键 / `FBlueprintAutoLayoutEngine` API）不依赖任何 DSL 插件，可独立使用。

## 调用方式

### 编辑器内手动

- 打开任意 Blueprint / Material / Animation Blueprint 编辑器
- 工具栏点击 **Auto Layout** / **Layout Selection**，或使用快捷键

### C++ 脚本化

```cpp
#include "BlueprintAutoLayoutEngine.h"

// 整图排版
FBlueprintAutoLayoutEngine::Layout(Graph);

// 仅排版选中节点，其余作为硬约束
FBlueprintAutoLayoutEngine::LayoutSelection(Graph, SelectedNodes);

// 自定义参数
FBALSettings Settings;
Settings.GapX = 48.f;
Settings.bForceDir = true;
Settings.ForcedDir = EBALPureDir::West;
FBlueprintAutoLayoutEngine::Layout(Graph, Settings);
```

## 测试

测试模块 `BlueprintAutoLayoutTests`（Editor-only）覆盖各管线阶段：

- `BALConstraintCollectorTests` / `BALGraphAnalyzerTests` / `BALStyleVoterTests`
- `BALLayoutSolverTests` / `BALCollisionResolverTests` / `BALCommitterTests`
- `BALPipelineTests`（端到端）

## 注意

- 本插件为 Editor-only，不参与运行时打包
- 与 DSL 插件配合时，请确保对应插件已启用（见 `.uplugin` 的 `Plugins` 字段）
- 大图排版前建议先在测试蓝图验证参数（`FBALSettings`）

---

**版本**: 1.0.0
**UE 版本**: 5.6+
