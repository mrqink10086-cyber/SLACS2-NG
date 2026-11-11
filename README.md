# SLACS2 NG

SLACS2 NG 是 SLACS 2 的原生 C++ 重写：把原来由 Papyrus、StorageUtil、JContainers、PO3 承担的体液层状态迁到 SKSE64 插件，用 C++ 容器、调度器和 SKSE 序列化实现七区域 × 五层的覆盖层业务。

- 只支持 Skyrim SE **1.6.1170**（`SKSE::Init` 之前拒绝其他运行时）。
- 复用原 `SLACS_2_Plus_SCO.esp` 的身份：Main Quest `SLACS_Main_Q`、VMAD 脚本类 `SLACS_Main`，Papyrus 只做 P+ 事件转发。
- 菜单改用 SKSE Menu Framework，单一入口、同页分组的全中文界面。
- 面向新档设计，存档格式为版本化 co-save；全局配置在 `Data/SKSE/Plugins/SLACS2NG.json`。

当前程序版本 **1.0.7**，纹理包独立版本 **1.0**。装备视觉由 C++ 直接播放 EFSH，使用现有 0–26 组合索引，迁移时清除旧版效果法术。纹理包补齐之前遗漏的 26 张装备效果贴图，并按 ESP 的 EFSH 记录验证资源引用；换装、读档和 NPC 实际渲染仍需游戏内验收。

## 功能

- **七区域、五层**：Face、BodyVaginal、BodyAnal、BodyBreast、BodyLegs、Hands、Feet，每区默认五层，LIFO 移除。
- **唯一自动加层入口**：P+ 的 `SexlabOrgasmSeparate`，经 `SLACS_Main` 薄桥把 `(sourceActor, tid)` 交给 native；`HookAnimationStart/End` 只维护过程上下文。
- **衰减与清洗**：自然衰减补算睡眠、等待、快速旅行经过的游戏时间；雨水只累计已观测到的室外降雨；游泳立即清洗；120 活动秒整体清洗（暂停/菜单冻结期间不累计）。
- **26 个效果组合**：口腔、阴道和肛门各有 0/1/2 级，组合索引为 `anal*9 + vaginal*3 + oral`，0 表示关闭。C++ 从 ESP 解析 EFSH，直接播放并核验实例；不通过添加法术维持视觉。
- **RaceMenu 覆盖层**：通过 `skee64` 接口读取实际加载的 INI 容量，自动分配空槽与本插件已有槽，保留其他模组占用；槽位不足时保留逻辑层并限频重试。
- **中文设置菜单**：打开“体液覆盖/设置”，在同页展开覆盖层、衰减清洗及角色管理分组，统一应用或保存。配置跨存档共用；菜单只读运行时快照并提交值命令，动作携带会话 epoch，换档后旧动作被拒绝。
- **NPC 支持**：自动过程不要求玩家或随从在场；成年女性 NPC 可接收覆盖层。菜单点击“刷新已加载 NPC”后选择角色，或面向角色后打开菜单并点击“选择准星 NPC”。候选列表按需刷新，常规 pump 不枚举场景角色。
- **诊断**：每 60 秒输出一行 `Perf` 统计（actor 数、环境采样、覆盖层提交、pump 耗时、队列深度、P+ 请求延迟）。

## 运行环境

| 组件 | 版本/标识 |
| --- | --- |
| Skyrim SE | 1.6.1170.0 |
| SKSE64 | 2.2.6（`skse64_1_6_1170.dll`） |
| P+ / SexLab | 参考 2.18.1（`SexLab.esm`、`SexLabUtil.dll`） |
| RaceMenu | 0.4.20（`skee64.dll`） |
| SKSE Menu Framework | 提供本插件所需菜单导出的兼容版本，不锁定 DLL 文件版本 |
| SkyUI | `SkyUI_SE.esp`（ESP 主文件要求） |

RaceMenu 的 `Data/SKSE/Plugins/skee64.ini` 决定可用容量；插件直接采用 RaceMenu 已加载的值，无需填写起始槽。完整显示七区域各五层所需的空闲容量和 INI 设置见 [安装说明](package/README.zh-CN.md#racemenu-必须调整的设置)。

## 构建

需要 [XMake](https://xmake.io) 3.0+（本机验证 3.0.9）与 MSVC 2022 / C++23：

```powershell
git clone --recurse-submodules https://github.com/mrqink10086-cyber/SLACS2-NG "SLACS2 NG"
cd "SLACS2 NG"
xmake f -p windows -a x64 --toolchain=msvc -y
xmake build -y "SLACS2 NG"
```

`after_build` 会自动执行 `tools/package.py`：编译 Papyrus 桥，校验 ESP/VMAD、必需的区域 diffuse、ESP 引用的装备效果贴图、字体缺字、菜单字符串和 1170 运行时元数据，并把 DLL 放进 `package/Data`。区域 normal/specular 按 profile 作为可选通道检查；EFSH 的非空贴图引用缺失会阻止打包。纹理包交付 264 张区域贴图及 26 张装备效果贴图。

宿主检查（不启动游戏）：

```powershell
foreach ($t in "slacs_s1_checks","slacs_s4_checks","slacs_s5s6_checks","slacs_s7_checks",
               "slacs_s5_persistence_checks","slacs_s8_checks","slacs_s9_checks",
               "slacs_overlay_checks","slacs_effects_checks","slacs_pplus_checks",
               "slacs_actor_candidates_checks","slacs_retry_schedule_checks",
               "slacs_background_pump_checks","slacs_pplus_callback_checks") {
  xmake build -y $t
  & "build/windows/x64/release/$t.exe"
}
```

日常发布只生成程序包：

```powershell
py tools/release.py
```

输出为 `artifacts/SLACS2NG-Main-1.0.7/` 及同名 ZIP。首次发布或纹理变化时生成纹理包：

```powershell
py tools/release.py --component textures
```

纹理包为 `artifacts/SLACS2NG-Textures-1.0.zip`，独立于程序版本编号；只有资源变化才更新纹理版本并重新发布。`--component all` 同时生成两包。每包的说明和 SHA-256 清单位于各自 `Docs/SLACS2NG/main/` 或 `Docs/SLACS2NG/textures/`，两包的安装路径互不覆盖。

## 安装

1. 备齐上表依赖，确认游戏为 1.6.1170。
2. 将 `SLACS2NG-Textures-1.0.zip` 和 `SLACS2NG-Main-1.0.7.zip` 分别直接拖入 MO2，安装成两个独立模组并同时启用；无需预先解压。启用 `SLACS_2_Plus_SCO.esp`。压缩包根目录直接对应游戏 Data 内容；手动安装时合并到游戏 `Data/`。
3. 按[安装说明](package/README.zh-CN.md#racemenu-必须调整的设置)设置 `skee64.ini` 并重启游戏；菜单会显示 RaceMenu 实际容量。
4. 进游戏按 F1 打开菜单。

以后更新只替换 Main 模组，保留 Textures 模组。旧合包用户也需安装含 26 张装备效果贴图的新纹理包；两项安装完毕后停用旧合包。完整步骤、配置语义、菜单说明与已知限制见 [package/README.zh-CN.md](package/README.zh-CN.md)。

## 仓库结构

```text
src/config       设置模型、JSON 读写与校验
src/core         七区域状态机、加层/清除/结算（纯值类型，无引擎依赖）
src/runtime      生命周期、值命令队列、pump、调度器、环境采样、轮转
src/papyrus      native 绑定与 P+ 事件桥
src/integration  P+ 适配、RaceMenu 覆盖层、效果、3D/引擎查询
src/menu         快照发布与同页分组菜单
src/persistence  co-save 编解码
tests            十四组宿主检查
tools            package.py / release.py / clean_esp.py / verify_*.py
docs             PLAN、INTERFACES、SESSION-01..05、AUDIT、RELEASE-NOTES
```

## 文档

- [实现计划](docs/PLAN.zh-CN.md)
- [接口契约](docs/INTERFACES.zh-CN.md)
- 实现记录：[SESSION-01](docs/implementation/SESSION-01.md)、[02](docs/implementation/SESSION-02.md)、[03](docs/implementation/SESSION-03.md)、[04](docs/implementation/SESSION-04.md)、[05](docs/implementation/SESSION-05.md)
- [源码审核与修复（2026-11-06）](docs/AUDIT-2026-11-06.zh-CN.md)
- [发布说明](docs/release/RELEASE-NOTES.zh-CN.md)

## 验证状态

| 项目 | 状态 |
| --- | --- |
| DLL / PEX / ESP / 290 贴图 / 字体 / 菜单资源构建 | 构建通过；已校验必需区域 diffuse 与 ESP 引用的装备效果贴图，可选通道另行检查 |
| 十四组宿主检查（逻辑、调度、序列化、菜单、时钟、适配器、NPC 候选、重试、后台任务和 P+ 回调） | 全部通过 |
| LLVM 20 AddressSanitizer | 本版原生 EFSH 适配器检查通过，启用栈返回后访问检测；历史检查见审计文档 |
| 菜单动作贯通 UI → Runtime → Core → 外观/效果 | 待游戏验收 |
| 读档、换档、tid 复用、异步取消 | 待游戏验收 |
| 七区域覆盖层、26 效果、与 SlaveTatsNG 共存 | 待游戏验收 |
| 实机性能预算 | 待游戏验收 |

没有完成的实机项目不会被标记为“验收通过”。审核结论与被修复缺陷的清单见 `docs/AUDIT-2026-11-06.zh-CN.md`。

## 已知限制

- RaceMenu 默认容量只能显示部分图层；Body 四区域共享身体槽位，完整容量要求见安装说明。
- Body 主三区和 Face 各有 12 张贴图，可选五张不同贴图叠加；Legs、Hands、Feet 各只有 4 张，第五层会复用其中一张，但仍占独立槽位。
- 未接入 BiS / DnB：没有验证过的外部信号，不写“已支持”。
- UBE 贴图不在发布包内，缺 `textures/slacs2_ube` 是预期状态。
- 保存快照可能比最新一次 pump 晚一拍；保存回调不等待 VM、任务或材质。
- Runtime 析构先关闭入口、停止后台任务并关闭 P+/RaceMenu 回调，再销毁运行状态；不支持在游戏运行中热卸载 DLL。
- 旧 Papyrus 通知不带会话令牌，换档瞬间的极旧事件会被 epoch 拒绝，但不会跨档加层。

## 许可

GPL-3.0，附 Modding Exception（见 [LICENSE](LICENSE)、[EXCEPTIONS](EXCEPTIONS)）。

项目骨架来自 [libxse/commonlibsse-ng-template](https://github.com/libxse/commonlibsse-ng-template)，依赖 [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR)（6.0.0，`15ad90b`）。发布包内含 Noto Sans CJK SC（SIL OFL 1.1，随附许可证）；SKSE Menu Framework、P+、RaceMenu、SkyUI、SKSE 均为外部依赖，不再分发。
