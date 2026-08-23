# BlueprintAutoLayout

BlueprintAutoLayout 是一个 Unreal Editor 节点自动排版插件，可用于 Blueprint、Material 和 Animation Blueprint 图。

## 支持范围

- Unreal Engine 4.26 - 5.8
- Win64 / Mac / Linux
- 仅用于编辑器，不参与游戏运行时打包

## 安装

1. 将插件目录放到项目的 `Plugins/BlueprintAutoLayout` 下，并确认文件路径为：

   ```text
   <Project>/Plugins/BlueprintAutoLayout/BlueprintAutoLayout.uplugin
   ```

2. 使用对应 Unreal Engine 版本重新生成项目文件并编译项目。
3. 打开编辑器，在 **Edit > Plugins** 中启用 **Blueprint Auto Layout**。
4. 按提示重启编辑器。

源码版本需要安装对应 Unreal Engine 版本要求的 C++ 编译工具链。`BlueprintLisp`、`AnimBP2FP` 和 `MatBP2FP` 均为可选插件，不安装也可以使用手动排版功能。

## 编辑器中使用

打开 Blueprint、Material 或 Animation Blueprint，并切换到需要整理的图。

### 整理整个图

- 点击工具栏中的 **Auto Layout**
- 或在图中右键，选择 **Auto Layout > Auto Layout Graph**
- 或使用快捷键 `Ctrl+Shift+L`

### 仅整理选中的节点

1. 选中需要整理的节点。
2. 点击工具栏中的 **Layout Selection**，或在右键菜单中选择 **Auto Layout Selection**。
3. 也可以使用快捷键 `Ctrl+Shift+Alt+L`。

未选中节点会保持原位。若当前编辑器没有可读取的选区，**Layout Selection** 会整理整个图。排版操作支持编辑器的撤销功能。

## 在 C++ 中调用

在调用模块的 `.Build.cs` 中添加依赖：

```csharp
PrivateDependencyModuleNames.Add("BlueprintAutoLayout");
```

然后包含公开头文件并调用排版接口：

```cpp
#include "BlueprintAutoLayoutEngine.h"

// 整理整个图
FBlueprintAutoLayoutEngine::Layout(Graph);

// 仅整理指定节点
FBlueprintAutoLayoutEngine::LayoutSelection(Graph, SelectedNodes);

// 使用自定义间距和方向
FBALSettings Settings;
Settings.GapX = 48.f;
Settings.GapY = 64.f;
Settings.bForceDir = true;
Settings.ForcedDir = EBALPureDir::West;
FBlueprintAutoLayoutEngine::Layout(Graph, Settings);
```

需要锁定节点或限制节点移动范围时，可以构造 `TArray<FBALConstraint>`，并调用：

```cpp
FBlueprintAutoLayoutEngine::LayoutWithConstraints(Graph, Constraints, Settings);
```

## 与 DSL 插件配合

启用 `BlueprintLisp`、`AnimBP2FP` 或 `MatBP2FP` 后，在导入请求中加入 `AutoLayout` 行为，插件会在导入完成后自动整理新增或修改的节点。手动排版不依赖这些插件。

## 常见问题

- **工具栏没有按钮**：确认插件已经启用并重启编辑器，然后重新打开资产编辑器。
- **快捷键没有反应**：先点击图的空白区域，使当前图获得焦点。
- **插件无法编译**：确认正在使用受支持的 Unreal Engine 版本，并已安装该版本要求的 C++ 工具链。
