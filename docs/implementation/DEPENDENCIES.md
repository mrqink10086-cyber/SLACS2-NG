# SLACS2 NG 依赖与工具记录

更新：2026-11-05。本文记录本次实际核对过的构建工具、运行时、外部插件和头文件。路径为本机路径，校验值为 SHA-256；版本来自文件版本资源或随附说明。未实际用于构建或运行的项目会标注“仅核对”。

## 1. 构建环境

| 项目 | 路径 / 版本 | 校验值 | 使用状态 |
| --- | --- | --- | --- |
| XMake | `C:/Program Files/xmake/xmake.exe`，3.0.9 | — | 已用于配置与构建 |
| MSVC | Visual Studio 2022，x64 | — | 已用于编译 C++23 DLL |
| Windows | Windows 10/11 x64 | — | 构建与自动检查 |
| CommonLibSSE-NG | `E:/source/repos/SLACS2 NG/lib/commonlibsse-ng`，commit `15ad90b31b2ca09cf477c7e1a43ff5ff05af120f`（6.0.0） | 子模块固定提交 | 已用于编译；生成元数据被覆盖为精确 1170 |

配置命令：

```powershell
xmake f -p windows -a x64 --toolchain=msvc -y
xmake build -y
```

默认配置会使用 MinGW 并因缺少 vcpkg 失败，因此必须显式指定 MSVC。

## 2. 游戏与 SKSE

| 项目 | 路径 | 版本 | SHA-256 | 使用状态 |
| --- | --- | --- | --- | --- |
| Skyrim SE | `E:/st/steamapps/common/Skyrim Special Edition` | 1.6.1170.0 | `c434208894f07f604b852f29b8edc3a58c4de63de783373733e72b2b73f33be9`（SkyrimSE.exe） | 构建目标；已做一次加载冒烟 |
| SKSE loader | `E:/st/steamapps/common/Skyrim Special Edition/skse64_loader.exe` | 0.2.2.6 | `730c2743f6871fbaeb8606c1d3b7a55feca045c3d74858a41b0c6d03cd989fbc` | 游戏启动入口 |
| SKSE runtime DLL | `E:/st/steamapps/common/Skyrim Special Edition/skse64_1_6_1170.dll` | 0.2.2.6 | `c9a2c8a80df6bf2372c5f49468bb2e5ab67786157265b6f29ece9f4eac075d54` | 1170 运行时 |
| SKSE 源码基线 | `E:/source/repos/skse64` | commit `9398d04592a7eb9d754f2997701116df1022f1b4`；`skse_version.h` 为 2.2.6/1170 | — | 核对消息顺序、Task/Serialization API |

插件生成元数据使用显式版本列表，不依赖 Address Library：

```cpp
SKSE::PluginDeclaration::RuntimeCompatibility{
    SKSE::PluginDeclaration::VersionNumber{ 1, 6, 1170, 0 }
}
```

`src/main.cpp` 在 `SKSE::Init` 之前再次比较 `a_skse->RuntimeVersion()`，错误运行时直接返回 `false`，不进入 CommonLib 重定位访问。

## 3. Papyrus 编译

| 项目 | 路径 | 版本 | SHA-256 | 使用状态 |
| --- | --- | --- | --- | --- |
| PapyrusCompiler | `E:/st/steamapps/common/Skyrim Special Edition/Papyrus Compiler/PapyrusCompiler.exe` | 1.0.0.0 | `f441afa877ce0b315c6b613db7fe700a7bbc66b71a72fc4d0d8bc94577ceb0ce` | 构建时编译 `src/papyrus/SLACS_Main.psc` |
| Flags | `E:/st/steamapps/common/Skyrim Special Edition/Data/Source/Scripts/TESV_Papyrus_Flags.flg` | 随游戏 | — | 编译参数 |
| 源导入目录 | `Data/Source/Scripts`、`Data/Scripts/Source`，以及 `src/papyrus` | 随游戏 | — | `src/papyrus` 必须放在导入路径最前，防止同名旧脚本遮蔽 |

编译命令由 `tools/package.py` 生成，等价于：

```powershell
PapyrusCompiler.exe src/papyrus/SLACS_Main.psc `
  -f="E:/st/steamapps/common/Skyrim Special Edition/Data/Source/Scripts/TESV_Papyrus_Flags.flg" `
  -i="E:/source/repos/SLACS2 NG/src/papyrus;E:/st/steamapps/common/Skyrim Special Edition/Data/Source/Scripts;E:/st/steamapps/common/Skyrim Special Edition/Data/Scripts/Source" `
  -o="E:/source/repos/SLACS2 NG/package/Data/Scripts"
```

实际结果：`0 error(s), 0 warning(s)`，PEX 1,198 字节；`tools/package.py` 同时检查 PEX magic、必需字符串和旧业务字符串。

## 4. P+（SexLab P+）

| 项目 | 路径 | 版本 | SHA-256 | 使用状态 |
| --- | --- | --- | --- | --- |
| P+ 2.18.1 | `E:/source/repos/sexlab P+/SKSE/Plugins/SexLabUtil.dll` | 2.18.1.0 | `8a3252fbb405d8d441dcd5f241607b3ff0d68d801f00715d9ac6d91945ee224b` | 事件契约与兼容性参考基线；运行时只要求 DLL 已加载，不锁定精确版本 |
| P+ 2.18.1 ESM | `E:/source/repos/sexlab P+/SexLab.esm` | 2.18.1，381,243 字节 | `6c93590ca1f308af26cffba216dc4d1186d9d037eb8a9dda581f8d5fe973fad4` | 参考基线；运行时要求加载，但不强制与 2.18.1.0 成对 |
| 物理游戏内 P+ DLL | `E:/st/steamapps/common/Skyrim Special Edition/Data/SKSE/Plugins/SexLabUtil.dll` | 2.16.0.0 | `1ad6f9226e5998ba9ace789bf256363a8ba91ef5f4a33baeccf08290d9c1bc9d` | 仅冒烟环境；与 2.18.1 不同，不能作为游戏验收依据 |
| 物理游戏内 P+ ESM | `E:/st/steamapps/common/Skyrim Special Edition/Data/SexLab.esm` | 旧版，347,818 字节 | `30a97dda69b854b8d1e375a1c2a1f7bb462ceaf695c08f95eb51580a6ea3c1d8` | 与上面 2.16 DLL 配套；运行时可加载，但事件兼容性未验证，不能作为 2.18.1 验收依据 |

`E:/source/repos/sexlab P+` 只提供 2.18.1 发布文件和 `Source/Scripts/*.psc`，没有 C++ 源码；本项目按 2.18.1 的 PSC 事件签名核对，不锁定运行时 DLL 精确版本，也不链接其内部实现。

S2 只依赖以下 typed ModEvent 名称和参数，不直接链接 `SexLabUtil.dll`：

```text
SexlabOrgasmSeparate(Form source, int tid)
HookAnimationStart(int tid, bool hasPlayer)
HookAnimationEnd(int tid, bool hasPlayer)
```

`tid` 是 P+ 当前过程 ID；可在释放后复用，不能按 actor/tid 去重。每个事件由 Papyrus 桥原样转发，C++ 入队时分配独立 `IngressSequence`。

## 5. RaceMenu

| 项目 | 路径 | 版本 / 接口 | SHA-256 | 使用状态 |
| --- | --- | --- | --- | --- |
| 安装 DLL | `D:/tuanzimouse/mods/RaceMenu Anniversary Edition/SKSE/Plugins/skee64.dll` | 0.4.20.0 | `5225e4e3b185e6fc57c8d31b0cedbe5a030a951d9a744d33071b64c45a38c208` | 运行时依赖检查；S4 才链接接口 |
| 匹配头文件 | `D:/tuanzimouse/mods/RaceMenu Anniversary Edition/ModderResource/IPluginInterface.h` | `IOverlayInterface` v2、`IOverrideInterface` v2 | `093f4763ae483198b2d290c0a1fc32d1024ade822b3f8aeb044bbb174df15a2d` | 1170 集成依据 |
| 参考源码 | `E:/source/repos/RaceMenu` | 源码声明 1.7.99 / GOG 1179 | — | 仅查阅；不用于 1170 编译 |

RaceMenu 接口通过消息 `0x9E3779B9` 交换，`QueryInterface("Overlay")` / `QueryInterface("Override")`。`AddOverlays(reference, bool defer = true)` 与 `AddNodeOverride` 的 `bool` 参数在公开头文件与实现中的语义存在差异，S4 接入时必须显式传值并以 1170 实测为准。S2 在 `checkDependencies()` 中检查 `skee64.dll` 已加载且文件版本为 `0.4.20.0`；版本不同会报告 `VersionMismatch` 并保持入口关闭。

## 6. SKSE Menu Framework

| 项目 | 路径 | 版本 | SHA-256 | 使用状态 |
| --- | --- | --- | --- | --- |
| 安装 DLL | `D:/tuanzimouse/mods/SKSE_Menu_Framework/SKSE/Plugins/SKSEMenuFramework.dll` | 3.10.0.0 | `e3e893677769a374e8927cd3acd79e6c43cf224d86e6cc817ada069c4da7aa69` | 依赖检查；S8 菜单 |
| 安装配置/资源 | `D:/tuanzimouse/mods/SKSE_Menu_Framework/SKSE/Plugins/` | 3.10 | — | 冒烟测试需要完整目录（字体、字符串、主题） |
| 新源码头文件 | `E:/source/repos/SKSE-Menu-Framework-3-master/resources/SKSEMenuFramework.h` | 源码 3.14 | `03a39ccef309d234b0575e307f741907a18a389dc4a9e398730a1823db8c5153` | 仅核对；不直接作为 3.10 二进制头文件 |

已核对的 3.10 DLL 导出包含 `AddSectionItem`、`AddWindow`、`GetMainWindow`、`GetMenuFrameworkVersion` 等；`GetMenuFrameworkAPIVersion` 不在导出表。源码头文件的 `FullPathAddSectionItem` 不是独立导出，实际仍调用 `AddSectionItem`。S8 只使用 3.10 已验证的导出；S2 依赖检查核对文件版本 `3.10.0.0`；中文菜单还需要在框架 ini 中启用中文字体。

### 6.1 项目 ESP 的 masters

`package/Data/SLACS_2_Plus_SCO.esp` 的 TES4 master 顺序：

| 顺序 | Master | 运行时检查 |
| --- | --- | --- |
| 1 | `Skyrim.esm` | 由游戏本体保证 |
| 2 | `SexLab.esm` | `Runtime::checkDependencies` 检查已加载 |
| 3 | `SkyUI_SE.esp` | 当前未单独检查；缺失会导致 ESP 在 DataLoaded 前加载失败 |
| 4 | `RaceMenu.esp` | 当前未单独检查；缺失会导致 ESP 在 DataLoaded 前加载失败 |

这两个额外 master 是旧 ESP 的既有依赖，本次没有改写 TES4 master 列表；干净配置必须同时安装它们。

## 7. 交付资源与工具

| 项目 | 路径 | 说明 |
| --- | --- | --- |
| 项目 ESP | `E:/source/repos/SLACS2 NG/package/Data/SLACS_2_Plus_SCO.esp` | 25,850 字节；由旧 ESP 清理生成，后续只编辑此副本 |
| 旧 ESP 来源 | `E:/source/repos/SLACS2 p+ 0.7/SLACS_2_Plus_SCO.esp` | 39,917 字节；只读参考 |
| 旧脚本来源 | `E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc` | 只读参考；业务已迁 C++ |
| 7-Zip | `C:/Program Files/7-Zip/7z.exe` | 仅备用打包工具 |
| 依赖备份 | `C:/Users/15219/AppData/Local/Temp/opencode/slacs2ng-game-backup/` | 冒烟测试前的物理游戏文件备份 |

## 8. 未采用或尚待接入

- RaceMenu 源码目标 1.7.99/GOG 1179 与当前安装的 1170 `skee64.dll` 不匹配，未直接编译。
- Menu Framework 3.14 源码头文件与安装的 3.10 二进制并非同一版本，S8 前需要重新按 3.10 导出面核对。
- 物理游戏内 P+ 为 2.16.0.0，而目标参考是 P+ 2.18.1；S3 查询测试应在启用 P+ 2.18.1 的 MO2 配置中进行。
- 本次没有完成 1170 游戏内 DataLoaded 之后的行为验证；物理游戏在消息 0 期间退出，详见 `SESSION-01.md`。
