# SLACS2 NG 1.0.4 发布说明

2026-11-10，包名 `SLACS2NG-1.0.4`。

- 自动目标放开至全部成年女性 NPC，不再判断随从身份，也不要求玩家/随从参与过程。移除旧女性目标限制设置；旧 JSON 字段兼容忽略，保存时删除。
- 菜单新增“刷新已加载 NPC”和“选择准星 NPC”；候选默认按女性筛选，玩家及已跟踪角色始终显示。候选仅在显式刷新时扫描，使用连续数组与排序去重；常规 pump 不扫描 NPC 列表。
- 覆盖层先保留已有层的槽位，再分配新增层，避免新层夺占旧槽。每层维持独立贴图；相同材质不重复提交。Body 主三区与 Face 有 12 张资源，前五层使用不同贴图；Legs/Hands/Feet 只有 4 张，第五层复用一张但仍为独立覆盖层。
- 宿主检查覆盖女性 NPC 筛选、旧设置迁移、候选枚举与准星查询、五层贴图选择、旧层槽位稳定、外部槽保护及容量不足恢复。

游戏内叠层显示、准星选角及实机性能仍待验收。

## 1.0.3 历史发布说明

首发日期：2026-11-06（1.0.0）／更新：2026-11-07（1.0.1）
本次构建：2026-11-10，版本号 1.0.3，包名 `SLACS2NG-1.0.3`。
交付状态：**构建完成，待游戏验收**

SLACS 2 的 C++ 重写版本。原本由 Papyrus、StorageUtil、JContainers、PO3 承担的体液层状态迁到 SKSE64 插件，Papyrus 只保留转发 P+ 事件的薄桥。ESP 身份沿用原 `SLACS_2_Plus_SCO.esp`，状态按新档设计（版本化 co-save，不读取旧 Papyrus 存档数据）。只支持 Skyrim SE 1.6.1170。

## 2026-11-10 自动槽位与重试限频

- 直接采用 RaceMenu 从 INI 加载的实际容量，自动扫描空槽与自有槽。Body 四区域共享身体池，每区域可显示最多五层；保留其他模组占用。
- 移除手动起始槽设置，菜单显示 RaceMenu 实际总容量。旧 JSON 的 `firstOwnedSlot` 兼容读取并忽略，保存时移除；co-save 格式不变。
- 角色列表补入当前存活且已加载 3D 的随从。按队友状态识别，并排除受命令角色、儿童与生物，规则参考 devious-curses。
- 覆盖层和效果失败按每项两秒冷却；冷却项不会触发 25ms 积压轮询。暂停时取消未完成环境扫描，恢复后重新采样。
- 完整显示所有图层需要 Body/Face/Hands/Feet 各有 20/5/5/5 个空闲槽；INI 设置见[安装说明](../../package/README.zh-CN.md#racemenu-必须调整的设置)。修改 INI 后重启游戏。

## 2026-11-08 修复构建（1.0.2）

- 空槽只有残留透明度时可正常复用，不再因此只应用一层。
- 修改起始槽时保留其他区域刚迁入的精痕；读档后也会清理旧全局配置范围外的 SLACS 精痕。
- 配置校验修复极大槽位值触发的数组越界，并按 RaceMenu 每域 127 槽限制输入。
- 清理精痕时逐键核对材质所有权，保留其他模组改写的材质和透明度。
- 效果法术添加或移除失败会保留重试，成功缓存只在效果达成后更新。
- 双人场景保留没有对应 P+ CTYPE 的乳部阶段标签回退，多人场景不广播该标签。

三位独立 `gpt-6-luna xhigh` 子代理按代码和业务契约审查。十套原生检查、overlay/effects/S7/P+ 映射的 AddressSanitizer 检查通过；尚待游戏验收。详细记录见 `docs/AUDIT-2026-11-08.zh-CN.md`。

## 原 1.0.1 变更

- **菜单合并为单一入口**：三个分页（设置／衰减与清洗／状态与维护）合并为“体液覆盖/设置”，同页分“覆盖层与效果”“衰减与清洗”“角色管理与运行状态”三个折叠分组，设置区共用“应用并保存”“仅应用”“撤销修改”，角色管理分组默认折叠。改动只涉及 `src/menu/Menu.cpp`、`src/menu/Menu.h`。
- **框架兼容改成按导出检查**：`Runtime` 的依赖报告不再要求 `SKSEMenuFramework.dll` 文件版本恰为 `3.10.0.0`，改用 `Menu::available()` 探测插件实际使用的菜单导出，版本号只写入日志与报告文本（`expected` 字段变为 `required menu exports`）。框架版本与界面布局不同的安装不再被误判为不兼容。
- **其余未变**：配置 schema、co-save 格式、ESP、Papyrus 桥与全部玩法逻辑保持 1.0.0 的行为；升级替换 DLL 即可，配置与存档无需迁移。DLL 资源版本 `1.0.1.0`。

## 功能

- **七区域、五层**：Face、BodyVaginal、BodyAnal、BodyBreast、BodyLegs、Hands、Feet 各自独立，每区默认五层，LIFO 移除；区域之间互不干扰。
- **唯一自动加层入口**：只挂 P+ 的 `SexlabOrgasmSeparate`，`SLACS_Main` 把 `(sourceActor, tid)` 交给 native；`HookAnimationStart/End` 只维护过程上下文。
- **衰减与清洗**：自然衰减补算睡眠、等待、快速旅行经过的游戏时间；雨水只累计已观测到的室外降雨；游泳立即清洗；120 活动秒整体清洗（可关），暂停/菜单冻结期间不累计，32 位 `runTimeMS` 回绕已处理。
- **26 个效果组合**：口腔 × 阴道 × 肛门 × 等级（0/1/2），FormID 从 ESP 解析，索引 0 为关闭占位。
- **RaceMenu 覆盖层**：直接调用 `skee64` 的 C++ 接口，采用实际 INI 容量并自动分配空槽；槽位不足保留逻辑层并限频重试。Body 写 diffuse + normal + specular，Face 写 diffuse + normal。
- **中文设置菜单**：单一“体液覆盖/设置”入口，覆盖层、衰减清洗、角色管理在同页分组展开，统一应用或保存。全局配置跨存档共用；菜单只读运行时快照并提交值命令，动作携带会话 epoch，换档后旧动作被拒绝。框架兼容性按所需导出检查，不再锁定 DLL 文件版本；缺失框架或所需导出时会记录依赖错误并停止初始化。
- **配置与存档**：`Data/SKSE/Plugins/SLACS2NG.json`，UTF-8，schemaVersion 1，临时文件写盘后可回滚；非法值/未知字段/枚举错误按中文字段说明拒绝，`SettingsFeedback` 分别报告应用与写盘状态。角色层、衰减余量与整体清除剩余时间存在版本化 co-save；读档不覆盖全局配置。
- **诊断**：每 60 秒输出一行 `Perf`（actor 数、环境采样、覆盖层提交、槽位、pump 次数与平均/最大耗时、队列峰值、P+ 请求延迟）。
- **身份与兼容**：复用 Main Quest `SLACS_Main_Q` 与 VMAD 脚本类 `SLACS_Main`，任务条件与 alias 不需要改动；Papyrus 不再保存状态。

## 相对 1.0.0-rc1 的修复

| 编号 | 问题 | 修复 |
| --- | --- | --- |
| F1 | 正常读档的 `PreLoad → Revert → Load → PostLoad` 中，Revert 再次启动会话并改变 epoch，Load staging 被拒收，最终提交空状态。 | 已有 PreLoad 事务时，Revert 只使 Quest 绑定失效并保留加载窗口；独立 Revert 仍执行重置（`src/runtime/Runtime.cpp:288`、`:2044`）。 |
| F2 | rev N 视觉更新失败进入 dirty 队列后，rev N+1 成功没有撤销旧项；旧项可能永久遭 StaleRevision 拒绝，或在新目标为清空时恢复已清除的覆盖。 | 成功或已过期的目标清除 dirty 项与重试；重放前校验当前 Core actor 与 region revision（`src/runtime/Runtime.cpp:1367`、`:1464`）。 |
| F3 | dirty 队列每批都从同一批不可用角色开始，超过 64 项时后面的可用角色可能一直得不到处理。 | 有序 map 配合上次 key 的 upper_bound/wrap，跨批次公平轮转且不保存易失效迭代器（`src/runtime/RoundRobin.h:7`、`src/runtime/Runtime.h:155`）。 |
| F4 | 视觉重试设置 `report.deferred`，使逻辑保存快照停止发布；OverlayRetry deadline 分支仍会提前执行视觉队列。 | 完成逻辑批次后先发布快照再执行视觉/效果重试；OverlayRetry deadline 只消费唤醒，快照使用发布时的时间（`src/runtime/Runtime.cpp:766`、`:874`）。 |
| F5 | Scheduler 每次 upsert 追加堆节点、只删堆顶旧节点，早期有效 timer 可挡住后方旧节点；环境采样每秒重发相同 ID，堆可持续增长。 | 改为可擦除有序索引，每个有效 ID 在时钟域内只有一个节点；同 due 只替换 payload，改期/跨域/取消立即移除旧索引（`src/runtime/Scheduler.cpp:43`、`:154`）。 |
| F6 | `pumpNow` 在 pump 锁外推进可变 ActiveClock，解锁后再读 dirty/Quest 状态；save fallback 读取普通可变时间成员。 | 同一 `pumpMutex` 下取时、运行 `pumpLocked`、决定后续唤醒；删除共享 `lastObservedTime_`，保存只读原子发布的不可变快照（`src/runtime/Runtime.cpp:642`、`:690`、`:1961`）。 |
| F7 | 环境采样与首次 3D 检查每轮遍历全部角色，绕过 command/deadline 预算；菜单发布对每个角色再次解析 3D。 | 环境每批至多 64 个/2ms、3D 每批至多 64 个/1ms，未完成扫描 25ms 后续跑；菜单读取缓存 3D 状态（`src/runtime/Runtime.cpp:888`、`:1464`、`:1813`）。 |
| F8 | 新会话尚无成功自动加层时菜单没有 Core actor，无法对玩家做首次测试加层。 | 菜单增加未跟踪玩家候选，TestAdd 后重新解析 actor 并由 Core 创建业务状态；候选禁用清除/刷新/TestClear（`src/runtime/Runtime.cpp:1714`、`:1843`、`src/menu/Menu.cpp:469`）。 |

新增回归位于 `tests/s5s6_checks.cpp:117`（same-due 最新 payload、索引增长、取消、跨时钟域、steady timer）与 `tests/s9_checks.cpp:42`（130 项队列中前 128 项不可用时仍处理末尾可用项、删除/清空/回绕）。S8 新增 `Menu::publish/snapshot/submit` 与 `Settings::load/validate/save`；S9 新增中文字符串、Noto Sans CJK SC 字体与暂停期间活动秒不累计。

## 实际 1170 依赖组合

| 组件 | 版本/标识 |
| --- | --- |
| Skyrim SE | 1.6.1170.0 |
| SKSE64 | 2.2.6，`skse64_1_6_1170.dll` |
| CommonLibSSE-NG | 6.0.0，commit `15ad90b31b2ca09cf477c7e1a43ff5ff05af120f` |
| P+ / SexLab | 参考 2.18.1，`SexLab.esm` 381,243 字节 |
| RaceMenu | 0.4.20，`skee64.dll` 0.4.20.0 |
| SKSE Menu Framework | 提供所需菜单导出的兼容版本；原核对样本为 3.10.0.0，SHA-256 `e3e893677769a374e8927cd3acd79e6c43cf224d86e6cc817ada069c4da7aa69`，此文件版本及哈希不作为运行限制 |
| 中文字体 | Noto Sans CJK SC Regular，OFL 1.1 |

`SLACS_2_Plus_SCO.esp` 主文件：`Skyrim.esm`、`SexLab.esm`、`SkyUI_SE.esp`、`RaceMenu.esp`。

## 构建

```powershell
cd "E:/source/repos/SLACS2 NG"
xmake f -p windows -a x64 --toolchain=msvc -y
xmake build -y "SLACS2 NG"
```

构建后 `tools/package.py` 自动执行：ESP 结构/VMAD、264 个贴图、菜单配置与字符串、字体缺字、Papyrus 编译、PEX 字符串、1170 运行时元数据，并把 DLL 放入 `package/Data`。

## 测试

```powershell
foreach ($t in "slacs_s1_checks","slacs_s4_checks","slacs_s5s6_checks","slacs_s7_checks","slacs_s5_persistence_checks","slacs_s8_checks","slacs_s9_checks","slacs_overlay_checks","slacs_effects_checks","slacs_pplus_checks") {
  xmake build -y $t
  & "build/windows/x64/release/$t.exe"
}
```

## 发布产物

```powershell
py tools/release.py
```

本次输出 `artifacts/SLACS2NG-1.0.3/`、同名 `.zip` 与 `FILE-MANIFEST.json/.txt`。旧版本产物保持为历史构建。

## 安装

1. 安装上述 1170 依赖组合。
2. 将 `Data` 合并到游戏目录；启用 `SLACS_2_Plus_SCO.esp`。
3. 按[安装说明](../../package/README.zh-CN.md#racemenu-必须调整的设置)调整 `skee64.ini`；插件自动使用实际容量，无需填写起始槽。
4. 进入游戏按 F1，打开“体液覆盖/设置”，在同页展开分组并调整设置。

配置路径：`Data/SKSE/Plugins/SLACS2NG.json`。读取存档不会覆盖该全局文件；每个存档的角色状态在 co-save。

## 验证记录

| 类别 | 项目 | 状态 | 环境/命令 | 结果 | 待执行条件 |
| --- | --- | --- | --- | --- | --- |
| 构建 | DLL 链接 | 构建完成 | `xmake build -y "SLACS2 NG"` | 成功 | — |
| 构建 | PEX/VMAD/ESP | 构建完成 | `tools/package.py` | 0 错误；26 MGEF/SPEL、QUST、FLST 校验通过 | — |
| 构建 | 贴图/字体/菜单资源 | 构建完成 | `tools/package.py`、`tools/verify_chinese_font.py --strict` | 264 DDS；无缺字 | — |
| 自动检查 | 逻辑/调度/序列化/菜单/时钟 | 构建完成 | 七个 `slacs_s*_checks.exe` | 全部 PASS | — |
| 游戏实测 | 菜单动作贯通 | 待游戏验收 | 1170 + P+ + RaceMenu + Framework 3.10 | 未执行 | 可启动的 1170 环境 |
| 游戏实测 | 配置重启/读档保持 | 待游戏验收 | 同上，修改后重启并读档 | 未执行 | 同上 |
| 游戏实测 | 唯一自动加层入口 | 待游戏验收 | `SexlabOrgasmSeparate` 场景 | 未执行 | P+ 2.18.1 兼容环境 |
| 游戏实测 | 多人/仅随从 | 待游戏验收 | 玩家+随从+NPC 场景 | 未执行 | 同上 |
| 游戏实测 | tid 复用/异步取消/连续换档 | 待游戏验收 | 连续场景、快速读档 | 未执行 | 同上 |
| 游戏实测 | NPC/3D/七区域/26 效果 | 待游戏验收 | 调整 skee64.ini 后逐区域检查 | 未执行 | RaceMenu 槽位足够 |
| 游戏实测 | 覆盖层共存 | 待游戏验收 | 与 SlaveTatsNG 同装 | 未执行 | 共存环境 |
| 游戏实测 | 性能预算 | 待游戏验收 | 60 秒 `Perf` 日志 | 未执行 | 实机采样 |

本次没有可用的 1170 游戏环境，因此没有任何项目标记为“游戏验收通过”。

## 性能记录方式

运行时每 60 秒输出：

```text
Perf actors=... envSamples=... overlaySubmits=... overlaySlots=... pumps=... pumpAvgUs=... pumpMaxUs=... queueMax=... requests=... reqAvgUs=... reqMaxUs=...
```

验收时需记录代表场景（单人/多人/仅随从/NPC 卸载）的 actor 数、环境采样次数、覆盖层提交次数与槽位数、pump 平均/最大耗时、队列深度、P+ 请求延迟。当前预算保持 `WorkBudget{maxCommands=256,maxDeadlines=256,maxWallTime=5000us}` 和每 pump 最多 64 个脏视觉目标；没有实机数据前不做无依据调整。

## BiS / DnB

未接入。没有验证过的外部信号，因此不写“已支持”。现有 `ClearActor`/`TestClear` 命令可直接作为将来信号触发清除的接入点；验收记录中标为条件集成项。

## 分发条件

- Noto Sans CJK SC：SIL OFL 1.1，随包附许可证。
- SKSE Menu Framework：只分发配置格式与中文字符串，不重分发 DLL。
- P+、RaceMenu、SkyUI、SKSE：外部依赖，不随包分发。

## 已知限制

- 必须调整 RaceMenu 槽位；默认 6/3/3/3 且 `bPlayerOnly=1` 时只能显示部分区域。
- UBE 贴图不在发布包内，缺 `textures/slacs2_ube` 是预期状态。
- 保存快照可能比最新一次 pump 晚一拍；保存回调不等待 VM、任务或材质。
- 没有自动 `Runtime::shutdown`；进程退出由 SKSE 回收。
- 旧 Papyrus 通知不带会话令牌，换档瞬间的极旧事件会被 epoch 拒绝，但不会跨档加层。
- 面向新档设计：co-save 为新的版本化格式，不迁移旧 Papyrus 存档里的层状态。
