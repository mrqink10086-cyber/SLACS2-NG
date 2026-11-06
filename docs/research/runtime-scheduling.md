# SLACS2 NG：运行时、环境、调度与生命周期研究

日期：2026-11-05。范围：规划与源码研究；没有迁移插件代码、构建或运行游戏。面向新档，不承担旧版 Papyrus 存档迁移；新版自己的存取档与读档取消仍是必要设计。

CommonLibSSE-NG 研究基线为本地子模块 `15ad90b31b2ca09cf477c7e1a43ff5ff05af120f`。下面区分“源码事实”“设计建议”和“待实机确认”，避免把接口名称当成线程与生命周期保证。

## 建议先定的架构

核心采用 **事件输入 → 值类型命令 → 状态与 deadline → 合并后的表现更新**。第一版不需要用协程替代所有回调。回调负责接收游戏/SKSE/RaceMenu 事件；统一调度器负责时间与取消；协程以后可用于少量顺序式、多阶段流程，仍使用同一执行器与取消规则。

逻辑状态不保存引擎裸指针。游戏对象访问和外观修改集中于经 1.6.1170 验证的游戏任务执行阶段；纯算法可以离开引擎对象执行。`AddTask` 本身不能证明执行线程固定、下一帧运行或对象访问安全。

## 1. 运行时锁定与构建

### 源码事实

- CommonLib 提供 `SKSE::RUNTIME_SSE_1_6_1170`，加载参数可在初始化前读取 `RuntimeVersion()`。[版本常量](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/SKSE/Version.h:34>)、[加载接口](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/SKSE/Interfaces.h:27>)。
- 官方 SKSE `v2.2.6` 与 `v2.2.8` 的版本头均将发行 runtime 定义为 `RUNTIME_VERSION_1_6_1170`，对应发行字符串分别为 `2.2.6`、`2.2.8`。因此 1170 不应被写成只能搭配 2.2.6；具体 SKSE 版本应与目标安装及 RaceMenu/SexLab P+ 联调后固定。[官方 v2.2.6 版本头](https://github.com/ianpatt/skse64/blob/v2.2.6/skse64_common/skse_version.h)、[官方 v2.2.8 版本头](https://github.com/ianpatt/skse64/blob/v2.2.8/skse64_common/skse_version.h)。这两个 tag 分别为 `9398d04592a7eb9d754f2997701116df1022f1b4`、`b9bfa27de9fe927daade322c5209180176604495`；它们是本研究的历史基线，不表示当前最新版。
- xmake 有 `skyrim_se`、`skyrim_ae`、`skyrim_vr` 三个独立选项，默认均为 true；可从源码构建 AE-only。预编译库分支明确要求三者均为 true，防止头文件与库布局不同。[选项](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/xmake.lua:34>)、[预编译配置检查](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/xmake.lua:180>)。
- `PluginDeclaration::RuntimeCompatibility` 支持显式 runtime 列表；显式列表构造会设置 `_addressLibrary(false)`，版本无关声明是另一种构造方式。[声明语义](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/SKSE/Interfaces.h:621>)、[构造函数](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/SKSE/Interfaces.h:532>)。
- 当前 `commonlibsse-ng.plugin` xmake 规则默认生成 Address Library 版本无关声明，没有显式 runtime 列表配置；模板会生成 `SKSEPluginInfo` 导出。[规则](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/xmake.lua:454>)、[生成模板](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/res/commonlibsse-ng-plugin.cpp.in:3>)。

### 设计建议

1. 从源码以 `skyrim_se=false, skyrim_ae=true, skyrim_vr=false` 构建；插件与 CommonLib 使用一致宏，不把现成全 runtime 的静态库与 AE-only 头文件混合。
2. 加载入口首先检查 `a_skse->RuntimeVersion() == REL::Version{1, 6, 1170, 0}`，不匹配时返回失败；在 `SKSE::Init`、relocation、hook 安装及游戏单例访问之前完成。
3. 元数据只声明 1170；实现时调整生成声明的方式，避免手写与生成的 `SKSEPluginInfo` 重复导出。精确 runtime 列表不免除实际的 Address Library 依赖，安装依赖需独立声明。
4. AE-only 只限定结构族，不能取代精确 1170 检查。不要把 `RUNTIME_SSE_LATEST_AE` 之类常量名称作为版本锁。

## 2. 天气、室内、水中：能直接改进什么

| 需求 | 已核实接口与实际含义 | 规划结论 |
| --- | --- | --- |
| 是否正在下雨 | `RE::Sky::IsRaining()` 同时检查 current/last weather 的 rainy 标记和降水淡入/淡出阈值。[实现](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/RE/S/Sky.cpp:106>) | 优于只看 current weather 的 classification；处理天气切换时的降水状态。 |
| 下雪 | `RE::Sky::IsSnowing()` 使用相同的过渡判定。[实现](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/RE/S/Sky.cpp:112>) | 雪是否参与清洗属于玩法策略，不能因为有接口就自动启用。 |
| 室内 | actor 的 parent cell + `TESObjectCELL::IsInteriorCell()`；内部检查 `kIsInteriorCell`，另有独立 `kShowSky` 标记。[parent cell](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/T/TESObjectREFR.h:418>)、[室内判定](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/RE/T/TESObjectCELL.cpp:140>)、[cell flags](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/T/TESObjectCELL.h:128>) | 第一版使用明确的室内排除规则；“能见天空”与“室外”分别处理。 |
| 游泳 | `ActorState::IsSwimming()` 读取 swimming 状态。[实现](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/A/ActorState.h:202>) | 保留旧版游泳触发清洗的语义，必要时低频轮询已加载、受影响 actor。 |
| 浸入水中 | `TESObjectREFR::IsInWater()` 判断 `GetWaterHeight() > GetPositionZ()`。[实现](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/RE/T/TESObjectREFR.cpp:857>) | 与游泳不同，不能直接替换，否则涉水也可能被算成全身清洗。 |
| 时间跳变 | `Calendar` 提供 `GetDaysPassed/GetHoursPassed/GetTimescale`；`ScriptEventSourceHolder` 有 wait/sleep/fast travel 等来源。[Calendar](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/C/Calendar.h:64>)、[事件来源](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/S/ScriptEventSourceHolder.h:105>)、[fast travel 字段](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/T/TESFastTravelEndEvent.h:5>) | 使用日历差计算应到期量，事件用于及时唤醒与重采样，不逐小时补发回调。 |

`Sky` 是全局天气状态，`IsRaining()` 返回布尔值，不是雨量、屋檐遮蔽、actor 所处地点的降水强度。不能据此给异地、未加载 NPC 断言“在淋雨”。当前/上次天气与过渡比例只是全局快照。[Sky 数据成员](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/S/Sky.h:105>)。

旧版 `OnUpdate` 遍历已加载 actor，游泳时清除；`ProcessDecay` 每次基础衰减一次，室外雨天再衰减一次；快速旅行超过一小时也仅主动调用一次衰减。这是离散抽样规则，不能在迁移时默认为物理连续时间。[轮询](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:151>)、[快速旅行](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:220>)、[衰减](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:339>)。

**已确认的时间规则与建议算法**：基础衰减按已过去的游戏时间计算，包含睡眠、等待和快速旅行补算；雨天加速只积累已观测、可归属到 actor 的室外暴露区间的游戏时间。不用结束时的一次天气快照反推整段缺失历史。若采用固定间隔移除层，使用 `floor((累计时间 + 本次差值) / 间隔)` 算到期层数，并以当前层数封顶；不运行与跳过小时数成比例的补偿循环。层删除顺序仍需在领域设计中明确，旧版是每部位 LIFO。[旧版层移除](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:407>)。

## 3. 回调、计时器与协程的职责

| 方案 | 对本项目的价值 | 代价与限制 | 建议 |
| --- | --- | --- | --- |
| 事件回调 + 状态/deadline | 到期、清洗、读档、3D重建都能作为显式命令；容易保存语义状态 | 需要一个共享调度器 | 第一版采用。 |
| 协程 + `co_await delay` | 多步骤流程更接近顺序代码 | 仍需计时源、执行器、frame owner、取消与异常收口 | 只在出现真正的多阶段流程后加；不让业务持有裸 frame。 |
| 每 actor 的线程或睡眠线程 | 没有必要的独特能力 | 线程与停机成本、无法保证引擎线程上下文、读档竞态 | 不采用。 |
| 每 tick 自递归 `AddTask` | 写法很短 | SKSE 可能在同一次 drain 执行新任务，不能充当下一帧定时器 | 不采用。 |

CommonLib 并非完全没有协程支持：`IVirtualMachine::Awaitable` 与 `ADispatchMethodCall/ADispatchStaticCall` 可 await Papyrus 结果；其回调直接调用 `continuation.resume()`，`pending` 为普通 bool，并未提供计时、执行器切换或读档取消契约。不能把它当作通用且线程安全的定时器。[声明](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/I/IVirtualMachine.h:42>)、[恢复与 await_suspend](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/RE/I/IVirtualMachine.cpp:12>)、[异步派发包装](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/RE/I/IVirtualMachine.cpp:88>)。

C++ 协程只提供挂起/恢复机制，不自动提供线程、时钟、取消或存档。frame 可能动态分配；引用参数仍是引用，悬挂风险仍存在。对同一 frame 的并发恢复、恢复已销毁或不处于可恢复状态的 frame 都必须由库的所有权与执行策略排除。[协程定义规范](https://eel.is/c++draft/dcl.fct.def.coroutine)、[await 规范](https://eel.is/c++draft/expr.await)、[handle 恢复规范](https://eel.is/c++draft/coroutine.handle.resumption)。

将来若引入协程，至少需要：根任务 owner；单一恢复执行域；timer registration 与 frame 生命周期绑定；完成/取消只结算一次；读档 epoch；析构前移除等待注册；已排队恢复通过弱 ownership/token 查找任务；异常交回 owner。epoch 只判定业务工作是否过期，不延长 frame 寿命；任务 owner 与待恢复注册必须独立保证访问 frame 时它仍存在。不能仅把裸 `std::coroutine_handle<>` 捕获进 SKSE lambda，再在读档时 `destroy()`。取消请求也不能在另一线程直接销毁正在运行的 frame。

## 4. 时钟与共享调度器

### 明确区分三种时间

| 时间域 | 用途 | 暂停、读档与保存策略 |
| --- | --- | --- |
| 游戏日历时间 | 自然衰减、等待/睡眠/旅行后的结算 | 使用游戏小时/日；检测倒退与异常差值；跨读档重新建立基准。 |
| 游戏有效运行秒数（建议语义） | 污液视觉效果持续时间等“实际秒”设定 | 明确定义菜单/暂停/加载是否停止累计；存剩余秒数。 |
| `std::chrono::steady_clock` | worker 的唤醒和测量间隔 | 单调但不自动暂停；不能把绝对 time_point 写进存档，也不能据此推断游戏小时。 |

旧版效果 `OnUpdate` 每十秒检查 `GetTimeElapsed() > CumTimer`，结束时清除 FX 并发事件；不能仅凭 MCM 的“真实秒”文案推断暂停、读档时的精确行为。[旧版 effect](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_CumEffectHandler.psc:97>)。CommonLib 的 `BSTimer` 是帧时间状态/更新包装，并非公开的延期任务 scheduler；其注释还表明 pauseCount 非零会不更新。[BSTimer](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/B/BSTimer.h:33>)。具体暂停策略应先定玩法语义，再用 1170 小实验核实可用时间源。

### 推荐实现轮廓（设计推导）

- 一个 deadline 容器和按 actor/区域合并的 dirty 集合；仅受影响、已加载 actor 进入环境轮询集合。轮询频率是可配置成本，不建立每 actor 的线程、计时线程或持续协程。
- 事件进来后先更新逻辑状态，随后合并表现更新；同 actor 同区域连续变化只提交一次最终结果。clear 必须幂等；提交前复核 actor generation、区域/slot revision 与 runtime epoch，防止旧的 apply 覆盖新的 clear。
- 计时源有两条候选路线，在 S1 小实验中比较：① 已有合适、经 1170 验证的有界游戏更新入口；② 一个 `std::jthread` 等待最近 deadline，仅经 SKSE task 发唤醒，不访问 RE、RaceMenu、Papyrus。根据执行阶段、暂停行为、取消屏障与队列成本选择；本轮不预定优先项，也不因为“C++ 更快”增加 hook。
- 全局最多一个 pending pump；pump 消费合并队列并按条目数/时间预算处理，到预算后由外部计时源再次唤醒。无工作时停止轮询。worker 依据完成确认和正间隔继续等待，不用自递归 AddTask 做心跳；不能声称每次唤醒必是下一帧。
- pending/running gate 既要防重复排队，也要防两个任务入口同时执行。所有 gate 带 epoch/owner：旧任务退出时不得清除新档的 pending gate。加载阶段关闭派发；worker 的退出使用 stop token + join，不 detach。

这里的预算与 gate 只约束插件自己的队列，并不等价于引擎线程安全。SKSE 任务入口和外观调用阶段仍需下一节的验证。epoch 检查也只能拒绝过期工作，不能阻止检查后立刻开始读档的竞态；实现前必须证明世界切换与游戏对象访问处于可串行的安全阶段，或建立已验证的停派发/访问屏障。不能靠在 PreLoadGame 中等待另一个游戏任务来补这个保证。

## 5. 线程、指针与堆栈安全

### 已证明的事实与未证明的上下文

`TaskInterface` 接受 `std::function<void()>`，没有 delay/cancel 参数；包装分配 Task，运行函数后由 Dispose 释放。[声明](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/SKSE/Interfaces.h:180>)、[包装实现](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/SKSE/Interfaces.cpp:179>)。

官方 SKSE 2.2.8 的 `BSTaskPool::ProcessTasks()` 在原目标返回后，持续从自有队列弹出任务，执行 `Run/Dispose`，直到队列空；`AddTask` 只在锁内入队。两个 hook callsite 没有提供固定线程 ID 的保证。由此可以确定自递归入队存在同次 drain 反复执行的风险，不能确定每次都是固定主线程，也不能确定任务必在下一帧执行。[官方 Hooks_Threads.cpp](https://github.com/ianpatt/skse64/blob/v2.2.8/skse64/Hooks_Threads.cpp)。本地 SlaveTatsNG 注释声称观察到任务线程变化，同时带有猜测；作为实测线索，不提升为目标 1170 的已证契约。[本地注释](<E:/source/repos/SlaveTatsNG-master/src/main.cpp:156>)。

`BSTEventSource::SendEvent` 在持有 `BSSpinLockGuard` 时直接调用 `sink->ProcessEvent`，没有转交 executor。因此 sink 跟随发送者线程；每种具体事件发送线程仍需追踪或实测。sink 内不阻塞、睡眠、等待任务、递归发送同源事件或执行未知外部 API；复制必要值后返回。[事件派发](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/B/BSTEvent.h:131>)。

### 所有权与调用边界（设计建议）

| 对象/数据 | 持有方式与检查 |
| --- | --- |
| 短期 actor 引用 | `RE::ActorHandle`；使用时 `get()` 得到 `NiPointer`，检查解析结果、删除状态、cell 与需要的3D。非零 handle 不代表活对象。 |
| 长期逻辑状态/存档身份 | 保存 actor 的可重映射 FormID 与领域状态；加载后解析。不要保存 native ActorHandle 数值作为稳定身份。 |
| 异步事件参数 | 复制 FormID/handle、枚举、数字、自己拥有的字符串；不捕获事件指针、回调栈地址或借用的 `string_view`。 |
| 3D/node/material/overlay API 指针 | 仅在获证的访问阶段短期取得与使用；卸载/重建后重新查询，不跨 sleep/await/读档缓存。 |
| 纯计算任务 | 只接收不可变 DTO 和返回值，结果携带 epoch/revision；回到访问阶段先检查版本再提交。 |

`ActorHandle::get()` 返回 `NiPointer<T>`，`operator bool` 仅检查 handle 是否有值；`NiPointer` 通过引用计数附着/分离。强引用延长对象引用生命周期，不冻结3D、不证明线程安全。[handle](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/B/BSPointerHandle.h:155>)、[ActorHandle 类型](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/B/BSPointerHandle.h:198>)、[NiPointer](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/N/NiSmartPointer.h:27>)。

不要在锁内调用 RaceMenu、Papyrus、引擎函数或任意外部回调；先获取值快照、释放插件锁，再调用外部 API。若调用可能同步重入，逻辑处理采用队列避免递归展开栈。不能跨 `co_await` 持有 mutex，也不能以递归重试/每层一次调用导致与层数或补偿时间同阶增长的调用深度。异常必须在插件回调/任务边界收口，不能穿越引擎 ABI。

## 6. 新版保存、读档与取消协议

CommonLib 提供 unique ID、save/load/revert/form-delete callbacks、record API 与 FormID/VMHandle 重映射接口。`VMHandle` 与 `ActorHandle` 是不同的概念，不能用 `ResolveHandle` 重映射原生 actor 指针 handle。[序列化声明](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/SKSE/Interfaces.h:81>)、[重映射实现](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/SKSE/Interfaces.cpp:158>)。

官方 SKSE 保存/加载源码直接在当前序列化过程中调用插件 callback；应在 callback 生命周期内读写 record，不能把 `SerializationInterface*` 暂存到晚到任务中继续读写。[官方 Serialization.cpp](https://github.com/ianpatt/skse64/blob/v2.2.8/skse64/Serialization.cpp)。`PreLoadGame/PostLoadGame` 在 load lock 区间内派发；PostLoadGame 的 `data` 是 `(void*)result` 编码的布尔值，不是可解引用的 `bool*`。不要在这些回调中等待需要游戏任务继续执行才能完成的工作。[官方 Hooks_SaveLoad.cpp](https://github.com/ianpatt/skse64/blob/v2.2.8/skse64/Hooks_SaveLoad.cpp)。

**建议协议**：

1. `PreLoadGame` / revert：关闭入口、递增 `runtimeEpoch`、使旧命令与任务 token 失效、取消 timer 注册、清理插件持有的临时状态。该过程不依赖后续游戏任务才能完成；晚到任务检查 epoch 后直接退出。
2. load callback：同步读取带版本的 records 到纯数据 staging；检查长度、数量、枚举、数值范围及 FormID 解析结果。此时不访问 actor3D，也不重建表现。不承担旧版 Papyrus 的转换。
3. `PostLoadGame` 成功后，在经验证的访问阶段发布 staging、重新建立 deadline 和 actor 绑定。失败时丢弃 staging 并保持旧 epoch 无效，不复活旧任务。
4. 3D 可用后按 actor 分批重建表现；对象存在与 3D 可用是两个条件。监听对象加载/卸载等事件补做失效与重建，不能假设 PostLoadGame 时所有 NPC 的3D均已就绪。
5. save callback：同步保存最近完整提交的逻辑快照；若与更新并发，用短的插件自有锁复制快照，再释放锁并序列化。写入完成状态边界必须明确定义，不能保存半次更新。不要在 save callback 中等待 AddTask、Papyrus 返回或渲染完成。
6. new game：用新的 epoch 和空领域状态开始；不存在 SLACS2 NG record 时也走明确的空状态初始化。form delete 事件用于移除对应逻辑记录并使异步结果失效；VMHandle 到领域 actor 身份的映射需单独设计，不能强制转成 native ActorHandle。

保存内容应是：schema/version、可重映射 actor FormID、各区域层信息/顺序、衰减的游戏时间基准/累计余量、视觉效果剩余有效秒数、恢复所需配置标识。不要保存裸指针、node、native actor handle、mutex、线程、`std::function`、协程 frame 或 steady_clock 的绝对时间点。pending 任务和表现队列由语义状态重建。

## 7. 实现前的最小验证与验收

这些是后续实现阶段的小实验，本轮未执行。

- **线程与阶段**：在1170记录 AddTask、候选更新 hook、Papyrus回调、相关事件、RaceMenu回调的线程 ID、调用阶段和同步重入情况。选定可访问 RE/3D 的阶段；仅把任务串行化不足以满足该项。
- **环境**：晴→雨→晴过渡、室内有天空标志、游泳与涉水、已加载与异地NPC；验证雨水策略符合明确语义。
- **时间**：暂停/菜单、等待、睡眠、快速旅行、timescale 改动、读取更早存档；验证游戏小时与有效秒数分离，不反推未知天气历史。
- **取消与3D**：排队 apply 后 clear、排队后读档、连续两次读档、读档失败、新游戏、actor 卸载/删除与3D重建；旧epoch任务均不能作用于新状态。
- **队列**：密集事件、多actor、重复clear与大时间跳变；验证合并、bounded pump、单pending gate，无自递归任务风暴或每actor线程。
- **存取档**：新版往返、缺失record、未知schema、丢失actor FormID；只重建语义状态，不恢复进程对象。

实施顺序见 [主计划 S1–S9](../PLAN.zh-CN.md)：S1 核定执行阶段，S5 实现存档，S6 接入共享调度器与既定清洗规则。接口见 [模块契约](../INTERFACES.zh-CN.md)；协程保留为后续流程组织选项。
