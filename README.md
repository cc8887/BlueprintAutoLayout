# BlueprintAutoLayout

BlueprintAutoLayout 是一个 Unreal Editor 插件，对任意 Blueprint 图（Logic / Material / Animation）的节点做**自动排版**：拓扑感知、约束感知、避免重叠、稳定写回。它既可手动触发，也能在其他 DSL 插件导入节点后自动整理图形。

## 功能

- **通用图排版**：支持任意 `UEdGraph` 子类（EventGraph、MaterialGraph、AnimGraph 等）
- **三种入口**：
  - `Layout`：整图排版
  - `LayoutSelection`：仅排版选中节点，其余节点视为硬约束（Hard）保持原位
  - `LayoutWithConstraints`：外部传入约束，便于脚本化调用
- **约束模型**：`Hard`（不可动）/ `Soft`（可在 MaxDrift 内漂移）/ `RigidGroup`（成组整体平移，如 Comment Box）
- **复杂拓扑**：支持分叉、汇合、执行环、data-only 图和共享 Pure 子图；reroute/knot 链在分析时折叠为 logical edge
- **分层布局**：拓扑层级、稳定回边识别、barycentric crossing reduction、连接 pin 的 median alignment
- **Pure 分组**：按 data-hop 和消费者执行深度分配 owner，可向 West/East/North/South 分层摆放
- **组件与注释**：断开组件独立锚定/打包；嵌套 Comment 自内向外重算边界并向外网格对齐
- **大图稳健性**：图遍历使用显式栈，crossing pass 使用预建邻接索引；同一输入使用稳定 tie-break
- **VCS 友好**：位移小于 `MoveThreshold` 的节点不写回，减少无意义 diff
- **编辑器集成**：工具栏按钮 + 图右键菜单；快捷键 `Ctrl+Shift+L`（整图）/ `Ctrl+Shift+Alt+L`（选中）

## 排版管线

`FBlueprintAutoLayoutEngine` 是统一门面，按六个阶段编排：

```
ConstraintCollector → GraphAnalyzer → StyleVoter → LayoutSolver → CollisionResolver → Committer
```

- **ConstraintCollector**：收集锁定节点 / 成组约束
- **GraphAnalyzer**：构建 pin-level logical graph，识别节点角色、执行回边/层级、Pure owner、组件和 Comment 层级
- **StyleVoter**：决定 Pure 节点相对消费者的摆放方向（West/East/North/South）
- **LayoutSolver**：分层排布执行图与 data-only 图，减少交叉并对齐连接 pin，随后摆放 Pure/Knot 和断开组件
- **CollisionResolver**：用 margin-aware spatial hash 迭代消除重叠，并在安全时向理想位置压紧
- **Committer**：执行最终网格对齐、碰撞复查、Comment 边界更新，再按阈值事务化写回

## 支持范围

- 模块类型：Editor-only（`PostEngineInit` 加载）
- 平台：Win64 / Mac / Linux
- 引擎版本：UE 4.26 - 5.8

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

- 三个 DSL 插件均为**可选依赖**（`.uplugin` 标 `Optional: true`）。`Build.cs` 在编译期探测各插件是否存在，存在则加入依赖并定义 `WITH_<PLUGIN>=1`，缺失则定义 `=0`；源码用 `#if WITH_*` 把对应集成编译出。
- 因此**任一或全部 DSL 插件缺失 / 被禁用，BlueprintAutoLayout 仍能编译、加载、运行**——只是不挂接缺失插件的钩子。
- 运行期还有 `IsAvailable()` 兜底，处理「插件存在但晚于本插件加载」的场景。
- 手动入口（菜单 / 快捷键 / `FBlueprintAutoLayoutEngine` API）不依赖任何 DSL 插件，可独立使用。

> 接口细节与鲁棒性分析见 [INTERFACES.md](INTERFACES.md)。

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

测试模块 `BlueprintAutoLayoutTests`（Editor-only）覆盖各管线阶段和复杂图形回归：

- `BALConstraintCollectorTests` / `BALGraphAnalyzerTests` / `BALStyleVoterTests`
- `BALLayoutSolverTests` / `BALCollisionResolverTests` / `BALCommitterTests`
- `BALPipelineTests`（端到端）
- `BALAdvancedTopologyTests` / `BALAdvancedRegressionTests`
- `BALDeepRegressionTests`（环、确定性、大 margin、4096 节点长链）

运行当前插件测试：

```powershell
UnrealEditor-Cmd.exe HostProject.uproject -unattended -nop4 -NullRHI `
  "-ExecCmds=Automation RunTests BlueprintAutoLayout; Quit"
```

多版本打包与测试由配套 CI 流水线执行，可在 CI 配置中启用：

```json
"AutomationTests": {
  "Enabled": true,
  "Filter": "BlueprintAutoLayout",
  "TimeoutMinutes": 30
}
```

## 注意

- 本插件为 Editor-only，不参与运行时打包
- 与 DSL 插件配合时，请确保对应插件已启用（见 `.uplugin` 的 `Plugins` 字段）
- 大图排版前建议先在测试蓝图验证参数（`FBALSettings`）

---

**版本**: 1.0.0
**UE 版本**: 4.26 - 5.8
