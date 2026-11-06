# SESSION-01：S1/S2 实现与验证记录

更新：2026-11-05。工作目录：`E:/source/repos/SLACS2 NG`。本次只实现 S1 和 S2；S3 及之后的业务模块没有提前写入。

## 1. 本次结论

已完成：

- C++23 / CommonLibSSE-NG 基础运行时、值类型、设置、值队列、ActorRegistry、有界串行 pump 和单个后台唤醒线程。
- Plugin Load 的精确 1170 检查和错误运行时提前拒绝；DataLoaded 依赖/表单检查；新游戏与成功读档的会话切换。
- 原 `SLACS_Main_Q`、同名 `SLACS_Main.psc/.pex` 薄桥；三项 P+ 事件订阅和三个 Global Native。
- 项目 ESP 副本清理：删除旧 Main 业务属性、MCM/SpellHandler Quest、旧 FormList、33 条 SexLab MGEF 覆盖；保留自有 Spell/MGEF/EFSH 和 Mega 种族 FormList。
- 构建时自动编译 Papyrus、核对 ESP/VMAD/PEX、生成本包 DLL。
- S1 自动检查通过；ESP 静态核对通过；精确 1170 的插件加载和 `systems installed` 日志已确认。

尚未完成：

- 1170 游戏内 DataLoaded 之后的 S1/S2 行为验证。物理测试游戏在 SKSE 消息 0 分发期间退出，未到达 DataLoaded；不能据此宣称游戏内验收通过。
- PPlusAdapter、BodyVaginal、Core、Overlay、Scheduler、完整 Serialization、菜单和效果属于后续步骤。
- `SLACS2NG.json` 没有随包写入默认文件；缺失时按契约使用内存默认值，这是当前行为。

## 2. S1 实现状态

| 模块 | 状态 | 实现位置 | 说明 |
| --- | --- | --- | --- |
| 公共值类型 | 完成 | `src/core/Types.h` | 契约中的身份、时钟、区域、层、诊断、预算、生命周期类型 |
| Settings | 完成 | `src/config/Settings.h/.cpp` | 固定 schema 1 的 JSON 读写；缺失文件使用默认值；保存前验证 |
| Ingress | 完成 | `src/runtime/Ingress.h/.cpp` | 有界值队列；`postOrgasm` 在锁内分配 `IngressSequence`；唤醒回调在解锁后调用 |
| ActorRegistry | 完成 | `src/runtime/ActorRegistry.h/.cpp` | FormID 到会话内 ActorId；重新解析 Actor、handle、3D；reset 时新会话重新编号 |
| Runtime | 完成最小可用版 | `src/runtime/Runtime.h/.cpp` | 安装回调、依赖检查、会话 epoch、命令消费、Quest 重试、快照发布 |
| 后台 pump | 完成 | `src/runtime/BackgroundPump.h/.cpp` | 单 jthread 等待；单个 `taskPending_` 门；通过 SKSE `AddTask` 派发一次任务线程 tick；不自我 AddTask；执行线程/阶段仍以 1170 日志为准 |
| 持久化占位 | S5 占位 | `src/persistence/Serialization.h/.cpp` | 已定义 `SaveSnapshot/LoadResult` 和 schema 1 二进制记录；S5 再扩展正式格式 |
| 入口 | 完成 | `src/main.cpp` | 先比较精确 runtime，再 `SKSE::Init`；安装 Runtime 和 Papyrus |
| 构建/打包 | 完成 | `xmake.lua`、`tools/package.py` | 精确 1170 元数据；构建后编译 PEX、核对 ESP/VMAD/PEX、复制 DLL |

### 2.1 游戏处理入口选择

本次采用“单等待线程 + SKSE task 派发”，没有把 SKSE 更新回调直接作为业务 pump：

- `BackgroundPump` 的线程只负责等待和合并唤醒，不接触 RE/VM/RaceMenu。
- 每次唤醒最多设置一个待执行 SKSE task；`taskPending_` 防止同一会话出现多个并发任务。
- task 在 SKSE task 回调中执行 `Runtime::pump`；SKSE 2.2.6 不承诺固定线程或下一帧，真实执行阶段仍需游戏内验证。`maxCommands` 和命令间 `maxWallTime` 已生效，`maxDeadlines` 留给 S6 的共享 Scheduler。快照、Quest 重试和设置写盘不计入命令循环预算，其中前两者在超时后会跳过到下一节拍，设置写盘仍是同步例外。
- pump 发现还有余量时，通过后台线程请求下一次独立节拍，而不是在 task 内部再次 AddTask，避免同一次任务排空期间自递归。
- 该选择是 Scheduler 内部实现；`Runtime::pump` 和命令接口不依赖具体派发方式。

入口核验结论：

- 对照的现有入口是 SKSE 更新/任务回调；`AddTask` 不承诺固定线程或下一帧执行，官方实现的 task 池会连续排空任务，因此不能依赖“本次排空结束后原线程仍在同一帧”，也不能在 task 内部自递归 AddTask。
- 选择“单等待线程 + 每次一个 SKSE task”可以保证同一会话只有一个待执行泵任务；`taskPending_` 是重入闸门，余量通过后台线程的后续节拍重新派发。
- P+ 事件和 Papyrus native 只在发送线程复制参数/入队，不访问 Actor、VM、3D 或 RaceMenu；这些访问只在 pump 的 SKSE task 阶段发生。
- 该入口只影响 Scheduler 派发；S6A 会把 Quest 重试、衰减和采样并入共享 Scheduler，但 `Runtime::pump` 的预算和命令契约不变。

## 3. S2 实现状态

| 内容 | 状态 | 位置 |
| --- | --- | --- |
| 薄 Papyrus 桥 | 完成 | `src/papyrus/SLACS_Main.psc` |
| PEX 构建与校验 | 完成 | `tools/package.py`、`package/Data/Scripts/SLACS_Main.pex` |
| Papyrus native 注册 | 完成 | `src/integration/Papyrus.cpp` |
| Quest 查找与订阅 | 完成 | `src/integration/QuestBridge.cpp` |
| 新游戏/读档重订阅 | 完成 C++ 路径 | `Runtime::onSessionReady`、`Runtime::processQuestRetry` |
| ESP/VMAD 清理 | 完成 | `tools/clean_esp.py`、`package/Data/SLACS_2_Plus_SCO.esp` |
| Runtime 通知消费 | 完成记录版 | `Runtime::processNativeOrgasm`、`processSceneNotice` |

### 3.1 Papyrus 桥内容

`SLACS_Main.psc` 只保留：

```papyrus
ScriptName SLACS_Main extends Quest

Function RegisterEvents()
Function UnregisterEvents()
Event OnPPlusOrgasm(Form akSource, int aiTid)
Event OnPPlusStart(int aiTid, bool abHasPlayer)
Event OnPPlusEnd(int aiTid, bool abHasPlayer)
Function NotifyOrgasm(Form akSource, int aiTid) Global Native
Function NotifySceneStart(int aiTid, bool abHasPlayer) Global Native
Function NotifySceneEnd(int aiTid, bool abHasPlayer) Global Native
```

- `RegisterEvents` 先调用 `UnregisterEvents`，再登记 `SexlabOrgasmSeparate`、`HookAnimationStart`、`HookAnimationEnd`。
- Event 只转发参数，不读取属性、不查询 P+、不保存状态。
- 没有旧 `SLACS_Main` 的业务属性、OnInit、MCM、StorageUtil、JContainers 或效果处理代码。

### 3.2 订阅与重试

- Quest 按插件文件名 `SLACS_2_Plus_SCO.esp` 和 local ID `0x000D62` 查找，不使用加载顺序前缀。
- `QuestBridge::trySubscribe` 先调用 `UnregisterEvents` 再调用 `RegisterEvents`，避免重订阅产生重复监听。
- `onSessionReady` 只登记 `readyRequested_` 并请求后台节拍；pump 的 `activateSession` 在依赖、生命周期和读档提交都完成后才开放 ingress。首次订阅由 pump 发起；失败后 500ms 重试，最多 40 次；每次重试由后台唤醒另行派发。
- 新游戏走 `kNewGame`；读档走 `kPreLoadGame` → load staging → 成功 `kPostLoadGame`；revert 走序列化 revert 回调。`beginSession` 在消息线程关闭入口、递增 epoch 并记录待处理转换；清理、Actor 绑定和激活都在 `pump` 持 `pumpMutex_` 的入口阶段串行完成，避免与命令处理并发。

### 3.3 ESP 清理结果

`package/Data/SLACS_2_Plus_SCO.esp`（25,850 字节）静态核对：

| 组 | 数量 | 说明 |
| --- | --- | --- |
| MGEF | 26 | 只保留自有 `mf_spell_*` |
| SPEL | 26 | 自有 Spell |
| QUST | 1 | 只保留 `SLACS_Main_Q` 04000D62 |
| EFSH | 26 | 自有 Effect Shader |
| FLST | 1 | 保留 `SLACS_MegaCumRaces` 04004E66 |

已删除：33 条 `SexLab*Effect` MGEF 覆盖、`SLACS_MCM_Q` 04000D63、`SLACS_SpellHandler_Quest` 04002E19、`SLACS_AffectedActorsList` 04000D64。`SLACS_Main_Q` 的 VMAD 替换为最小 `SLACS_Main` 脚本记录，无属性。

## 4. 修改入口

| 文件 | 本次修改 |
| --- | --- |
| `src/main.cpp` | 精确 1170 检查、`SKSE::Init`、Runtime 安装、Papyrus 注册 |
| `src/core/Types.h` | S1 公共值类型和 Core/Scheduler DTO |
| `src/config/Settings.h/.cpp` | 默认配置、验证、JSON 读写 |
| `src/runtime/Ingress.h/.cpp` | RuntimeCommand 队列、序号、容量 |
| `src/runtime/ActorRegistry.h/.cpp` | FormID/ActorId/handle 映射 |
| `src/runtime/BackgroundPump.h/.cpp` | 单等待线程与 SKSE task 派发 |
| `src/runtime/Runtime.h/.cpp` | 生命周期、消息、pump、命令消费、Quest 重试 |
| `src/integration/Papyrus.h/.cpp` | 三个 native 注册与入队 |
| `src/integration/QuestBridge.h/.cpp` | Quest 查找、VMAD 检查、Register/Unregister 调用 |
| `src/persistence/Serialization.h/.cpp` | S5 占位快照与二进制记录 |
| `src/papyrus/SLACS_Main.psc` | 薄桥源码 |
| `xmake.lua` | 精确 1170 元数据、构建后打包、S1 检查目标 |
| `tools/package.py` | PEX 编译顺序、ESP/VMAD/PEX/元数据核验、DLL 复制 |
| `tools/clean_esp.py` | 旧 ESP 清理与校验 |
| `tests/s1_checks.cpp` | Settings/Ingress 自动检查 |

## 5. 公开接口

公开类型和签名以 `docs/INTERFACES.zh-CN.md` 为准，本次实现没有改变契约。当前可调用入口：

```cpp
namespace slacs
{
    Runtime& Runtime::instance();

    bool Runtime::install();
    DependencyReport Runtime::checkDependencies() const;
    bool Runtime::initialize();
    void Runtime::beginSession(SessionReason a_reason);
    void Runtime::onLoadStaging(LoadResult a_result);
    LoadCommitResult Runtime::commitLoadStaging();
    void Runtime::onSessionReady();
    void Runtime::shutdown();
    void Runtime::publishSaveSnapshot();

    SessionEpoch Runtime::epoch() const noexcept;
    IngressResult Runtime::enqueue(RuntimeCommand a_command);
    PumpReport Runtime::pump(const GameTime& a_now, WorkBudget a_budget);
    Ingress& Runtime::ingress() noexcept;

    IngressResult Ingress::post(RuntimeCommand a_command);
    IngressResult Ingress::postOrgasm(FormId a_sourceFormId, Tid a_tid, SessionEpoch a_epoch);

    std::optional<ActorId> ActorRegistry::bind(FormId a_formId);
    std::optional<ActorLease> ActorRegistry::resolve(ActorId a_actor, ResolveNeed a_need);
    std::optional<FormId> ActorRegistry::formId(ActorId a_actor) const;
    void ActorRegistry::reset(SessionEpoch a_epoch);

    bool Native::Register(RE::BSScript::IVirtualMachine* a_vm);
    void Native::NotifyOrgasm(RE::StaticFunctionTag*, RE::TESForm*, std::int32_t);
    void Native::NotifySceneStart(RE::StaticFunctionTag*, std::int32_t, bool);
    void Native::NotifySceneEnd(RE::StaticFunctionTag*, std::int32_t, bool);
}
```

`Runtime::processNativeOrgasm` 是下一次接 PPlusAdapter 的消费点；当前它验证 epoch、绑定 ActorId、记录 FormID/tid/sequence/count，只写日志和计数，不产生 Core 层。因此“通知消费”只证明命令被 pump 取走并记录，不证明加层或视觉效果。`Runtime::processPPlusCompleted` 已经能接收成功/失败结果并记录，S3 从这里接查询完成结果。

## 6. 构建

环境与依赖见 `docs/implementation/DEPENDENCIES.md`。

```powershell
xmake f -p windows -a x64 --toolchain=msvc -y
xmake build -y -r
```

原始控制台输出保存在 `docs/implementation/build-s1.log`。结果：

- C++ 编译、链接成功，控制台无 error/warning 输出。
- `PapyrusCompiler.exe`：`0 error(s), 0 warning(s)`，`Assembly succeeded`。
- `package ready: E:\source\repos\SLACS2 NG\package\Data`
- `build ok`。

产物：

| 产物 | 大小 | 位置 |
| --- | --- | --- |
| DLL | 1,692,160 字节 | `build/windows/x64/release/SLACS2 NG.dll` |
| 包内 DLL | 1,692,160 字节 | `package/Data/SKSE/Plugins/SLACS2 NG.dll` |
| 包内 PEX | 1,198 字节 | `package/Data/Scripts/SLACS_Main.pex` |
| 项目 ESP | 25,850 字节 | `package/Data/SLACS_2_Plus_SCO.esp` |

本次交付对应的工作区快照（未提交 commit；`git HEAD` 为 `117325dfe8ef4351b6a973e55692ea522dfbf026`，以下文件为未跟踪/修改状态）：

| 文件 | SHA-256 |
| --- | --- |
| `src/runtime/Runtime.cpp` | `231f08b0b9e7c38a528e06f311059e1afb7e1dd50d72b51e3f2c3d7cce26f007` |
| `src/runtime/Runtime.h` | `b12c07174a030f3e1bffeb2caa5c534ba1cdf2fb79b89e58ef91edb417d9589e` |
| `src/integration/QuestBridge.cpp` | `de4eb93e5951eae2341458edd24f41d872e0af865f5eb043065943e4943f5a3b` |
| `src/core/Types.h` | `f0d2ac88c613204d0f7004ccb10500634979852b660a0c98beae290e8df6690d` |
| `src/persistence/Serialization.cpp` | `4f5eea1b0b8b19f3ac6ff444e0d4ff0da4928f4fbd6e2d65f6f1b80dd662aa5b` |
| `build/windows/x64/release/SLACS2 NG.dll` | `2230554ed198d5d0c889847d13ee1085bf9bd3fba358b2e529928e61e74edf09` |
| `package/Data/Scripts/SLACS_Main.pex` | `0064d394becd06126d9869560c07158783e0e03e03760cf6b5e9583bc5a7d247` |
| `package/Data/SLACS_2_Plus_SCO.esp` | `77477cfe385a67e3c7e8c487b60450cb94f549913aeca1523063f97d6669976e` |

生成元数据已核对为：

```cpp
SKSE::PluginDeclaration::RuntimeCompatibility{
    SKSE::PluginDeclaration::VersionNumber{ 1, 6, 1170, 0 }
}
```

## 7. 自动检查

### 7.1 S1 检查

```powershell
xmake build -y slacs_s1_checks
./build/windows/x64/release/slacs_s1_checks.exe
```

结果：`SLACS2 NG S1 checks passed`。输出保存在 `docs/implementation/s1-checks.log`。必须在仓库根运行；测试会在当前工作目录写临时 JSON，工作目录不可写时会得到保存失败而不是测试逻辑失败。

覆盖：

- Settings 默认值验证、缺失文件、保存/读取往返、未知字段、非法层数。
- Ingress 关闭拒绝、打开接受、同 actor/tid 连续两次 orgasm、序号递增、每次接受只唤醒一次、容量溢出、参数保留、场景开始/结束参数、关闭后拒绝。

### 7.2 ESP/VMAD/PEX 检查

```powershell
py tools/clean_esp.py --verify package/Data/SLACS_2_Plus_SCO.esp
```

结果：`verified package\Data\SLACS_2_Plus_SCO.esp`，输出保存在 `docs/implementation/esp-verify.log`。该脚本只核对 TES4、33 个 SexLab MGEF 前缀、两个移除 QUST、最小 VMAD、移除 FLST、保留 Mega FLST；26/26/1/26/1 计数和引用完整性是另行独立解析的结果，不是 `--verify` 的覆盖范围。构建内 `tools/package.py` 还会检查：

- PEX magic `fa57c0de`。
- 必需字符串：`SLACS_Main`、三个 Register/Event/Native 名称、三个 P+ 事件名。
- 禁止旧脚本字符串：`MaxLayersPerPart`、`ProcessComplexOrgasm`、`SLACS_AffectedActors`、`StartSlotBody`。
- 生成元数据精确 1170。注意：`tools/package.py` 检查的是生成的 `commonlibsse-ng-plugin.cpp` 文本，不解析 DLL；DLL 内嵌的 `SKSEPlugin_Version` 需要另用 `llvm-readobj`/PE 工具核对。本次已对 DLL 做过独立核对。

### 7.3 未覆盖的自动检查

- 没有 RE/VM 环境下的 QuestBridge 集成测试；只能通过 1170 游戏日志验证 `RegisterEvents` 是否实际执行。
- 没有模拟错误 runtime 启动的测试；代码路径和生成元数据已核对，但没有第二个游戏运行时可用于实测。

## 8. 1170 游戏实测记录

### 环境

- `E:/st/steamapps/common/Skyrim Special Edition`，SkyrimSE.exe 1.6.1170.0。
- SKSE 2.2.6，`skse64_1_6_1170.dll`。
- 临时复制包内 ESP/PEX/DLL 到物理 `Data`；复制 RaceMenu `skee64.dll` 和完整 Menu Framework `SKSE/Plugins` 资源，避免框架因缺资源 fatal。
- 测试前备份：`C:/Users/15219/AppData/Local/Temp/opencode/slacs2ng-game-backup/`。

### 结果

`skse64_loader.log`：

```text
productVersionStr = 1.6.1170.0
version = 0001000604920000
dll = ...\skse64_1_6_1170.dll
```

`SLACS2 NG.log`（时间 17:13:23，PID 40172）：

```text
SLACS2 NG v0-0-0-0
SLACS2 NG loading for Skyrim 1.6.1170
SLACS2 NG systems installed
```

这证明插件代码执行到了 `Runtime::install()`；`skse64_loader.log` 证明启动的是 1.6.1170.0 和 `skse64_1_6_1170.dll`。日志没有出现 `Runtime initialized`、`Session ready` 或 `Quest events subscribed`，因此 DataLoaded 未到达是**间接推断**，不是已保存的直接证据。

测试当次的 `skse64.log` 在恢复物理游戏时被旧的 `skse64.log.before` 覆盖，当前文件不再包含本插件的加载行；报告不再引用 `handle 10`、`sending message type 0 to plugin 7` 或“没有 dispatch 8”作为可复核事实。当前保留的证据只有 loader 日志、`SLACS2 NG.log` 和备份文件哈希。

物理游戏在本次测试中进程结束，没有 CrashLogger dump；退出原因未确认。该环境加载了大量其他插件，且 `SexLabUtil.dll` 为 2.16.0.0、`SexLab.esm` 为 347,818 字节，不是目标 P+ 2.17NG（`SexLab.esm` 380,652 字节，SHA-256 `1a599f...ad5d4`）。即使到达 DataLoaded，`Runtime::checkDependencies` 也会把 P+ 标为 `VersionMismatch`，`activateSession` 不会开放 ingress；因此该环境不能用于 S2 验收。

### 状态

- 1170 插件加载/安装：**部分通过（仅 Load/Install）**；没有保留当次 SKSE 插件扫描日志，不能把 `skse64.log` 的加载行列为证据。
- DataLoaded 以后行为：**待执行**，需要干净 1170 配置。
- 退出/重载 epoch 行为：**代码路径完成，游戏内待执行**。
- 临时物理游戏文件：**关键文件已恢复**（ESP 39,917 字节、PEX 34,995 字节、SexLabUtil.dll 的 SHA-256 与备份一致；Data 下无临时 `SLACS2 NG.dll`、`skee64.dll`、`SKSEMenuFramework.dll`）。备份目录只有 4 个文件，Menu Framework 复制资源没有逐项清单，因此不能证明每一个临时资源都已完整还原。

## 9. 验收项对照

| 验收项 | 状态 | 证据 / 条件 |
| --- | --- | --- |
| 1170 加载 | 部分通过 | `skse64_loader.log` 为 1.6.1170.0；`SLACS2 NG.log` 有三行安装日志；测试当次 `skse64.log` 已丢失，DataLoaded 未到达 |
| 其他 runtime 在重定位前拒绝 | 代码完成，未实测 | `main.cpp` 在 `SKSE::Init` 前比较；metadata 精确 1170 |
| 通知入队与消费 | 入队通过；Runtime 消费未自动验证 | `s1_checks` 仅覆盖 `Ingress::post/pop`、参数、序号、容量；`Runtime::processNativeOrgasm` 只写日志 |
| 加载/退出入口与 epoch 切换 | 代码完成，游戏内待测 | `beginSession` 只关闭入口/递增 epoch，清理和激活在 pump 的 `applySessionTransition/activateSession` |
| orgasm/start/end 参数与次数 | 入队层通过，Runtime 游戏内待测 | 三个 native、SceneNotice、计数日志 |
| 同 actor/tid 连续合法 orgasm | 入队层通过 | 不按 actor/tid 去重；两次连续测试均接受 |
| Quest/VMAD/PEX 挂载匹配 | ESP 子集+VMAD+PEX 字符串静态通过 | `clean_esp.py --verify` 不检查 26/26/1/26/1 计数、SPEL/EFSH 内容、masters、引用完整性；这些计数是独立解析结果 |

### 9.1 对抗审核与修正

按要求启动了三路只读对抗审核，结论分别如下：

- 需求/交付审核：无干净 1170 DataLoaded 实测；生命周期/VM 访问原本在消息线程；AddTask 的执行线程/阶段未验证；Quest 订阅成功只代表 VM 接受派发；物理环境 P+ 2.16 与目标 2.17 不符。
- 并发/内存/算法审核：未发现可证明的 CRITICAL 内存破坏；发现异常跨 Papyrus/SKSE/task 边界、预算不完整、会话状态线程亲和性未证明、已排 task 捕获裸 `this` 等问题。
- 报告/产物核验：确认 ESP/PEX/DLL/元数据/哈希和大部分静态结论；同时指出 `maxDeadlines` 是未实现声明、测试当次 `skse64.log` 已被覆盖、报告与 17:22 的 DLL 快照不一致、P+ 2.16 必不过依赖门。本文已按这些结论修改。

已修正：

- `beginSession` 只关闭入口、递增 epoch、清除 Quest 缓存并登记转换；清理、读取提交和激活移入 `pump` 的 `pumpMutex_` 临界区。
- `kPostLoadGame` 只在成功时请求提交；`NoStaging` 不再开放入口。
- Papyrus native、消息、序列化回调和 task 边界加异常保护；`taskPending_` 用 RAII 复位。
- Settings 增加同 domain slot 重叠检查、阈值上限和 classic locale/close 检查。
- ActorRegistry 保存 `ActorHandle`；Scene tid 有上限和重复 Start/未知 End 处理。
- `SaveSnapshot` 带 epoch，`saveTo` 拒绝旧 epoch 快照。
- 1170 日志段落改为只引用可复核的 loader/插件日志；不再引用已覆盖的 `skse64.log` 行。
- 构建、S1 检查、ESP verify 输出归档到 `docs/implementation/`。

仍未修正（明确保留）：

- `maxDeadlines` 尚未实现，属于 S6 共享 Scheduler。
- Quest 订阅成功仍以 `DispatchMethodCall2` 返回 true 为准；没有 Papyrus 回调确认。
- 已排 SKSE task 捕获 `BackgroundPump` 裸 `this`；插件不做运行时卸载，未实现跨卸载的共享状态。
- `ActorState.revision` 读档恢复、deadline/visual 的 revision 校验属于 S5/S4。
- FormID remap 失败只跳过并计数，不阻断 load commit。
- 设置写盘仍在菜单命令路径内同步执行。
- 需求审核的核心阻断未消除：没有干净 1170 + P+ 2.17NG 的 DataLoaded/事件实测。

## 10. 剩余事项

1. 在干净 1170 配置中实测：新游戏、成功读档、失败读档、回主菜单/重载、`beginSession` epoch 切换。
2. 验证 Quest 实际执行 `RegisterEvents`，观察三项 P+ 事件；用同一 actor/tid 连续触发 orgasm，确认每次独立到达。
3. S3：实现 `PPlusAdapter`，从 `Runtime::processNativeOrgasm` 发起查询，完成结果走 `Runtime::processPPlusCompleted`，再生成 Core 加层操作。
4. S4：实现 `Core` 和 `BodyVaginal` 的 OverlayAdapter 投影；当前 Runtime 只记录通知，没有层状态。
5. S5：完善 `Serialization` 的正式保存格式和 load staging 恢复；当前 schema 1 是可用占位。
6. S6：把 Quest 重试、自然衰减、雨/游泳采样并入共享 Scheduler；当前重试是 Runtime 内的有界泵路径。
7. S8：按 3.10 导出面接菜单；不要直接使用 3.14 源码头文件的新 API。
8. 在 `docs/implementation/DEPENDENCIES.md` 记录的版本组合下重跑游戏验收，并把结果补回本文。

## 11. 下一次入口

从通知消费点继续：

- 起点：`src/runtime/Runtime.cpp` 的 `Runtime::processNativeOrgasm`。
- 下一步模块：`src/integration/PPlusAdapter.h/.cpp`（尚未创建）。
- 第一步业务投影：`BodyVaginal` 的 Core 状态和 RaceMenu Overlay 适配器。
- 现有完成结果入口：`Runtime::processPPlusCompleted`；查询失败、取消、超时和旧 epoch 都从这里终结。
