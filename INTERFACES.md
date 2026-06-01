# 共通接口与 BlueprintAutoLayout 鲁棒性说明

本文整理 `BlueprintLisp`、`AnimBP2FP`、`MatBP2FP` 三个 DSL 插件**共通的导入生命周期接口**，并分析 `BlueprintAutoLayout` 在这三个插件缺失 / 部分缺失时的运行情况。

---

## 一、共通接口：导入生命周期 Hook

三个 DSL 插件各自在 `<Plugin>Module.h` 中声明了一套**结构完全相同**的导入生命周期接口，仅命名空间与导出宏不同：

| 概念       | BlueprintLisp                  | AnimBP2FP                  | MatBP2FP                  |
| -------- | ------------------------------ | -------------------------- | ------------------------- |
| 命名空间     | `BlueprintLispImportLifecycle` | `AnimBP2FPImportLifecycle` | `MatBP2FPImportLifecycle` |
| 导出宏      | `BLUEPRINTLISP_API`            | `ANIMBP2FP_API`            | `MATBP2FP_API`            |
| 模块类      | `FBlueprintLispModule`         | `FAnimBP2FPModule`         | `FMatBP2FPModule`         |
| Host 接口  | `IBlueprintLispImportHookHost` | `IAnimBP2FPImportHookHost` | `IMatBP2FPImportHookHost` |
| 模块名（字符串） | `"BlueprintLisp"`              | `"AnimBP2FP"`              | `"MatBP2FP"`              |

### 1.1 生命周期阶段 `EImportLifecyclePhase`

```cpp
enum class EImportLifecyclePhase : uint8
{
    PreNodeChanges,       // 节点变更前
    PostNodeChanges,      // 节点变更后  ← BlueprintAutoLayout 在此阶段触发排版
    PrePropertyChanges,   // 属性变更前
    PostPropertyChanges,  // 属性变更后
    PreFinalize,          // 收尾前
    PostFinalize,         // 收尾后
};
```

### 1.2 数据载荷

```cpp
// 导入上下文（贯穿整个导入会话）
struct FImportLifecycleContext
{
    FGuid       ImportSessionId;
    UObject*    TargetAsset      = nullptr;
    UEdGraph*   TargetGraph      = nullptr;   // 目标图
    FName       ScopeName;
    bool        bIsFullRebuild   = false;
    bool        bIsIncremental   = false;
    bool        bIsHeadless      = false;     // 无头（commandlet）模式
    bool        bWillCompile     = false;
    TSet<FName> RequestedBehaviors;           // 请求的行为，如 "AutoLayout"
};

// 单个节点变更
struct FImportNodeChange
{
    UEdGraphNode*         Node       = nullptr;
    EImportNodeChangeType ChangeType = Added;  // Added / Modified / Removed
};

// 节点阶段事件（OnNodePhase 的入参）
struct FImportNodePhaseEvent
{
    EImportLifecyclePhase     Phase;
    FImportLifecycleContext   Context;
    TArray<FImportNodeChange> Changes;
};
// 另有 FImportPropertyPhaseEvent / FImportFinalizePhaseEvent，结构同理
```

### 1.3 Hook 抽象基类 `IImportLifecycleHook`

```cpp
class IImportLifecycleHook
{
public:
    virtual ~IImportLifecycleHook() = default;

    // 同阶段多个 hook 的执行优先级（数值大者先执行）
    virtual int32 GetPriority(EImportLifecyclePhase Phase) const { return 0; }

    virtual void OnNodePhase(const FImportNodePhaseEvent& Event) {}
    virtual void OnPropertyPhase(const FImportPropertyPhaseEvent& Event) {}
    virtual void OnFinalizePhase(const FImportFinalizePhaseEvent& Event) {}
};
```

### 1.4 注册 / 注销接口（Host）

```cpp
// 每个模块类都实现了对应的 Host 接口
FImportLifecycleHookHandle RegisterImportLifecycleHook(
    TSharedRef<IImportLifecycleHook> Hook);

void UnregisterImportLifecycleHook(FImportLifecycleHookHandle Handle);

// 句柄：可判空，便于安全注销
struct FImportLifecycleHookHandle { FGuid Id; bool IsValid() const; };
```

### 1.5 模块访问的统一约定

三个模块类都提供了一致的静态访问器：

```cpp
static FXxxModule& Get();        // 取模块单例
static bool        IsAvailable(); // == FModuleManager::Get().IsModuleLoaded("Xxx")
```

> 注意差异：`FBlueprintLispModule::Get()` 用 `GetModuleChecked`（不自动加载），
> 而 `FAnimBP2FPModule::Get()` / `FMatBP2FPModule::Get()` 用 `LoadModuleChecked`（会尝试加载）。
> 调用前都应先用 `IsAvailable()` 判断。

---

## 二、BlueprintAutoLayout 如何消费该接口

`FBlueprintAutoLayoutModule::RegisterImportHooks()`（`BlueprintAutoLayoutModule.cpp:225`）为每个**已加载**的 DSL 模块注册一个 hook：

```cpp
if (FBlueprintLispModule::IsAvailable())
    ...BlueprintLispHandle = FBlueprintLispModule::Get().RegisterImportLifecycleHook(...);
if (FAnimBP2FPModule::IsAvailable())
    ...AnimBP2FPHandle = FAnimBP2FPModule::Get().RegisterImportLifecycleHook(...);
if (FMatBP2FPModule::IsAvailable())
    ...MatBP2FPHandle = FMatBP2FPModule::Get().RegisterImportLifecycleHook(...);
```

每个 hook 仅在 `PostNodeChanges` 阶段、且 `Context.RequestedBehaviors` 含 `"AutoLayout"` 时触发：

- 有变更节点 → `FBlueprintAutoLayoutEngine::LayoutSelection`（仅排版新节点）
- 无变更节点 → `FBlueprintAutoLayoutEngine::Layout`（整图排版）

注销时（`UnregisterImportHooks()`）同样先判 `Handle.IsValid()` 与 `IsAvailable()`，再调用 `Unregister`。

---

## 三、鲁棒性分析：三个插件缺失 / 部分缺失会怎样？

结论先行：**运行期逻辑是健壮的，但当前的「构建期 + 插件依赖」配置把三者声明成了硬性必需项，因此在任一插件完全缺失时，BlueprintAutoLayout 根本无法编译 / 加载——运行期的防御代码在这种场景下用不上。**

分三个层级看：

### 层级 1 — 运行期逻辑：✅ 健壮（可优雅降级）

- `RegisterImportHooks` / `UnregisterImportHooks` 对每个模块**独立**做 `IsAvailable()` 判断，互不影响。
- 注销前判 `Handle.IsValid()`，不会重复 / 空注销。
- `HookRegistrationState` 用 `TUniquePtr` 管理，`ShutdownModule` 始终调用 `UnregisterImportHooks`，无泄漏。
- 手动入口（工具栏 / 快捷键 / `FBlueprintAutoLayoutEngine` 三个静态 API）**完全不依赖**任何 DSL 模块。

→ 仅就这一层而言，「某个模块存在但尚未加载」或「只加载了部分模块」都能正常工作：缺的就不挂钩子，有的正常挂。

### 层级 2 — 编译 / 链接期：❌ 硬依赖

`BlueprintAutoLayout.Build.cs` 把三者放进 `PublicDependencyModuleNames`：

```csharp
PublicDependencyModuleNames.AddRange(new[]
{
    "Core", "CoreUObject", "Engine",
    "BlueprintLisp", "AnimBP2FP", "MatBP2FP",   // ← 三者均为硬依赖
});
```

且公共头 `BlueprintAutoLayoutModule.h:8-10` 直接 `#include` 三个模块头：

```cpp
#include "BlueprintLispModule.h"
#include "AnimBP2FPModule.h"
#include "MatBP2FPModule.h"
```

→ 任一插件**源码/头文件不存在**，则编译/链接直接失败，根本产不出二进制。

### 层级 3 — 插件加载期：❌ 必需依赖

`BlueprintAutoLayout.uplugin` 的 `Plugins` 字段把三者标为 `Enabled: true` 且**未标 `Optional`**：

```json
"Plugins": [
    { "Name": "BlueprintLisp", "Enabled": true },
    { "Name": "AnimBP2FP",     "Enabled": true },
    { "Name": "MatBP2FP",      "Enabled": true }
]
```

→ 任一被引用插件在工程中缺失 / 被禁用，UE 会判定依赖不满足，**拒绝加载 BlueprintAutoLayout**（或报依赖缺失）。

### 小结

| 场景                      | 结果                                  |
| ----------------------- | ----------------------------------- |
| 三个插件都在                  | ✅ 正常，全部挂钩                           |
| 三个都在、但某个运行时晚于本插件加载      | ✅ 正常（`IsAvailable` 兜底，仅该项不挂钩）       |
| 任一插件**完全缺失**（源码/插件目录没有） | ✅ 编译期检测缺失 → 编译出该集成，正常加载运行           |
| 任一插件存在但被**禁用**          | ✅ `Optional` 依赖，本插件正常加载（仅运行期不挂对应钩子） |

> 下表为**改造前**的旧行为（仅作历史参考）：任一插件物理缺失会导致编译失败 / UE 拒绝加载，运行期 `IsAvailable` 防御代码触及不到。本插件现已完成下节所述的可选依赖改造。

---

## 四、可选依赖改造（已实施）

为让 BlueprintAutoLayout 在缺少任意 DSL 插件时仍能编译、加载并运行（只是少挂对应钩子），已在三处协同改造：

### 4.1 `.uplugin` — 标记为可选依赖

```json
"Plugins": [
    { "Name": "BlueprintLisp", "Enabled": true, "Optional": true },
    { "Name": "AnimBP2FP",     "Enabled": true, "Optional": true },
    { "Name": "MatBP2FP",      "Enabled": true, "Optional": true }
]
```

`Optional: true` 让 UE 在插件缺失/禁用时**不报依赖错误**，照常加载 BlueprintAutoLayout。

### 4.2 `Build.cs` — 按存在性条件添加依赖 + 定义宏

`BlueprintAutoLayout.Build.cs` 新增 `AddOptionalPluginModule()` / `IsPluginAvailable()`：

- `IsPluginAvailable(name)`：在 Plugins 根目录下递归查找 `<name>.uplugin`，判断插件是否存在（引擎版本无关，不依赖 UBT 内部 API）。
- 存在 → 加入 `PublicDependencyModuleNames` 并定义 `WITH_<PLUGIN>=1`；
- 缺失 → 不加依赖，定义 `WITH_<PLUGIN>=0`。

涉及宏：`WITH_BLUEPRINTLISP` / `WITH_ANIMBP2FP` / `WITH_MATBP2FP`。

### 4.3 源码 — 用宏编译开关包裹集成代码

- **公共头 `BlueprintAutoLayoutModule.h`**：移除对三个 DSL 模块头的 `#include`（避免把可选类型泄漏给所有消费者）；`FHookRegistrationState` 继续以 PImpl 形式仅在 `.cpp` 中定义。
- **`BlueprintAutoLayoutModule.cpp`**：
  - 三个 `#include` 各自用 `#if WITH_*` 包裹；
  - 每个 `CollectChangedGraphNodes` 重载、每个 `FXxxAutoLayoutHook` 类、`FHookRegistrationState` 的成员、`RegisterImportHooks` / `UnregisterImportHooks` 中的对应分支，均按 `#if WITH_*` 编译；
  - 共享的 `AutoLayoutBehaviorName` / `AutoLayoutEarlyPriority` / `RunGraphLayout<>` 用组合宏 `BAL_ANY_DSL_INTEGRATION`（三者任一为真）包裹，三者全缺时不产生未使用告警。

### 验证矩阵（改造后）

| 三个插件状态 | 编译        | 加载  | 运行         |
| ------ | --------- | --- | ---------- |
| 全部存在   | ✅         | ✅   | ✅ 全挂钩      |
| 部分存在   | ✅ 仅编译存在项  | ✅   | ✅ 仅挂存在项的钩子 |
| 全部缺失   | ✅ 集成全部编译出 | ✅   | ✅ 仅手动入口可用  |

手动入口（工具栏 / 快捷键 / `FBlueprintAutoLayoutEngine` API）在任何组合下都可用。
