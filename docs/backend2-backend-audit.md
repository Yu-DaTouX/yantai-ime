# backend2 后端与宿主兼容性排查（2026-11-08）

完成重点链路静态排查、四项可靠性修复、一项休眠兼容路径防御性修复及缓存移动优化；**不能据此认定所有软件已兼容**。本次不部署、不向窗口发送按键、不启动/结束已安装 Server/Watchdog、不运行 probe-server.ps1，也不访问用户实际数据目录。真机步骤是交给维护者执行的测试说明，不是已执行的结果。

工作树 `E:/yantai-wt/backend2`，分支 `yt/backend2`，起点 `39764300`。先读了根/windows/engine 的 AGENTS.md、CONTRIBUTING.md，以及 server/AGENTS.md。未改 `server/src/window/candidate_*`、`ui/`、`skins/`、出厂默认值、配置格式或 IPC 契约；无需运行契约生成器。

## 问题清单（严重程度排序）

P1 = 可能卡死/崩溃宿主或阻断输入；P2 = 资源/延迟及恢复风险；P3 = 当前休眠路径的防御性问题。这里的崩溃风险来自源码与模拟回调，不是假称已有宿主崩溃复现。

|级别|位置（本分支修复后行号）|问题、触发与影响|处理/证据|
|---|---|---|---|
|P1|`windows/src/Global/TsfPreeditStyle.h:48`；写端 `windows/src/IME/MetasequoiaIME_IpcWorker.cpp:493`；读端 `windows/src/Key/KeyHandler.cpp:497`|旧实现为进程共享可变静态 `std::string`。Worker 写样式时宿主线程读引用，多 TSF apartment 还可同时写；属于数据竞争/未定义行为，不仅是显示错误。|**已修**：原子枚举发布，返回不可变字面量 `string_view` 快照，保持 raw/pinyin/empty 语义。`windows/tests/tsf_preedit_style.cpp` 检查快照不随写入变化、宽字符解析和两写两读各十万次压力；未运行 TSan。提交 `7ef55b8f`。|
|P1|`windows/src/Edit/GetTextExtentEditSession.cpp:31,53,61,78`；`windows/src/IME/MetasequoiaIME.cpp:416`|原 layout edit session 借用 view/range/presenter。即使 TF_ES_SYNC，宿主的 GetTextExt/GetWnd 也可重入结束组字，导致借用对象被释放；旧结果可能写进新会话。|**已修**：只为 view/range 成对保留 COM 引用；presenter 是既有显式 delete 所有权，不能依靠 AddRef 保活，故触及它之前先由服务检查当前 presenter 身份及 composition epoch，再核对 context/range。`windows/tests/caret_extent.cpp` 编译生产身份门/构造/析构/处理函数，覆盖立即删除及布局尚未清理但纪元已变的迟到回调。提交 `f0674816`、`50ae63d0`（后者修正前者的所有权假设，必须一并保留）。|
|P1|`windows/src/IPC/Ipc.cpp:1757`|Aux 原先先同步 PIPE_WAIT WriteFile，再在等确认前切 NOWAIT；Server 活着但不读/配额不足时，激活、停用、右键通知可无限阻塞宿主。|**已修**：所有写前设置 MESSAGE/NOWAIT，失败和零字节不重发；保留原确认窗口。`windows/tests/aux_pipe.cpp` 编译真实发送函数，覆盖顺序、满缓冲、错误、超时、句柄关闭、无重发。提交 `a6289c76`。|
|P1 待验证|`windows/src/Key/KeyHandler.cpp:123,657,763`；`windows/src/IME/MetasequoiaIME.cpp:656`|既有多个路径对实现 COM 接口的 presenter 显式 delete，绕过外部 AddRef；若宿主保留 ITfUIElement 接口至 EndUIElement 之后，存在悬空接口风险。未在真实宿主保留接口场景复现。|**未整体修改**：应独立梳理 Begin/EndUIElement、layout advise/unadvise 和 presenter 所有者引用，再迁移为显式 teardown + Release，防止引用环/清理延后复活候选。本次布局 session 不增加 presenter 引用，且先验证当前身份，避免依赖这一不成立的保活假设。|
|P1|`windows/src/IPC/Ipc.cpp:429,464,475,920,2072`|首次/断管重连仍在宿主线程依次等 Reply PipeReady、Worker PipeReady、ProtocolReady（各 250ms），其后 focus ACK 最多 150ms。并非整个连接总共只有 250ms；分别临近超时才成功时，累计预算可接近 900ms，另有 hello 写和连接尝试。|**未修，高风险状态机改动**。须把握手改成协作轮询/专用连接器，保留“反向端点先注册、Main 最后授权、精确 focus token 后才能发键”的不变量；不能直接去掉 ACK 或在宿主线程 MsgWait 泵消息。方案见下文。|
|P2|`windows/src/IME/MetasequoiaIME.cpp:112`、`windows/src/IME/MetasequoiaIME_IpcWorker.cpp:168`|ServerLaunchThreadProc / IpcWorkerThread 未设执行期 C++ 异常边界；字符串/容器分配异常可能逃出线程并终止整个宿主。TryCreate 只捕获创建线程的失败，不捕获线程执行期异常。|**未修**：需定义异常时已吃键如何结算、焦点重置与模块引用如何释放，不能简单吞异常继续使用半更新状态。未做故障注入，不能量化发生率。|
|P2|`windows/src/IPC/Ipc.cpp:375,412,1751,1786`|Aux 仍有 5×20ms 连接重试、可选 150ms ACK 等待；overlapped 超时取消后 GetOverlappedResult(TRUE) 仍同步排空取消，不能声称超时是绝对硬上限。|**部分修复**：无限配额写等待已消除；剩余等待保留，避免改变停用通知顺序。应随握手异步化处理；不能不排空就销毁栈 OVERLAPPED。|
|P2|`engine/common/cache.h:79,98,106`|旧缓存插入先淘汰旧值、再加 LRU 节点，值复制/分配抛异常后留下孤立键，后续插入/淘汰可丢错条目。右值参数原来只绑定 const&，前缀补全 std::move 仍复制候选。|**已修**：新值完整存储后才淘汰、异常回滚 LRU；增加移动入口。`server/tests/src/test_candidate_cache.cpp` 验证失败不丢旧值、后续 LRU 正确、512 项插入/更新零元素复制；该测试也进入 portable。提交 `694d6312`。|
|P2|`server/src/session/engine_input_session.cpp:60,77,91,101,116,129`|每键 ApplyConfiguration，启用相关来源时反复解析模型路径/stat，前缀补全与整句来源还可能重复查同一个模型；磁盘/杀软变慢时拖高每键尾延迟。|**未修**：按模型安装/删除、配置代次缓存路径是方案，但不能只按配置字符串永久缓存，否则下载模型后不生效。当前引擎 bench 不覆盖这个 Server 适配器开销。|
|P2|`server/src/utils/serial_task_runner.cpp:29,89`；`server/src/ipc/event_listener_worker.cpp:1227`|学习写队列无界，慢盘时可积压包含候选副本的任务，增长内存；idle_handler 的 std::function 复制也在异常捕获外。|**未修**：候选副本维持选词时快照，并非可直接删除的冗余拷贝；不能简单丢学习事件或阻塞按键线程。需持久化事件/明确队列预算与合并语义及慢盘注入测试。未做长时间压力/泄漏测量。|
|P3|`windows/src/IPC/Ipc.cpp:896`|共享候选页旧实现对跨进程可写的定长字符数组做无界 assign；若无终止符，会扫入邻字段/映射外。当前 Activate 的 InitIpc 调用已注释（`windows/src/IME/MetasequoiaIME.cpp:813`），所以是休眠兼容路径防御，不声称当前正常激活可复现。|**已修**：只在 ABI 字段内找 NUL，无 NUL 返回失败且不覆盖调用者原页。`windows/tests/shared_candidate_page.cpp` 编译生产函数，验证空映射/空页/末槽终止/无终止页。提交 `e1295ce3`。|

### 锁、句柄和恢复的已查边界

- Server 发送路径是 `g_pipe_clients_mutex → PipeEndpoint::write_mutex_`（`server/src/ipc/ipc.cpp:82,1518`）；Write 内不反向获取 registry 锁，反向管道发布前已 NOWAIT（`:310,848,919`）。抽查未发现这两把锁的反向嵌套；这不是全仓锁序证明。
- `ShutdownPipeClients` 分别快照 handler/registry，再在锁外断开并等待（`server/src/ipc/ipc.cpp:1323`）；未擅自把断开搬回 registry 临界区。
- Worker 退出先 CancelIoEx/排空，再 join，最后 CloseIpc（`windows/src/IME/MetasequoiaIME_IpcWorker.cpp:224`；`MetasequoiaIME.cpp:982`）。不要为了“非阻塞”跳过排空，造成栈 OVERLAPPED/缓冲区悬空。
- 重连、focus token、composition epoch、局部 resync 与 transport reset 由已有生产函数抽取测试覆盖；本次补了布局回调寿命/归属。没有实际重启已安装 Server，没有证明所有时序不丢键。传输歧义按既有规则吞键/清组合，不重放。
- 未做全仓 COM 异常屏障、全仓句柄审计、长时间内存泄漏检测，也未改候选绘制方负责的资源生命周期。

### 首次握手异步化方案（未落地）

1. owner 保存握手阶段、句柄、request id、截止时间和 generation；每次窗口消息只推进已完成 I/O，不等待 250ms。I/O 若跨回调悬挂，OVERLAPPED/缓冲区必须拥有持久寿命。
2. 先 Reply ACK、再 Worker ACK，发布 worker 后再 Main/ProtocolReady；最后 focus ACK。未完成不授权发送键，不把协议确认解释成候选，不移动 COM 对象到后台线程。
3. 以 HWND token、focus token、composition epoch 隔离迟到完成；失焦/停用时取消并排空，只有 owner 消息能安装新连接。
4. 必须新增真实私有管道故障注入：各阶段延迟 >250ms、ACK 迟到/乱序、取消与完成竞赛、停用时挂起、重连时句柄复用；断言 owner 单次调用不阻塞、首键最多发送一次、不往新文档投旧结果。现有 reconnect/focus 测试不能替代这些 I/O 时序测试。

## 性能：实测，不把噪声当收益

同机 i7-13700F，Windows/MSVC Release。仅使用 `E:/yantai-wt/.bench-data` 副本。基线为起点代码 + 只暴露原有评测工具的 CMake 改动；后测为缓存优化后的同工具/同数据。后续 TSF 修复不链接进这两个引擎评测程序。

```bash
bash E:/yantai-wt/yantai-build.sh E:/yantai-wt/backend2 server
METASEQUOIA_IME_DATA_DIR=E:/yantai-wt/.bench-data \
  E:/yantai-wt/backend2/server/build/bin/Release/bench_input_session.exe 9
METASEQUOIA_IME_DATA_DIR=E:/yantai-wt/.bench-data \
  E:/yantai-wt/backend2/server/build/bin/Release/eval_quanpin_sentence.exe \
  E:/yantai-wt/backend2/engine/tests/data/quanpin_sentence_eval_300.tsv --mode trigram --rounds 3
```

`bench_input_session` 六条固定输入，每条 reset_state/reset_cache，预热一轮不计、正式九轮；计 handle_character 同步耗时（包含候选生成）。数字为每键 ms：

|模式|前 median / p90 / max|后 median / p90 / max|
|---|---|---|
|off|0.09 / 1.06 / 3.14|0.09 / 1.00 / 2.59|
|lattice|0.39 / 1.23 / 2.57|0.42 / 1.37 / 3.58|
|lattice+google|0.42 / 1.36 / 3.32|0.41 / 1.33 / 2.49|
|+neural_keyboard|0.44 / 1.39 / 2.63|0.44 / 1.34 / 4.39|
|+neural_desktop|0.41 / 1.19 / 2.42|0.41 / 1.19 / 2.50|
|all|0.43 / 1.36 / 2.85|0.43 / 1.18 / 2.71|

`eval_quanpin_sentence` 文件名虽是 300，实际载入 **320 条**。一轮预热不计、正式三轮取汇总中位数：

|指标|前|后|
|---|---:|---:|
|每键 mean ms|0.9183|0.7639|
|每键 p95 ms|2.9120|2.1575|
|每键 median ms|0.6096|0.5465|
|top1 / top3 / sent1|86.25% / 86.25% / 86.25%|86.25% / 86.25% / 86.25%|
|空候选 / 非完整拼音|0 / 0|0 / 0|
|后台 ready ms|0（此模式同步）|0（此模式同步）|
|工作集 / 峰值 MB|50.3 / 55.4|50.0 / 54.3|

这不是纯 SQL 查询基准，也不是宿主→TSF→Server→上屏端到端延迟。评测没有独立报告“只生成候选”分段耗时；以上只能作为含候选生成的同步基线。没有实际 Server 冷启动耗时、Server 常驻内存或真实宿主每键耗时数据。

**没有证据把 eval 的下降归因于缓存移动优化**：九轮短语 bench 有涨有跌，且移动优化主要作用于前缀补全缓存，默认评测未启用该功能。确定性收益是 512 个候选元素的右值插入/更新从复制到零复制（计数回归，不是耗时百分比）；其余数字用于观察防退化，不宣传提速比例。

保留的本地原始输出为 `bench-before.log`、`bench-after.log`、`eval-before.log`、`eval-after.log`（不提交）。测量期间未主动更换数据；未监控其他进程是否写副本。末次记录的 SHA256：

- TSV：`ac8ff3fb0fb9b77f03b3dd6ca47ee404c0b8964efa74f637d0be20dc4e823bfc`
- msime.db：`353a1a4a22ac75e3e47bddfd5b43e248b287a35b32ba22d39204ea255ab6233c`
- sc.lm：`3588b3942c8fd62e1a6bd3bae8c7cadc0faf928c175b245224ed475862218387`

## 宿主分类审查与可自动化真机步骤（全部待执行）

约定：由测试脚本先聚焦空白可编辑控件，选水杉全拼、中文、半角；不得用 Unicode 文本注入/粘贴来代替 A–Z 虚拟键，否则绕过按键分类。`{SPACE}` 等表示实际按下/释放；字符间隔分别跑 100ms 和 10ms 两档。断言控件最终文本、composition 已结束、没有额外按键/重复上屏。中文词断言假定简体且“nihao”首选为“你好”；若该用户已调频，先记录首选作为 oracle，不修改用户配置或词频。

通用用例（每条从空字段开始）：

|ID|输入|期望|
|---|---|---|
|K1|`nihao{SPACE}`|`你好`，无额外空格，无残留 composition|
|K2|`nihaox{BACKSPACE}{ENTER}`|精确 `nihao`，这一颗 Enter 不产生换行/发送/执行命令|
|K3|`nihao{ESC}abc{ENTER}`|精确 `abc`，没有取消前的 nihao/你好|
|K4|`nihao{BACKSPACE}o{SPACE}` 连续重复 100 次|`你好`×100；次数不能少、不能重复提交|
|K5|A 框敲 `nihao`，聚焦 B 框敲 `zhongguo{ENTER}`|B 精确 `zhongguo`，不得混入 A 的回复；A 原组字按宿主既有确定/取消策略结束，不预设跨宿主统一文案|
|K6|在两个不同软件的空字段来回聚焦，每次执行 K2，共 100 次|每框恰有其自己的 `nihao`，无串窗、无卡死|
|K7|维护者在独立测试环境中重启 Server，等待协议/焦点恢复后在新字段执行 K2、K1|恢复后分别 `nihao`、`你好`；组字中重启时旧组合允许按既有失败策略清掉，不要求重放已吃键，也不能双上屏|

|宿主类别|失败方式 / 源码已有处理 / 本次变化|建议步骤与额外 oracle|
|---|---|---|
|Chromium/Electron：Chrome、Edge、VS Code、微信新版、QQ NT|renderer 落后时 TS_E_NOLAYOUT、临时 TF_LC_DESTROY、contenteditable 换 document、宿主提前确定。已有保留本布局有效 anchor（`windows/src/Tf/TfTextLayoutSink.cpp:287`）、组字中不因临时 view destroy 清 Server（`windows/src/UI/CandidateListUIPresenter.cpp:1063`）、精确 composition 身份终止（`windows/src/Composition/Composition.cpp:1149`）。本次修复布局重入寿命和样式数据竞争。|浏览器分别跑 textarea/contenteditable/地址栏的 K1–K6；VS Code 普通编辑器与集成终端分别跑；微信/QQ 只在草稿空框 K1–K4，不按第二颗 Enter 发送。组字时滚动/切 tab 再回原框，候选不得闪到旧文档；10ms 档重复 K4。|
|Office：Word、Excel、PowerPoint|Word 修饰键快照滞后；Excel 进入单元格编辑时短暂 NULL focus、成对标点光标左移可能变成换单元格。已有物理修饰键门（`windows/src/Key/KeyEventSink_Hotkeys.cpp:56`）、延迟焦点丢失（`windows/src/Thread/ThreadMgrEventSink.cpp:160`）、Excel 成对标点排除（`windows/src/Global/FanyDefines.h:18`）。本次布局寿命防护通用受益，未改变 Office 策略。|Word 空文档 K1–K4；Excel 空 A1 分别“直接键入”和 F2 后跑 K1，随后额外 Enter 应为 A1=`你好`、移到 A2；K2 的第一颗 Enter 只完成输入，不额外导航。PowerPoint 空文本框 K1–K3，检查不是意外新建段落/退出文本编辑。另测 Word 单独 Shift 一次切英文后键入 abc，期望 abc，不能有双切换。|
|经典 Win32 EDIT / RichEdit（含 x86）|read-only/禁键上下文、选区替换、异步 edit session 拒绝。已有 `_IsKeyboardDisabled` 与失败分类/重置；range 操作有局部 SEH 防护（`windows/src/Composition/Composition.cpp:17`），不是所有 COM 方法都有异常屏障。本次 Aux 有界写与样式原子化适用。|空 EDIT 与 RichEdit K1–K6；选择已有 `seed` 后 K1 应恰好替换为 `你好`；只读框按 K2 必须原文不变且宿主快捷键仍可用。另用 32 位 harness 重复；本次仅编了 x64 DLL，x86 未验证。|
|UWP / WinUI：SearchHost、设置|ApplicationFrameHost 与真实输入线程跨进程；Win+. 暂时抢 TSF 焦点，过期布局回调可能复活候选。Server 已匹配 GetGUIThreadInfo.hwndFocus 线程（`server/src/ipc/ipc.cpp:1257`）；线程焦点保留/恢复见 `windows/src/Thread/ThreadFocusSink.cpp:98,156`。本次布局结果归属检查避免旧回调更新新会话。|SearchHost 搜索框/设置搜索框 K1–K3，只断言文本，不按第二颗 Enter 打开结果。普通 WinUI 输入框敲 `nihao`→Win+.→Esc→Space，应 `你好`，候选不得留在面板里；来回测试 K5/K6。|
|控制台、Windows Terminal、mintty|浅 text store 不能读回文档/移动光标；IMM 桥可能不给裸 Shift key-up。已有浅文本探测回退（`windows/src/Composition/Composition.cpp:186,986`）、仅 mintty 的裸 Shift hook（`windows/src/Key/KeyEventSink_Hotkeys.cpp:89`）。本次 Aux 停用通知不再无限写等待，没扩大 hook。|在不会执行命令的空提示输入区域跑 K1/K2，不发送第二颗 Enter；K2 结束后 shell 应未执行任何命令。Windows Terminal 的 PowerShell/cmd profile、传统 conhost、mintty 分别跑 K1–K4；mintty 单独 Shift 后 abc 应原样 abc。|
|管理员程序 / uiAccess|跨完整性级别候选不置顶、IPC 拒绝；uiAccess 还依赖签名及受信安装路径，不是改 manifest 即保证。已有访问拒绝视为 Server mutex 存在（`windows/src/IME/MetasequoiaIME.cpp:51`）、管道 SID DACL（`server/src/ipc/ipc.cpp:256`）；本次不改安全边界。|维护者部署已签名构建后，普通/提升的同一 EDIT harness 各跑 K1–K6；两者均应上屏且候选可见、不能弹 UAC 或反复拉 Server。脚本提升由维护者预先完成，不测试安全桌面/密码框；开发目录裸构建不具备可比的 uiAccess 条件。|
|游戏、全屏程序|游戏宣称 UILess 却不画候选、独占全屏被浮窗打断、IMM 预判与实际按键时点不同。已有 CS2/Dota2 例外与 Minecraft 顶角锚点（`windows/src/Global/FanyDefines.h:24,28`）、CapsLock 延迟读边沿（`windows/src/Key/KeyEventSink.cpp:319`）。本次不改绘制/游戏策略；共享页无界读仅作休眠路径防御。|先由脚本打开游戏聊天并确保空字段，再跑 K1–K3（不发送消息）；CS2/Dota2 分别核对当前游戏兼容名称配置对应“宿主画”或“IME 画”，不能两份/都没有；Minecraft 基岩候选应固定当前显示器顶角，不从右下跳左上。窗口/无边框/独占全屏分别跑，不能踢出全屏。|
|Java / IDEA 的 IMM32 兼容路径|可能仅有代理 range、文档读回为空、VK_PACKET/组字事件顺序与原生 TSF 不同。已读代码未发现 IDEA/javaw 专门适配；依赖通用 range/按键/焦点路径，不能外推成 Java 已验证。没有扩大 mintty hook 到 Java。|IDEA 普通编辑器、搜索框、内置终端分别 K1–K6；x64 JVM 与实际使用的 JBR 版本记录到报告。K2 必须是 nihao 且不能误执行 Search/Run 动作；组字时点选另一文件再执行 K2，不能把旧候选写进新文件。|
|远程桌面 / RDP|本地与远端 IME 双处理、断线/重连焦点过期、动态分辨率与 DPI。绘制端已有 RdpForeground 缩放路径（`server/src/window/candidate_presenter.cpp:899,948`），属于另一人的改动边界，本次未改。TSF 侧仍走通用焦点/布局路径，不保证键盘重定向策略。|分“本地中文 IME、远端英文”和“本地英文、远端中文 IME”两种拓扑，不要两边同时中文。远端空 EDIT 跑 K1–K4；组字时断线重连，在新框 K2 应恰好 nihao、旧回复不能串入。100/150/200% 与动态分辨率切换后 K1，候选应在当前显示器工作区；不能拿本地 UIA 文本当远端上屏 oracle，需远端 harness 回传实际文本。|

额外针对新增样式竞态的真机项：维护者在测试配置副本中，持续输入 K4 的同时依次切 raw/pinyin/empty，并在多个同进程输入线程之间切换。最终上屏仍为 `你好`×100，三种样式都能生效、不出现撕裂样式/崩溃。这里未自动写任何实际配置。

## 已执行验证与提交

- `bash E:/yantai-wt/yantai-build.sh E:/yantai-wt/backend2 all`：最终 **BUILD OK**（Server + x64 TSF）。
- `bash E:/yantai-wt/yantai-build.sh E:/yantai-wt/backend2 test`：最终 **BUILD OK**；Server 9/9 ctest 目标、portable 1/1、windows 23/23。Server 原有 Ctrl+Enter 路由、TSF 全拼按键分类、commit-reply/focus/reconnect 回归继续通过。
- 将现有引擎逐键候选一致性测试接入受支持的 Server 测试命令：`server/tests/CMakeLists.txt:12`，本次日志该目标 0.08s 通过；没有声称跑过整个独立 engine ctest 集合。
- clang-format 18.1.8 对所有变动 C++ 文件 `--dry-run --Werror` 通过。未提交 build-errors.txt 或任何 *.log。
- 七个代码/测试主题提交，作者均为 YuDaTou <174885674+Yu-DaTouX@users.noreply.github.com>：`f0674816` 布局重入、`9e9b8759` 测试/评测接线、`694d6312` 缓存、`a6289c76` Aux、`7ef55b8f` 样式并发、`e1295ce3` 共享页、`50ae63d0` 布局实际所有权/纪元补充（不可遗漏）。没有 push。
