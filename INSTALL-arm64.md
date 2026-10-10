# NppMarkdownPanel — Windows on ARM64 原生版

Notepad++ 的 Markdown 预览插件，**原生 ARM64 构建**，无需安装任何额外运行时。

- 产物：`NppMarkdownPanel-0.9.3.0-arm64.zip`（2.51 MB）
- 前置依赖：**.NET Framework 4.8.1**（Windows 11 已自带，无需下载任何东西）
- 已实测：本机 Notepad++ 8.9.8 ARM64 上加载成功，面板正常打开并渲染

## 安装

1. 确认 Notepad++ 是 **ARM64** 版（架构不符会被拒绝加载）
2. 解压 zip，得到 `NppMarkdownPanel` 文件夹
3. 整个文件夹放到 Notepad++ 的 `plugins\` 下：

```
<Notepad++ 目录>\plugins\NppMarkdownPanel\
    NppMarkdownPanel.dll            ← 原生 ARM64 shim（N++ 实际加载的）
    NppMarkdownPanel.Managed.dll    ← 托管插件（由 shim 加载）
    style.css / style-dark.css
    WebView2Loader.dll
    lib\                            ← 依赖程序集
    help\
    README.md / License.txt
```

4. 重启 Notepad++，菜单 **插件 → MarkdownPanel → Toggle Markdown Panel** 打开预览

> 目录名必须与 DLL 同名：Notepad++ 按 `plugins\<目录名>\<目录名>.dll` 查找。

### 若报 "not compatible with the current version of Notepad++"

Windows 会阻止下载来的 DLL。请**解除所有 DLL 的锁定**（右键 → 属性 → 勾选"解除锁定"，含子目录）。
本机实测未出现此提示（文件未带 `Zone.Identifier`），但手动从浏览器下载的包通常需要这一步。

## 已验证的证据

### 1. 插件加载成功

```
Bootstrap OK (export table published)
setInfo OK: controller built, npp=0x54074A
getFuncsArray OK: 11 menu item(s)
```

无任何错误对话框；`NppMarkdownPanel.dll`、`clr.dll`、`clrjit.dll` 均在 Notepad++ 进程内。
插件菜单中出现 `MarkdownPanel`，11 个命令全部注册（`Toggle Markdown Panel` id=22028 等）。

### 2. 面板真正打开并渲染

触发 `Toggle Markdown Panel` 后，进程内出现：

```
CHILD [WindowsForms10.Window...] vis=True :: MarkdownPreviewForm
CHILD [Chrome_WidgetWin_1]       vis=True :: sample.md
CHILD [Chrome_RenderWidgetHostHWND] vis=True :: Chrome Legacy Window
```

即预览窗体已创建、WebView2 已加载文档、渲染进程就绪。截图见 `arm64-panel-proof.png`。

### 3. 运行环境

```
process-arch=Arm64     os-arch=Arm64     is-64bit-process=True
framework=.NET Framework 4.8.9345.0
probe=System.Windows.Forms.Form -> OK
probe=Microsoft.Web.WebView2.Core.dll -> OK
probe=Markdig.dll -> OK
probe=HtmlSanitizer.dll -> OK
failures=0
```

## 为什么需要单独一个 ARM64 构建

Notepad++ 以**原生 DLL** 方式加载插件，插件必须导出 6 个 C 入口点
（`isUnicode` / `setInfo` / `getName` / `getFuncsArray` / `beNotified` / `messageProc`）。

原项目的 x86/x64 构建靠 `UnmanagedExports.Repack.Upgrade`（RGiesecke DllExport）在编译期合成这些导出，
但该工具链**只生成 I386 / AMD64 / IA64 存根，完全没有 ARM64 支持**
（上游 [3F/DllExport#242](https://github.com/3F/DllExport/issues/242) 至今未解决）。

因此 ARM64 版本改用一个小型**原生 shim**：它导出这 6 个入口点、托管 CLR，再把调用转发给托管插件。

## 为什么不需要 .NET 8

社区里另一个可行方案是 .NET 8 + DNNE，但那要求用户额外下载约 55 MB 桌面运行时。
本方案走 **.NET Framework 4.8.1** 路线：4.8.1 自 Windows 11 起原生支持 ARM64 且系统自带
（实测 `FrameworkArm64\v4.0.30319\clr.dll` 的 PE 机器码为 `0xAA64`）。

**结论：用户零安装，插件体积 2.51 MB。**

> 注意：必须 **4.8.1** 及以上。arm64 的 `mscoree.lib` 只有 NETFXSDK **4.8.1** 提供，4.7.2 与 4.8 都没有。

## 顺带修复的一个上游缺陷

调试过程中发现原代码存在一个**时序竞态**（x86/x64 版同样存在，只是更少触发）：

`Main.cs` 原本用静态字段初始化器构造控制器：

```csharp
private static MarkdownPanelController mdpanel = new MarkdownPanelController();
```

`Main` 没有显式静态构造函数，编译器因此标记该类为 `beforefieldinit`（已用反射验证：`BeforeFieldInit=True`）。
这种类型的静态初始化器**可能在首次静态字段访问之前的任意时刻执行**，包括 JIT 编译仅引用该类型的方法时。
而控制器构造函数要读 `PluginBase.nppData` 来找配置目录，于是：

```
Directory.CreateDirectory("") -> ArgumentException
  -> Main 的类型初始化器失败（CLR 会缓存该异常并反复重抛）
  -> 异常穿透原生帧，被 Notepad++ 的 catch(...) 捕获
  -> 报 "Failed to load ... is not compatible"
```

修复：改用 `Lazy<T>` 延迟到首次使用（必然在 `setInfo` 之后）构造。
该改动对 x86/x64 同样有益，且已验证 x86/x64 构建仍正常通过。

## 构建

CI：`.github/workflows/arm64-build.yml`（runner: `windows-11-arm`）。本地复现：

```powershell
./build-arm64.ps1 -Configuration Release -Version 0.9.3.0
```

流程：还原包 → 编译托管插件（AnyCPU IL）→ 编译原生 ARM64 shim → 组装插件目录 →
打包 → 校验 PE 架构 → 跑冒烟测试。**冒烟测试失败会直接让构建失败**，避免发出未校验的包。

## 实现要点

| 文件 | 作用 |
|---|---|
| `arm64/NppShim.cpp` | 原生 ARM64 shim：导出 6 个入口点、托管 CLR、转发调用 |
| `arm64/NppSmokeTest.cpp` | 冒烟测试：按 Notepad++ 的方式加载 shim 并跨越托管边界 |
| `NppMarkdownPanel/PluginInfrastructure/Arm64Exports.cs` | 托管侧导出表（函数指针交接） |
| `NppMarkdownPanel/PluginInfrastructure/PluginAssemblyResolver.cs` | 解析 `lib\*.dll` |
| `NppMarkdownPanel/PluginInfrastructure/PluginDiagnostics.cs` | 启动诊断日志（`arm64-startup.log`） |
| `NppMarkdownPanel/Main.cs` | 控制器改为 `Lazy<T>`（根因修复，两平台共用） |
| `build-arm64.ps1` | 构建 + 打包 + 校验 + 冒烟测试 |

关键设计决定：

1. **用全路径加载托管程序集**（`ExecuteInDefaultAppDomain`）。CLR 默认按宿主 EXE（Notepad++ 根目录）
   探测程序集，而 Plugin Admin 只往 `plugins\<名>\` 装东西。传全路径正好绕开——这也是 DNNE 的
   netfx 路径不适用本插件的原因。

2. **显式注册程序集解析器**。x86/x64 版在 `MarkdownPanelController` 构造函数里注册 `AssemblyResolve`，
   但那对 ARM64 太晚：JIT 可能在准备入口点时就已需要 `MarkdigWrapper` 等程序集。
   改在最早的托管入口注册，与 JIT 顺序无关。

## 排错

插件启动日志写在插件目录（不可写时回退到 `%TEMP%\NppMarkdownPanel-arm64.log`）：

```
plugins\NppMarkdownPanel\arm64-startup.log
```

正常启动应包含 `Bootstrap OK` / `setInfo OK` / `getFuncsArray OK` 三行。

## 已知限制

- **仅 Windows 11 ARM64**：依赖 .NET Framework 4.8.1 的原生 ARM64 支持
  （Windows 10 ARM64 只有 x86 模拟，没有原生 4.8.1）
- **必须配 ARM64 版 Notepad++**
- 本分支只新增 ARM64 构建路径，**x86/x64 发布流程未改动**（`Main.cs` 的 `Lazy<T>` 修复两平台共用）
