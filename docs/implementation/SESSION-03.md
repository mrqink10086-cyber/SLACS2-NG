# SESSION-03：S5 / S6A 实现与验证记录

更新：2026-11-06。工作目录：`E:/source/repos/SLACS2 NG`。本次实现 S5（版本化持久化、保存快照、load staging 与加载生命周期）和 S6A（共享 Scheduler、`Core::Advance`、日历自然衰减、120 活动秒整体清除），接通 Runtime 调用链，并补齐持久化 codec、Scheduler/Core 自动检查。随后按 P+ 2.18.1 基线完成了内存/线程/生命周期/需求偏移审核与修复。S6B 的真实环境采样和 S7 的七区域/附加效果不在本次范围内。

## 1. 本次结论

已完成：

- **持久化记录**：`'SLNG'` 记录版本 1、payload schema 1；FormID + 业务值（有序层、纹理、变体、衰减余量、最后结算小时、整体清除剩余秒），不含 ActorId、原生句柄、绝对 deadline 或 steady 时间。
- **独立 codec**：`SerializationCodec` 负责编码、解码、长度/数量/枚举/浮点校验和 FormID remap；`Serialization` 只保留 SKSE 记录读写包装，使持久化逻辑可以脱离 SKSE 做自动检查。
- **保存快照**：Runtime 在完整 pump 后构造 `SaveSnapshot`，用 `std::atomic<std::shared_ptr<const SaveSnapshot>>` 发布；SKSE save callback 原子读取该只读快照并同步写入。
- **load staging**：load callback 只解码并 `ResolveFormID`，把拥有的值交给 Runtime；游戏执行阶段由 pump 提交：绑定新 ActorId、恢复层/纹理/余量/最后结算时间/整体清除剩余秒、`Core::replaceState`、重建 deadline 与外观目标，最后激活并重建 Quest 订阅。
- **加载生命周期**：PreLoad 关闭入口、切换 epoch、取消旧 epoch 的 P+ 请求和 deadline、清空 deadline 索引并丢弃旧 staging；旧 ingress 项被屏障淘汰；PostLoad 成功提交，读档记录非法时以空状态开放入口并记录诊断，PostLoad 回调报告失败时仍停留关闭状态；新游戏和 revert 走同一入口关闭/epoch 切换/清理路径；`Runtime::shutdown` 提供同一清理路径，但 SKSE 没有插件 shutdown 消息，当前入口未自动调用。`loadExpected_` 拒绝新游戏或非 PreLoad 状态下到达的 load staging。
- **Scheduler**：单一共享期限容器，稳定 `DeadlineId`，calendar/active 两个最小堆 + generation 懒删除，支持更新、取消、`cancelEpoch`、按数量/耗时预算取出；Quest 就绪重试的 steady 项也进入同一容器，不再单独占用一条唤醒路径。
- **`Core::Advance`**：以日历差闭式结算自然衰减，默认一游戏小时一层，LIFO 删除，空区域余量归零；已观测雨小时作为额外自然衰减输入；游泳立即清除；120 活动秒整体清除以 `timedClearAt` 表示，加层成功重置该 actor 的期限，其他状态变化保留原截止时间。
- **Runtime 接线**：真实时间心跳、暂停/菜单期间的活动秒冻结、旅行/等待后的日历跳跃结算、`DeadlineElapsed → Runtime 校验 → Core → ChangeSet → adapters`、OverlayRetry、Quest 重试和集中到期预算均已接入。
- **验证**：完整插件构建、DLL/PEX/ESP 打包、S1/S4/S5-S6/S5 持久化四组自动检查通过。

尚未完成：

- **1170 游戏内验收未执行**。当前物理环境仍是 P+ 2.16.0.0、`SexLab.esm` 347,818 字节，与参考基线 P+ 2.18.1（`SexLab.esm` 381,243 字节）不同。Runtime 只要求 P+ DLL 与 ESM 已加载，不锁定精确版本；但在 2.16 上的 typed ModEvent 兼容性未验证，因此不能作为 2.18.1 验收依据。构建和自动检查不能替代游戏内保存/恢复、跨加载通知、暂停活动和视觉恢复的实测。
- **S6B 环境采样未实现**：本 session 已保留 `EnvironmentSample` 入口和 `EnvironmentObserved → Runtime → Core::Advance` 链路，但没有真实天气/室内/游泳采样器，因此真实雨水加速和游泳立即清除只有 Core 单元测试，没有游戏采样验收。
- **S7 未实现**：其余六个区域仍只有 Core 数据模型和 ChangeSet 目标，没有完整 RaceMenu profile/纹理/附加效果。
- **S8 未实现**：Clear/Refresh/TestAdd/TestClear/Settings 已有 Runtime 命令处理，但没有中文 SKSE Menu Framework 外部入口。

## 2. S5 实现状态

### 2.1 保存

| 内容 | 状态 | 位置 |
| --- | --- | --- |
| 记录类型、记录版本、payload schema | 完成 | `src/persistence/Serialization.cpp`、`SerializationCodec.cpp` |
| 保存字段：FormID、七区域层、纹理/变体、余量、最后结算、清除剩余秒 | 完成 | `SerializationCodec::encode`、`Serialization::makeSnapshot` |
| 长度、数量、枚举、浮点校验 | 完成 | `SerializationCodec::encode/decode` |
| 原子只读快照 | 完成 | `Runtime::publishSaveSnapshot`、`Runtime::saveTo` |
| save callback 同步读取快照 | 完成 | `Runtime::onSave` |
| 快照只在完整 pump 发布 | 完成 | `Runtime::pump` |

记录布局：

```text
record type 'SLNG', record version 1
payload schema 1
capturedAt.calendarHours: double
capturedAt.activeSeconds: double
actorCount: uint32
for each actor:
    formId: uint32
    for 7 regions:
        layerCount: uint32
        for each layer (oldest -> newest):
            id: uint64
            textureIndex: uint16
            variant: uint8
        decayRemainderGameHours: double
        revision: uint64
    lastSettledGameHours: double
    hasTimedClear: uint8 (0/1)
    timedClearRemainingSeconds: double (仅 hasTimedClear=1)
```

约束：

- `kSchemaVersion=1`、`kMaxActors=100000`、`kMaxLayers=5`、`kMaxRecordBytes=64 MiB`、`kMinActorBytes=161`。
- `makeSnapshot` 跳过 FormID 0 和既没有层也没有整体清除期限的 actor；每区域最多保留 5 层；余量、最后结算和剩余秒做有限值/范围收敛；整体清除剩余秒为 `max(0, timedClearAt - now.activeSeconds)`。
- `encode` 拒绝重复 FormID、非法 schema、非有限时间、非法纹理/变体、负整体清除秒、非有限结算时间；`save` 再把 codec 结果写入 SKSE 记录。
- Runtime 只在 `!report.deferred` 的完整 pump 后发布新快照；如果本轮因命令/期限预算延后，旧快照保持可读，save callback 读取到的是最近一次完整结算状态。
- save callback 不等待 pump；它只读取 `atomic<shared_ptr<const SaveSnapshot>>`，检查 epoch 后同步写记录。快照为空或 epoch 不匹配时写空快照，不把旧会话数据带入新档。

### 2.2 加载与 staging

| 内容 | 状态 | 位置 |
| --- | --- | --- |
| load callback 解码 + FormID remap | 完成 | `Runtime::onLoad`、`Serialization::load`、`SerializationCodec::decode` |
| 拥有值放入 staging，不保留 SKSE 接口/游标 | 完成 | `Runtime::onLoadStaging` |
| staging epoch 标记 | 完成 | `Runtime::stagingEpoch_` |
| 游戏执行阶段提交 | 完成 | `Runtime::commitLoadStaging` |
| 绑定新 ActorId、恢复业务状态 | 完成 | `ActorRegistry::bind`、`Core::replaceState` |
| 重建 deadline、外观目标、Quest 订阅 | 完成 | `Core::replaceState`、`Runtime::applyChangeSet`、`activateSession` |
| 失败加载丢弃 staging、保持入口关闭 | 完成 | `Runtime::commitLoadStaging`、`Runtime::handleLoadFailure` |
| 存档没有本插件 co-save 记录 | 按空档提交 | `commitLoadStaging` 的 NoStaging 分支 |
| 非 PreLoad 的迟到 staging 拒绝 | 完成 | `Runtime::loadExpected_` |

加载流程：

1. **PreLoad**：`Runtime::beginSession(SessionReason::PreLoadGame)` 先关闭 ingress，记录旧 epoch 并递增 epoch，再取消旧 epoch 的 P+ 查询和 Scheduler 期限、清空 `deadlineIds_`、丢弃旧 staging；随后 Quest 失效、staging/ready 标志清零并安排 pump。
2. **load callback**：`Serialization::load` 读取记录并调用 codec。codec 解码后通过回调对每个 FormID 调用 `ResolveFormID`；remap 失败、FormID 0 或重复目标产生 `PersistenceError::RemapFailed` 并从 actor 列表跳过。`onLoadStaging` 只在 `loadExpected_` 为真时接收，并把拥有的 `LoadResult` 放入 `loadStaging_`，标记 `stagingEpoch_ = epoch()`。
3. **过渡 pump**：`applySessionTransition` 清空 ingress、ActorRegistry、Core、场景/tid、dirty/3D/容量缓存和 Overlay 会话；**不丢弃本次 PreLoad 已收到的 staging**，因为 load callback 可能先于过渡 pump 到达。
4. **PostLoad 成功**：`requestLoadCommit` 设置提交标志。下一次 pump 在 `commitLoadStaging` 中检查 `stagingEpoch_ == epoch()`，然后：
   - 对每个 persisted actor 用 `ResolveFormID` 后的 FormID 重新 `ActorRegistry::bind`，不可绑定者计入 `skipped`；
   - 拒绝 FormID 0、重复 FormID、`layer.id == uint64 max` 等无效记录；
   - 写入 `ActorState`，`lastSettledGameHours` 原样恢复，`timedClearAt = now.activeSeconds + max(0, timedClearRemainingSeconds)`；
   - 重置旧状态后 `Core::replaceState`，由 Core 生成七区域完整目标和 deadline intent；`applyChangeSet` 投影外观并建立新 Scheduler 期限；
   - `activateSession` 打开 ingress、发布快照并安排 Quest 订阅重试。
5. **PostLoad 失败**：`handleLoadFailure` 关闭入口、清空 staging/Scheduler/deadline 索引、保持 `sessionReady_ = false`，随后只做清理过渡，不恢复旧回调或旧状态。
6. **Empty / NoStaging / Invalid**：没有 `'SLNG'` 记录、SKSE 没有触发本插件 load callback，或记录解码/epoch 校验失败时，按空档提交，清空状态并打开入口；不会把 session 永久留在关闭状态。PostLoad 消息本身报告失败时仍走 `handleLoadFailure`，保持入口关闭。

记录中的 actor revision 不保存；区域 revision 按记录恢复，actor revision 从 0 重建。`Core::replaceState` 生成的 deadline 使用恢复后的区域 revision，TimedClear 使用重建后的 actor revision，因此不会把旧会话的 owner revision 带入新档。

### 2.3 入口、屏障和 epoch

Runtime 的会话切换顺序为：**关闭 ingress → epoch 切换 → 取消旧请求/期限 → 清理缓存 → 提交 staging → 打开 ingress**。实现上 `beginSession` 先关闭入口并递增 epoch，再用旧 epoch 取消 P+ 和 Scheduler；`applySessionTransition` 清空 ingress 队列。这样：

- 屏障前排队的旧入口命令不会进入新会话。
- 旧 epoch 的 P+ 完成值在 `processPPlusCompleted` 的 epoch 检查处被丢弃。
- 旧 epoch 的 deadline 在 `cancelEpoch` 后失效，即使堆中残留也因 generation/取消而不返回。
- 屏障后到达的 Native 通知按当前入口状态和当前 epoch 处理：入口未开放时 `Ingress::post` 拒绝；入口开放后使用 Papyrus native 调用时的 `runtime.epoch()`，若 tid 不在当前场景表或 FormID 无效则产生拒收诊断，不会复用旧场景状态。Papyrus 事件本身不携带来源 session token，旧脚本栈在激活后迟到时会被盖上当前 epoch；因此跨加载旧通知仍必须按第 8 节做 1170 时序实测。
- 跨加载旧 Papyrus 通知单独列为 1170 实测项，记录到达阶段（PreLoad/load callback/PostLoad/激活后）、epoch、tid 和最终处理结果。

## 3. S6A 实现状态

### 3.1 Scheduler

| 内容 | 状态 | 位置 |
| --- | --- | --- |
| 单一共享期限容器 | 完成 | `src/runtime/Scheduler.h/.cpp` |
| 稳定 DeadlineId | 完成 | Runtime `deadlineIds_`，键 `(actor, kind, optional<Region>)` |
| calendar/active deadline | 完成 | Scheduler 两个最小堆 |
| 更新/取消/cancelEpoch | 完成 | `Scheduler::upsert/cancel/cancelEpoch` |
| generation 懒删除 | 完成 | `Entry::generation` + `DropStale` |
| 数量/时间预算 | 完成 | `Runtime::processDueWork`、`WorkBudget` |
| Quest 就绪重试并入同一容器 | 完成 | `Scheduler::upsertSteady/takeDueSteady` |
| owner revision 校验 | 完成 | `processDeadlineElapsed` 核验 epoch/actor/region revision |

约定：

- `NaturalDecay` 使用 `ClockDomain::CalendarGameHours`，owner 是 region；`TimedClear`、`SwimSample`、`OverlayRetry` 使用 `ClockDomain::ActiveSeconds`。`OverlayRetry` 由 Runtime 在适配器 dirty 时安排，不由 Core 安排。
- `DeadlineIntent` 仍不带 epoch/id；Runtime 按 `(actor, kind, region)` 分配稳定 id，补入当前 `epoch()`、clock、due 和 ownerRevision 后写入 Scheduler。
- 更新同一 id 时旧 generation 失效，旧堆项不会触发业务；`cancel` 删除当前项，旧堆项由 `DropStale` 跳过。
- `takeDue` 每轮最多取 `maxDeadlines` 个；`processDueWork` 同时受 wall budget 限制，剩余项设置 `report.deferred`，由下一次独立节拍继续，不阻塞入口命令或 P+ 轮询。
- Quest 订阅重试使用同一个容器中的 steady 项；成功订阅后取消该项。

### 3.2 Core::Advance 与时间结算

| 内容 | 状态 | 位置 |
| --- | --- | --- |
| 自然衰减闭式结算 | 完成 | `Core::SettleActor` |
| 默认一游戏小时一层、LIFO | 完成 | `SettleActor`、`ClearRegionLayers` |
| 空区域余量归零 | 完成 | `SettleActor`、`Sanitize` |
| 已观测雨小时额外输入 | 完成（Core 路径） | `EnvironmentSample::observedRainHours` |
| 游泳立即清除 | 完成（Core 路径） | `SettleActor` |
| 120 活动秒整体清除 | 完成 | `SettleActor`、`EmitDeadlines` |
| 加层/清区域/改配置前按旧参数结算 | 完成 | `ApplyAddLayers`、`ApplyClearRegions`、`ApplySettingsChanged` |
| deadline intent 生成 | 完成 | `EmitDeadlines` |

规则：

- 自然衰减：`elapsed = max(0, now.calendarHours - lastSettledGameHours)`；每区域 `total = decayRemainder + elapsed + observedRainHours`（雨只在外部采样确认的暴露时段计入，不从当前天气反推过去）；`steps = floor(total / interval + 1e-9)`；从最新层开始删除 `min(steps, layers)` 层；剩余余量为 `total - removed * interval`，区域清空时归零；`lastSettledGameHours = now.calendarHours`。结算完成后产生新 NaturalDecay deadline，due 为 `now.calendarHours + max(0, interval - remainder)`。
- 自然衰减关闭：不累计日历时间，所有区域的部分余量清零，避免关闭期间银行化进度在重新启用后一次性扣层；空区域余量保持 0。
- 游泳：匹配当前 actor 的样本且 `swimming && swimCleaning == LegacyImmediateClear` 时清除所有区域；区域层清空、余量归零并递增 revision。
- 整体清除：`timedClearAt` 以活动秒表示。启用时，成功加层把该 actor 的 `timedClearAt = now.activeSeconds + TimedClearSeconds(settings)`（非法/缺失值回退 120 秒）；清区域、自然衰减和其他状态变化**保留**原截止时间；从关闭改为启用且已有层时，以变更时刻重新计时。达到或超过 `timedClearAt` 时清除所有区域；关闭该功能时清除 `timedClearAt` 并取消对应 deadline。
- 活动秒来自 `RE::BSTimer::runTimeMS / 1000`。`BSTimer::Update` 在 `pauseCount != 0` 时 no-op，因此暂停/菜单期间活动秒不增长；steady 时间只用于 P+ 超时、后台唤醒和预算测量，不写入保存。
- `AddLayers` 在容量检查和实际加层前先按旧状态结算目标 actor；`SettingsChanged` 在替换配置前由 Runtime 对所有 actor 用旧设置结算；`ClearRegions` 也先结算再清除。这样长时间跳跃、改变结算频率和修改衰减间隔不会产生与逐步调用不同的层数/余量。
- `Core::replaceState` 先 `Sanitize`，再生成七区域完整目标和每个 actor 的 deadline intent；读档重建与普通运行使用同一套 deadline 语义。

### 3.3 Runtime 接线

| 内容 | 状态 | 位置 |
| --- | --- | --- |
| 真实时间心跳 | 完成 | `Runtime::pumpNow` 的 1 秒 heartbeat |
| 暂停/菜单活动秒 | 完成（BSTimer） | `Runtime::currentGameTime` |
| 旅行/等待后的日历跳跃 | 完成 | 下一次 pump 的 `Advance` |
| DeadlineElapsed 校验与处理 | 完成 | `Runtime::processDeadlineElapsed` |
| ChangeSet → Overlay/目标投影 | 完成 | `Runtime::applyChangeSet` |
| OverlayRetry | 完成 | `scheduleOverlayRetry/cancelOverlayRetry` |
| 配置变更旧状态结算 | 完成 | `processMenuAction` 的 `settleAllWithOldSettings` |
| 关闭/加载时取消期限 | 完成 | `beginSession`、`handleLoadFailure`、`shutdown`（入口未自动调用 shutdown） |

pump 顺序：

```text
生命周期过渡 / load commit / ready
→ P+ poll
→ ingress 命令（数量 + wall budget）
→ Scheduler 业务期限 + Quest steady 重试（数量 + wall budget）
→ dirty 外观重试
→ 完整未延后时发布 SaveSnapshot
```

- `pumpNow` 在有入口积压或 `report.deferred` 时安排 25 ms 唤醒；Quest 重试 250 ms；dirty 外观 500 ms；P+ pending 50 ms；会话就绪时每秒 heartbeat，保证没有新命令时 calendar/active 时间仍被结算。
- `processDeadlineElapsed` 先核对 epoch，再按 kind 核对 actor/region revision 和业务条件；自然衰减必须匹配 region revision，整体清除必须匹配 actor revision 且 `timedClearAt` 仍存在；过期项计入 `rejectedStale` 并产生 `StaleRevision` 诊断，不执行 Core 变更。
- 集中到期时，`processDueWork` 每次只取一个业务期限并按 `maxDeadlines`/wall budget 循环；未处理余量设置 `deferred`，不会在一个 pump 中无限处理，也不会影响入口命令的既有预算。
- 外观 dirty 时安排 `OverlayRetry`（活动秒 +2 秒）；成功后取消该项；失败重新安排，避免 3D/容量恢复后没有下一次机会。

## 4. 修改入口

| 文件 | 本次修改 |
| --- | --- |
| `src/core/Types.h` | 新增 `DeadlineElapsed`、`DeadlineUpdateStatus`；保留既有公开类型/单位/所有权 |
| `src/core/Core.cpp`、`src/core/Core.h` | 实现 `Advance` 分支、闭式自然衰减、游泳清除、整体清除、deadline intent、加层/清区域/配置变更前结算 |
| `src/runtime/Scheduler.h/.cpp` | 新增共享期限容器、稳定 id/generation、calendar/active 两域、steady Quest 重试 |
| `src/runtime/Runtime.h/.cpp` | 接入 save snapshot、load staging/commit/failure、epoch 屏障、pump 期限处理、Environment/Deadline 命令、OverlayRetry、Quest 重试和心跳 |
| `src/persistence/Serialization.h/.cpp` | 版本化 SKSE 记录读写；改为调用独立 codec |
| `src/persistence/SerializationCodec.h/.cpp` | 新增纯编解码、校验、FormID remap、`makeSnapshot` |
| `src/runtime/Ingress.h` | 复用 `Types.h` 的 `DeadlineElapsed`，不重复定义 |
| `tests/s5s6_checks.cpp`、`tests/s5_persistence_checks.cpp` | 新增 Scheduler/Core、持久化 codec 及审核回归检查 |
| `src/runtime/ActorRegistry.h/.cpp` | 增加 `remove(FormId)`、FormID 回收校验，避免旧 ActorHandle 与新 FormID 混用 |
| `src/integration/OverlayAdapter.h/.cpp` | 增加 `clearActor`，会话切换前清除已知 actor 的 live override；活动指针改为原子 |
| `src/integration/PPlusAdapter.cpp` | 终态投递失败时保留结果和预留槽，成功后才标记 posted/erase |
| `src/integration/QuestBridge.cpp` | 失效/重置前派发 `UnregisterEvents` |
| `xmake.lua` | 新增两个检查目标 |
| `package/Data/SKSE/Plugins/SLACS2 NG.dll`、`Scripts/SLACS_Main.pex` | 本次构建产物（无新增资源） |

未修改 `docs/INTERFACES.zh-CN.md` 的公开名称、签名、单位和所有权；新增 codec 是内部实现，不改变 S5 对外 `Serialization` 接口。

## 5. 公开接口

以下签名与 `docs/INTERFACES.zh-CN.md` 一致，本次实际使用：

```cpp
namespace slacs
{
    // S5
    SaveSnapshot Serialization::makeSnapshot(const CoreState& state, const GameTime& now);
    PersistenceStatus Serialization::save(SKSE::SerializationInterface& skse,
                                          const SaveSnapshot& snapshot);
    LoadResult Serialization::load(SKSE::SerializationInterface& skse);

    void Runtime::beginSession(SessionReason reason);
    void Runtime::onLoadStaging(LoadResult result);
    LoadCommitResult Runtime::commitLoadStaging();
    void Runtime::onSessionReady();
    void Runtime::shutdown();
    void Runtime::publishSaveSnapshot();

    // S6A
    DeadlineUpdateStatus Scheduler::upsert(const Deadline& deadline);
    bool Scheduler::cancel(DeadlineId id);
    void Scheduler::cancelEpoch(SessionEpoch epoch);
    std::vector<DeadlineElapsed> Scheduler::takeDue(const GameTime& now,
                                                    std::size_t maxItems);

    ChangeSet Core::apply(CoreState& state, const CoreOp& op,
                          const Settings& settings, const GameTime& now);
    ChangeSet Core::replaceState(CoreState& state,
                                 std::vector<ActorState> reboundState,
                                 const Settings& settings, const GameTime& now);
}
```

内部扩展：

- `Scheduler::upsertSteady/cancelSteady/takeDueSteady` 只服务于 Runtime 的 Quest 重试，不改变公开 deadline 契约。
- `Runtime::onLoadStaging` 只接收 PreLoad 阶段的 staging；`loadExpected_` 是内部屏障标志。
- `SaveSnapshot` 由 Runtime 补 `epoch`；`makeSnapshot` 只构造业务内容。

## 6. 构建

环境：Windows x64、VS2022、XMake 3.0.9、CommonLibSSE-NG 6.0.0（commit `15ad90b31b2ca09cf477c7e1a43ff5ff05af120f`），生成运行时元数据 `1,6,1170,0`。

命令：

```powershell
xmake f -p windows -a x64 --toolchain=msvc -y
xmake build -y -r
```

结果（审核后最终完整日志：`docs/implementation/build-s3-audit-final.log`；审核前日志 `build-s3.log`、`build-s3-audit2.log`）：

- C++ 全量编译、链接成功；控制台无 error/warning（最终全量构建 10.547s）。
- `PapyrusCompiler.exe`：`0 error(s), 0 warning(s)`，`Assembly succeeded`。
- `package ready: E:\source\repos\SLACS2 NG\package\Data`。
- `build ok`。

产物：

| 产物 | 大小 | 位置 | SHA-256 |
| --- | --- | --- | --- |
| DLL（构建目录） | 1,975,808 字节 | `build/windows/x64/release/SLACS2 NG.dll` | `aee9570f41606832a0997219ae307eaa1f31da47d22bd6aff9a0a965363206e6` |
| DLL（包内） | 1,975,808 字节 | `package/Data/SKSE/Plugins/SLACS2 NG.dll` | 同上（`cmp` 一致） |
| PEX | 1,198 字节 | `package/Data/Scripts/SLACS_Main.pex` | `fdc2b17ffa1a323abc81060acd196a79d33136e6a924a9a24a04ed05dae0ddc7` |
| ESP | 25,850 字节 | `package/Data/SLACS_2_Plus_SCO.esp` | `77477cfe385a67e3c7e8c487b60450cb94f549913aeca1523063f97d6669976e` |
| BodyVaginal DDS | 60 个，约 265 MB | `package/Data/Textures/slacs2/Vaginal/` | 未改动 |

`py tools/clean_esp.py --verify package/Data/SLACS_2_Plus_SCO.esp` 输出 `verified package\Data\SLACS_2_Plus_SCO.esp`（`docs/implementation/esp-verify-s3-audit.log`）。

## 7. 自动检查

环境：Windows x64 / VS2022 / XMake 3.0.9 / CommonLibSSE-NG 6.0.0。

命令与结果：

```powershell
xmake build -y slacs_s1_checks slacs_s4_checks slacs_s5s6_checks slacs_s5_persistence_checks
./build/windows/x64/release/slacs_s1_checks.exe
./build/windows/x64/release/slacs_s4_checks.exe
./build/windows/x64/release/slacs_s5s6_checks.exe
./build/windows/x64/release/slacs_s5_persistence_checks.exe
```

完整日志：`docs/implementation/checks-s3-audit-final.log`（审核前为 `checks-s3.log`）。四项均通过：

- `SLACS2 NG S1 checks passed`
- `SLACS2 NG S4 checks passed`
- `SLACS2 NG S5/S6 checks passed`
- `SLACS2 NG S5 persistence checks passed`

### 7.1 S5 持久化检查

`tests/s5_persistence_checks.cpp` 覆盖：

- 两个 actor 的完整往返：FormID remap、层顺序、纹理 index、Normal/Mega 变体、余量、区域 revision、最后结算小时、整体清除剩余秒。
- 失败 remap 丢弃记录并产生 `RemapFailed`；重复 FormID 丢弃后一个 actor；FormID 0 丢弃。
- 截断记录、超量层数、payload schema 不符、非法变体、NaN 余量、非法 `hasTimedClear` 标志全部拒绝。
- 编码阶段拒绝重复 FormID、schema 不符、NaN 时间、非法纹理/变体、负整体清除秒、非有限结算时间。
- `makeSnapshot` 过滤空 actor、保留只有整体清除期限的 actor、把 150 秒期限按当前活动 30 秒转换为 120 秒、7 层截到 5 层、负余量归零并保留 revision。
- 审核后新增：actor 按 FormID 排序（跨 unordered_map 稳定）；超大整体清除剩余秒截到 `1.0e9`；非法纹理池/重复层 ID 在 codec 与 `Core::replaceState` 两侧被拒绝或清理。

### 7.2 S6A 检查

`tests/s5s6_checks.cpp` 覆盖：

- Scheduler 稳定 id 更新/代际失效、取消、`cancelEpoch`、数量预算、非法 kind/clock/due、steady 队列。
- 五层 LIFO；自然衰减 1 小时到期；1 小时逐步结算与 4 小时一次跳跃的层数/余量/deadline 一致；长跳跃清空后余量 0、deadline Cancel。
- 空区域不积累余量；已观测雨小时只按暴露增量加入；游泳立即清除。
- 整体清除在 119/120 活动秒的边界、成功加层重置期限、手动清区域保留原期限、关闭功能取消期限。
- 新增前结算（层 id 1/2/3/4/6、新区域 id 7、timer 180）、配置变更前按旧参数结算、自然衰减关闭。
- 审核回归：日历回退不重复结算；极小 interval/巨大累计量不溢出且安全清空；已有层时启用整体清除从当前时刻计时并生成 deadline；`replaceState` 清理非法/重复层并递增 revision；Scheduler `updateRevision` 保持 due 且拒绝旧 epoch。

### 7.3 未覆盖的自动检查

- 没有真实 SKSE `SerializationInterface` save/load 回调测试；codec 已覆盖记录内容，但 SKSE 回调、`ResolveFormID` 的实际 load order 和 save callback 时序需要 1170 实测。
- 没有 Runtime 会话切换的自动化测试；PreLoad/PostLoad/加载失败/旧通知需要 1170 或注入式集成环境。
- 没有 RaceMenu 视觉保存/恢复测试；只验证了 Core/目标/deadline 和持久化数据。
- 没有 Runtime / P+ / Quest / VM 的外部调用集成测试；线程安全结论来自代码审查和锁边界核对，不能替代 1170 实测。
- S6A 的自动检查覆盖普通时间与 Core 的时间规则；真实天气采样和游泳状态由 S6B 验收。

## 8. 1170 游戏实测

### 环境

- 游戏：Skyrim SE 1.6.1170.0，SKSE 2.2.6（`skse64_1_6_1170.dll`）。
- 参考基线：P+ 2.18.1（`SexLabUtil.dll` 2.18.1.0，`SexLab.esm` 381,243 字节）、RaceMenu 0.4.20、Menu Framework 3.10、`SLACS_2_Plus_SCO.esp`。P+ 参考目录只提供发布文件与 PSC，未提供 C++ 源码。
- 运行时依赖门只要求 `SexLabUtil.dll` 与 `SexLab.esm` 已加载，不比较 P+ 精确版本；实际版本写入依赖报告便于排查。其他版本必须自行确认 typed ModEvent 兼容性。
- 当前物理环境（SESSION-01/SESSION-02 记录）：P+ 2.16.0.0，`SexLab.esm` 347,818 字节。该环境可通过加载门，但与 2.18.1 验收基线不同，事件兼容性未验证。

### 执行状态

本次**没有启动 1170 做 S5/S6A 行为测试**。构建、codec 和 Scheduler/Core 自动检查已完成；以下游戏内项目待依赖替换后执行。不把旧 Load/Install 记录当作本轮行为通过。

| 项目 | 操作/命令 | 期望 | 当前结果 | 待执行条件 |
| --- | --- | --- | --- | --- |
| 依赖门 | 启动到 DataLoaded，查看 `SLACS2 NG.log` | initialized/ready，`SexLabUtil.dll` 与 `SexLab.esm` 均已加载；报告记录实际版本 | 未执行 | 验收建议使用参考基线 P+ 2.18.1 + 381,243 字节 `SexLab.esm` |
| 无 co-save 记录读档 | 删除本插件记录或读一个从未保存过本插件的档 | 按空档提交并开放入口，不永久关闭 | 未执行 | 1170 存档操作 |
| 已删除 actor | 删除/回收一个已记录 actor 的 FormID | 对应 ActorId、层、deadline、dirty 状态被移除 | 未执行 | 1170 控制台/剧本删除测试 |
| 覆盖层保存/恢复 | 加层→保存→退出→读档 | 七区域层数、顺序、纹理和变体恢复；BodyVaginal 视觉恢复 | 未执行 | 1170 + RaceMenu 槽位足够 |
| FormID remap | 在有其他 mod 的 load order 保存/读档 | 可解析 FormID 绑定新 ActorId；失效记录跳过并记录 | 未执行 | 使用可变 load order 的 1170 存档 |
| 连续换档 | A 档保存→加载 B→立即加载 A/新档 | 无旧 A 层/期限/请求泄漏到 B | 未执行 | 两个存档 + 日志时间戳 |
| 加载失败 | 删除/破坏记录或模拟读取失败 | 记录非法时以空状态开放并记录诊断；PostLoad 消息失败时保持关闭，旧状态不复活 | 未执行 | 需要可重复的失败注入 |
| 加载时请求完成 | 读档过渡期间让旧 P+ 查询完成 | 旧 epoch 完成值被丢弃，不写新状态 | 未执行 | 延迟/注入 P+ 完成 |
| 跨加载旧 Papyrus 通知 | 在读档前触发 orgasm，记录通知在 PreLoad/load callback/PostLoad/激活后的到达 | PreLoad 关闭期丢弃；激活后按当前 epoch 处理；无旧场景层 | 未执行 | 记录到达时刻、epoch、tid、结果 |
| 新游戏/revert/shutdown | 主菜单新游戏、返回主菜单、退出 | 入口关闭→epoch 切换→期限/请求取消→清理；shutdown 无残留唤醒 | 未执行 | 1170 主菜单/退出流程 |
| 暂停活动秒 | 120 秒 clear 倒计时中打开菜单/暂停 30 秒 | 活动秒不增长，恢复后继续剩余秒 | 未执行 | 同 1，观察日志/状态栏 |
| 旅行/等待结算 | 1 层、余量 0.5h，等待 3h/快速旅行 | 一次闭式结算，层数和余量与逐步结算一致 | 未执行 | 1170 等待/旅行 |
| 空区域 | 清空区域后等待数小时 | 余量保持 0，无未来衰减积累 | 未执行 | 同 1 |
| 新增前结算 | 余量 0.9h 时加层 | 先按旧状态结算，再按当前时间重置 clear 期限 | 未执行 | 同 1 |
| 配置变更 | 运行中把衰减间隔改为 2h | 先用旧间隔结算，再按新间隔生成期限 | 未执行 | 菜单命令或配置文件热载 |
| 期限更新/取消 | 加层、清区域、配置关闭 | 成功加层重置该 actor clear；清区域保留原 due；关闭取消 | 未执行 | 同 1 + 日志 |
| 集中到期预算 | 大量 actor 同时到期 | 单 pump 有界，余量下一节拍处理，入口不饥饿 | 未执行 | 需压测脚本/多人存档 |
| 视觉恢复 | 3D 重建/远处 actor/槽位不足后恢复 | dirty → OverlayRetry → 恢复，无重复层 | 未执行 | RaceMenu Body 槽位 ≥12 |

### 已确认的静态事实

- `BSTimer::runTimeMS` 只在 `BSTimer::Update` 运行时推进；`pauseCount != 0` 时 `Update` no-op，因此暂停/菜单期间活动秒停止。该结论来自 CommonLibSSE-NG 头文件与既有 S1/S4 静态核对。
- SKSE `kPostLoadGame` 的 `data` 是 `(void*)result` 编码的布尔值，不解引用；Runtime 按 `dataLen >= 1 && data != nullptr` 判定成功，失败走 `handleLoadFailure`。
- 保存记录不含 ActorId、`RE::ActorHandle`、`NiPointer`、绝对 deadline、steady 时间；读档只通过 FormID + 业务值重建。
- 当前环境无法满足 1170 行为验收；构建和自动检查通过不等于游戏内通过。

## 9. 验收项对照

| 验收项 | 实现状态 | 验证状态 |
| --- | --- | --- |
| 覆盖层保存/恢复 | 完成 | codec 往返通过；1170 视觉/读档待测 |
| FormID remap 和失效记录 | 完成 | codec 自动检查通过；真实 load order 待测 |
| 连续换档 | 完成 | 生命周期代码完成；1170 待测 |
| 加载失败 | 完成 | 记录非法按空档开放；PostLoad 消息失败保持关闭；1170 注入待测 |
| 无 co-save 记录 | 完成 | 空档提交路径；1170 待测 |
| FormID 回收/删除 | 完成 | ActorRegistry/onFormDelete 代码完成；1170 待测 |
| 加载时请求完成 | 完成 | epoch/P+ 检查完成；1170 延迟注入待测 |
| 跨加载旧 Papyrus 通知 | 完成代码级 | 1170 单独实测待执行 |
| 调用频率变化 | 完成 | S6 检查逐步 vs 跳跃一致 |
| 长时间跳跃结算 | 完成 | S6 检查通过 |
| 空区域 | 完成 | S6 检查通过 |
| 新增前结算 | 完成 | S6 检查通过 |
| 配置变更 | 完成 | S6 检查通过（Core 时间规则） |
| 暂停活动秒 | 完成代码级 | BSTimer 静态事实；1170 待测 |
| 期限更新/取消 | 完成 | Scheduler/Core 检查通过；1170 待测 |
| 集中到期预算 | 完成 | Scheduler 数量/耗时预算完成；1170 压测待执行 |
| S6A 调度与普通时间 | 完成 | 自动检查通过 |
| S6B 真实雨水/游泳 | 未实现采样器 | 下一 session |
| 构建/打包 | 完成 | 构建、PEX、ESP、四组检查通过 |

## 10. 剩余事项

1. **1170 验收**：替换依赖到 P+ 2.18.1 与 381,243 字节 `SexLab.esm` 后，逐项执行第 8 节；特别记录跨加载旧 Papyrus 通知的到达阶段和结果。
2. **S6B Environment**：新增 `Environment` 采样器，读取室内/天气/降雨/降雪/游泳状态；只把确认暴露的时段累加进 `observedRainHours`，不从当前天气反推缺失历史；读档/新游戏时 reset/invalidate；把采样值通过 `EnvironmentObserved` 或 `Core::Advance` 的 `EnvironmentSample` 交给 Core。
3. **S7**：为其余六区域补 RaceMenu profile、纹理池/槽位上限、附加效果和跨区域资源；当前只有 BodyVaginal 有可见 Overlay。
4. **S8**：把 Clear/Refresh/TestAdd/TestClear/Settings 接中文 SKSE Menu Framework；当前命令处理已存在，无外部入口。
5. **RaceMenu ini**：当前 Body/Face/Hands/Feet 槽位为 6/3/3/3、`bPlayerOnly=1`；BodyVaginal 7..12 会返回 `NoOwnedCapacity`。该配置由用户维护，本包不覆盖。
6. **已知代码限制**：
   - actor revision 不随记录保存，读档从 0 重建；区域 revision 和 deadline 语义已按恢复值重建。
   - 连续 PreLoad 回调无法由 SKSE 区分来源；`loadExpected_` 能拒绝非 PreLoad staging，但极端情况下前一次 load 的迟到 callback 仍可能与新 PreLoad 竞争，需在 1170 观察。
   - SKSE 没有插件 shutdown 消息，`Runtime::shutdown` 当前没有自动调用方；退出依赖进程结束，`BackgroundPump` 会检查 stopping 标志后再执行 tick。
   - save 快照只在未 deferred 的完整 pump 发布；保存时若正好有积压，使用上一次完整结算快照。
   - 当前没有真实环境采样，Core 的雨水/游泳路径只有单元测试。
   - Papyrus 通知不携带来源 session token；激活后到达的旧脚本栈事件会使用当前 epoch，必须依赖场景 tid/FormID 校验并做 1170 时序实测。
   - RaceMenu 自己的 co-save 可能在读档后恢复本插件的旧 override；本插件只清除已知 Core actor 的 live override，未知 actor 的残留需要在 1170 验证。

## 11. 审核修复（P+ 2.18.1 基线）

本次用独立子代理按“内存/UB、线程/生命周期、需求偏移”三条线审核实际源码，主代理逐条复核后修复：

| 问题 | 处理 | 位置 |
| --- | --- | --- |
| P+ 依赖门原先硬编码精确版本，2.18.1 之外的兼容安装会被拒绝 | 运行时只要求 `SexLabUtil.dll` + `SexLab.esm` 已加载，实际版本写入报告；2.18.1 仅作参考基线；文档同步 | `Runtime::checkDependencies`、DEPENDENCIES、README |
| 没有 co-save 记录时 `NoStaging` 会让入口永久关闭 | 缺失 staging 按空档 `Committed` 提交并发布空快照 | `Runtime::commitLoadStaging` |
| RaceMenu 握手失败仍可能开放入口 | `overlayReady_` 门；握手失败 `initialize` 返回 false | `Runtime::initialize/activateSession` |
| 会话切换后旧 live overlay 仍留在 RaceMenu | 关闭入口后、epoch 切换前清理已知 Core actor 的 override | `Runtime::beginSession`、`OverlayAdapter::clearActor` |
| 日历回退会重复结算；巨大/极小衰减比可能溢出 | 回退只把 elapsed 夹到 0 且不移动基线；先比较层数再转 `size_t` | `Core::SettleActor` |
| 关闭自然衰减后仍银行部分余量 | 关闭时清零所有区域余量并递增 revision | `Core::SettleActor` |
| 已有层时启用整体清除不重新计时 | `ArmTimedClear` 在启用且无期限时以当前活动秒计时 | `Core::ApplySettingsChanged/replaceState` |
| 非法/重复层、越界纹理、重复层 ID 可进入状态 | `NormalizeRegion` 与 codec 双侧清理/拒绝 | `Core::Sanitize`、`SerializationCodec` |
| `(actor<<3)|index` 去重键截断高 ActorId | 改为 `std::set<std::pair<ActorId,size_t>>` | `Core::ApplyAddLayers` |
| 无 co-save 时 load 记录缺失被当作 Empty，且只读第一条记录 | 扫描最多 64 条找 `'SLNG'`；读失败返回 `IoError` | `Serialization::load` |
| OverlayRetry 每 500 ms 被重新设 due，永不独立到期 | `Scheduler::updateRevision` 只更新 revision，不改 due | `Runtime::scheduleOverlayRetry`、`Scheduler` |
| actor FormID 回收后可能混用旧句柄 | resolve 校验句柄 FormID，不匹配回退 LookupByID；增加 remove | `ActorRegistry` |
| 删除 actor 后业务状态/期限残留 | 删除回调解析 Form/Handle，移除 ActorId、Core、deadline、dirty | `Runtime::onFormDelete/removeActorForm` |
| Quest 失效未 UnregisterEvents | 失效/重置前派发反注册 | `QuestBridge` |
| P+ 终态投递失败会丢结果 | 原子 posting/posted，成功后才 erase；同 epoch 饱和保留重试 | `PPlusAdapter::complete` |
| 外部调用持锁/生命周期 | 保留必要的 pump 锁串行化；BackgroundPump tick 检查 stopping；未能自动调用 shutdown 的限制已记录 | `Runtime`、`BackgroundPump` |
| 读档记录非法后入口永久关闭 | `Invalid` 后改为以空状态请求开放入口，不再吞掉 ready 请求 | `Runtime::pump` |
| 延迟 P+ 完成可复活已删除 actor | 投递前过滤已消失目标；全失效则丢弃该 completion | `Runtime::dispatchPPlusCompletion` |
| dirty 视觉重放可突破 pump 预算 | 增加 2 ms 局部墙钟预算，超时置 deferred 留待下一次 pump | `Runtime::processDirtyVisuals` |
| Overlay 失败被误当已解决 | 仅 `Applied/NoChange` 清除 dirty 并取消重试；失败保留并重新排期 | `Runtime::applyChangeSet/processDirtyVisuals` |
| 删除 actor 后 Overlay 缓存残留 | 新增按 ActorId 清理缓存的重载并在删除路径调用 | `OverlayAdapter::clearActor(ActorId)`、`Runtime::removeActorForm` |
| 极端 interval 下永不衰减 | 非有限步数按“删除全部层”处理，NaN 安全归零余量 | `Core::SettleActor` |
| `ApplySettingsChanged` 不诊断非法层上限 | 与 `ApplyAddLayers` 一致产生 `InvalidSettings` | `Core::ApplySettingsChanged` |

审核结论：S5/S6A 的既定范围已实现；剩余 S6B 环境采样、S7 七区域/效果、S8 菜单、RaceMenu 槽位配置和 1170 实测仍是未完成项，不在本次“已完成”声明内。

## 12. 下一次入口

下一次从 **Environment（S6B）** 继续：

- 实现 `Environment::sample(ActorId, RE::Actor&, const GameTime&)` 及 reset/invalidate，接入 `Runtime::pump` 的独立采样节拍。
- 复用 `EnvironmentSample` 的 `interior/raining/snowing/swimming/observedRainHours`；普通无样本 `Advance` 仍只结算日历/活动时间，有样本时叠加环境输入。
- 保留现有 `EnvironmentObserved → Runtime → Core::Advance → ChangeSet → adapters` 链路，不在采样器里改业务状态。
- S6B 验收通过后进入 S7：七区域 profile/纹理/效果和跨区域槽位；S8 再接中文菜单。

代码入口依次为：`src/integration/Environment.h/.cpp`（待建）、`src/runtime/Runtime.cpp` 的 `pump`/`processEnvironmentObserved`、`src/core/Core.cpp` 的 `SettleActor`。
