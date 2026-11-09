# 问题清单第 1、2 条调查（v0.1.0）

调查日期 2026-11-09。只读调查，未改仓库代码、未提交。工作树 `E:\yantai-wt\misc`（分支 yt/misc，调查结束时 `git status` 干净）。

## 结论（先看这里）

**第 1 条（Word 首次 Ctrl+A 后多出「啊」）**：最可能是 TSF 前端用 `GetAsyncKeyState` 读修饰键（读的是「此刻的物理键盘」），
而不是 `GetKeyState`（读「正在处理的这条消息发出时」的状态）。宿主线程忙（Word 刚启动）时，按键消息排队，
等 Word 处理到 Ctrl+A 的 `A` 时，测试脚本已经松开了 Ctrl（Ctrl+A 全程只有 ~35ms 按下 + ~120ms 间隔），
`CaptureIpcModifiers()` 读到 Ctrl=0，`A` 被当成普通字母吃进组字（`a'ni'hao`），空格上屏「啊你好」。
**这不只是测试时序问题**：真人在宿主卡顿时快速按 Ctrl+A / Ctrl+C / Ctrl+V 也会被拆成「字母进组字」。
修法：模式键采集改用 `GetKeyState`（和同一函数族里的 `Global::UpdateModifiers` 一致），加单元测试和一条「卡住宿主」的真机用例。
这是推断，**没有在真实 Word 里复现**（调查边界禁止往前台发键、不能占屏幕），见下文「没验证的」。

**第 2 条（gaidaoxiawusandian 首选「该到下午三点」）**：已在离线评测里按用户配置（unigram+trigram+neural_desktop，白霜 msime.db 副本 + sc.lm + 神经桌面模型）复现。
原因在 **Trigram 词格（sc.lm）这一层**：它把「该/到」两个单字拆开走，比「改到」少 0.47 个 log10，路径分最高；
神经重排在没有上文时也略偏向「该到」（Δ=0.32 nats），两者一致，所以首选是「该到下午三点」，Google 整句（Unigram）给的正确的「改到下午三点」被压到第 2。
不是白霜词库词频问题（词库里「改到」2500、「该到」仅 1，并且词库权重在词格里只起 0.01 量级的平局作用）。
建议：先把评测草案并入评测集建基线；修法优先考虑「词格里给高权重多音节词整词加一点边加成」，并用 300 句集做回归，不建议单独调神经 λ（无上文时调了也翻不过来）。

---

## 一、第 1 条：Ctrl+A 被当字母

### 1.1 路径与证据

按键判定链（`windows/AGENTS.md` 核心输入链路）：`OnTestKeyDown` → `_IsKeyEaten`。Ctrl 组合键是否交还宿主，全看 `CaptureIpcModifiers()`：

- `windows/src/Key/KeyEventSink.cpp:136-146`：`CaptureIpcModifiers()` 用 `GetAsyncKeyState(VK_SHIFT/VK_CONTROL/VK_MENU)` 采集。
- `KeyEventSink.cpp:433`：`_IsKeyEaten` 中文模式分支入口 `shortcutModifiers = CaptureIpcModifiers()`；`:514` 用它调 `key_route_policy::RouteModifiedKey(...)`。
- `windows/src/Key/KeyRoutingPolicy.h`（`RouteModifiedKey`）：只要 `modifiers & (Control|Alt)` 非零就返回 `Application`（交还宿主，`_IsKeyEaten` 返回 false）；为零才走本地分类器 → `IsVirtualKeyNeedForFreshComposition` 把字母吃进组字。
- 所以 Ctrl 位只要在采集那一刻是 0，Ctrl+A 的 A 就会成为字母 a。
- 延迟路径同理：`KeyEventSink.cpp:987`（`_DispatchKeyDown` 的 `capturedModifiers`）、`KeyEventSink_DeferredKeys.cpp:389` 都落回 `CaptureIpcModifiers()`。
- 同一个键事件里，`Global::UpdateModifiers`（`windows/src/Global/Globals.cpp:389-396`）用的是 `GetKeyState`——**同一个 sink 里两套口径并存**。
- 其他 `GetAsyncKeyState` 用法：`Composition/CompositionProcessorEngine_KeyClassify.cpp:171`（V 模式 Shift+数字）、`KeyEventSink_DeferredKeys.cpp:456-457`（Win 键、Ctrl/Alt 位）、`KeyEventSink.cpp:118,242`（Win 键）。`KeyEventSink_Hotkeys.cpp:69` 的注释明确说 modifier-release 热键的「预备」要读真实键盘，所以那一处是有意的，不在修改范围。

测试脚本的按键方式（`scripts/compat-test.ps1:58-59,87`）：`keybd_event` 真实注入，`Chord` = Down Ctrl → Down A → 睡 35ms → Up A → Up Ctrl → 睡 120ms。Ctrl 总共只按下 ~35ms。
宿主要是在 Ctrl 抬起后才处理到 A 的消息，`GetAsyncKeyState(VK_CONTROL)` 就是 0。

与现象的吻合度：
- 只有「新开 Word 的第一个用例」中招，之后 9 个都正常——Word 刚启动时 UI 线程忙，之后空闲，消息处理及时。
- `A` 被输入法吃掉，所以 Word 也没收到 Ctrl+A，这一次的清空/选择都没做；随后的 `nihao` 前面带着一个 `a`，读回「啊你好」。若是 Word 收到了 Ctrl+A，就不会有字母 a 进组字。

### 1.2 排除的其他路径

- 「激活前 Ctrl 已按下」：`OnSetFocus`/激活路径不改修饰键状态（修饰键每次按键现采），不会让 Ctrl+A 变字母。
- 「OnTestKeyDown 与 OnKeyDown 不一致」：Test 看到 Ctrl（交还宿主）就不会再有 OnKeyDown；Test 看到无 Ctrl（吃）时 OnKeyDown 也不会再看到 Ctrl（已松开）。两次采集不会造成「Test 交还、KeyDown 却处理」，只会一致地吃错。
- `thread_local` 的 `Global::ModifiersValue`（Globals.cpp:380）只用于热键/Shift 单击判定，不参与 Ctrl+字母路由。

### 1.3 修复方案

1. 把修饰键采集抽成头文件内联函数（例如 `windows/src/Key/ModifierCapture.h`），内部改用 `GetKeyState`：它返回「当前线程正在处理的这条键盘消息发出时」的状态，与 TSF 在消息循环里同步调用 sink 的时机一致。保留 `CaptureIpcModifiers()` 作为薄封装，所有调用点不动。
2. 同类的 `GetAsyncKeyState(VK_LWIN/VK_RWIN)`（KeyEventSink.cpp:118,242,512-513、DeferredKeys:456-457）和 KeyClassify.cpp:171 一并改，口径一致。Hotkeys.cpp 的「真实键盘」读取保持不变。
3. 风险：`GetKeyState` 在某些跨进程输入宿主（触摸键盘/UWP）里状态可能不同步，所以改动后要在记事本、Chrome、Edge、Word、Windows Terminal 各跑一遍 `compat-test.ps1`；以及 Shift 切换（Shift 单击不影响，因为 Shift 走 `UpdateModifiers`）。
4. 不要「在激活/焦点时再补采一次」——问题不在焦点，而在每条消息的采集时刻。

### 1.4 能自动化的复现测试设计

A. 单元测试（可进 CI，`windows/tests` 独立工程）：
- 前提：把采集函数放进头文件（见上 1），测试里不需要链 TSF DLL。
- 用 `SetKeyboardState` 往测试线程的键盘状态表里写 `VK_CONTROL=0x80`（文档：该表就是 `GetKeyState` 读的那张），此时没有物理按键，`GetAsyncKeyState(VK_CONTROL)` 必为 0。
- 断言 `CaptureIpcModifiers() & kControl` 为真，再用 `RouteModifiedKey(...)` 断言结果是 `Application`。修复前此用例失败，修复后通过。反向用例：状态表清零后应为 0。
- 不注入任何真实按键，不抢前台。

B. 真机用例（扩展 `scripts/compat-test.ps1`，在测试自己新开的窗口里跑，沿用 Guard 保护）：
- 新建一个 WinForms 文本框窗口，其 UI 线程在获得焦点后立刻被 `Thread.Sleep(600)` 阻塞；窗口阻塞期间用现有 `Chord @($VK.Ctrl) $VK.A`（Ctrl 只按 35ms）注入；阻塞解除后继续敲 `nihao` + 空格，读回文字。
- 期望「你好」，修复前应复现「啊你好」。这是对「宿主忙、消息排队」的确定性模拟，比等 Word 冷启动稳定。
- 对照用例：同样阻塞，但 Ctrl 按住 800ms，应无论修不修都通过，用来确认判定的是采集时刻而不是别的。

### 1.5 没验证的

- 没有在真实 Word 里复现（边界禁止往前台发按键）。Word 冷启动期间消息处理延迟超过 ~35ms 是推断，未量到。
- 没有读 Word 内部的 TSF 调用时机；「宿主在自己消息循环里同步调 TestKeyDown」依据 TSF 一般行为，未逐宿主确认。
- 备选解释（Word 冷启动时输入法尚未激活 → 这次 Ctrl+A 根本没经过输入法）不能产生字母 a 进组字，因此和现象不符，已排除；但无法排除「还有第二个原因同时存在」。

---

## 二、第 2 条：「该到」压过「改到」

### 2.1 复现

方法：用户数据目录的 msime.db（含 -wal）、sc.lm、神经桌面模型只读复制到 `E:\yantai-wt\misc-data`，config.toml 同步（sentence_wordlattice / sentence_google / sentence_neural_desktop 均为 true）。
用工作树自带的 `engine/tests` 的 `eval_quanpin_sentence`（Release，低优先级构建，`BUILD OK`），`METASEQUOIA_IME_DATA_DIR` 指向副本，`--sc-lm` 指向副本里的 sc.lm。

输入 `gaidaoxiawusandian`，首选：

| 模式 | 首选 |
|---|---|
| unigram（Google） | 改到下午三点 ✔ |
| neural_desktop（单开，**没有 sc.lm**，n-best 来自启发式） | 改到下午三点 ✔（注意：这不是产品配置，见下） |
| trigram（词格 + sc.lm） | **该到下午三点** ✘ |
| trigram + neural_desktop | **该到下午三点** ✘ |
| unigram + trigram + neural_desktop（用户配置） | **该到下午三点** ✘，「改到下午三点」（Google）排第 2 |

用户配置下前几行：#0 该到下午三点（Generated，w=-11380）、#1 改到下午三点（Fallback/Unigram）、#2 改到（词库 w=2500）、…、「该到」词库行 w=1 排很后。多次复跑结果一致。

### 2.2 各层打分

词库（msime.db 副本，只读查询）：
- `gai'dao`：改到 2500、改道 1475、该岛 340、改刀 340、**该到 1**；`gai`：该 339830、改 56880；`dao`：到 1611270；`xia'wu`：下午 69435；`san'dian`：三点 23095。
- 词库里整词「改到」远强于「该到」，词库层没有问题。词格里词库权重只作平局的小项：`dictionary_tiebreak` 系数 0.01 × log10(权重)（`engine/quanpin/word_lattice.h`），而且单字「该」(339830) 的权重本身就大于「改」(56880)，加大它只会更偏向「该」。
- 词格 n-best（对 sc.lm 打分，log10，临时打印，已还原）：该/到/下午/三点 **-11.381**；盖到… -11.760；改/到/下午/三点 **-11.846**；改道… -12.330。

sc.lm 逐词分（用 `ngram::LanguageModel` 直接查，log10）：
- 该 -3.206，到 -3.177；改 -4.241，到 -2.825；整词「改到」-6.036，「该到」-6.266。
- 「改到 下午 三点」-12.584，「该 到 下午 三点」-11.590，「改 到 下午 三点」-12.048。
- 即：「改到」作整词其实比「该到」好 0.23，但「改到」之后 `下午` 回退到一元（-4.139），而「到 下午」有二元（-3.5）；加上「该」比「改」高频 1.03，单字拆分路径反超整词路径。拆成 该/到 的路径在 n-best 里同句去重后成为「该到下午三点」的代表，分最高。

神经桌面模型（score_sentences，nats，λ=0.5 的 `rerank_order`，无上文）：
- 该到下午三点 -25.192；改到下午三点 -25.515；盖到 -28.890；改道 -32.594。神经略偏「该到」（+0.32），与词格同向 → 首选「该到」，神经行与词格行重复故不再单独出现。
- 带上文「今天的会议」：该到 -24.992，改到 -24.161，神经偏「改到」0.83；但词格 static 差 1.08 nats，λ=0.5 下综合仍是「该到」微弱领先（0.5×(1.08−0.83)≈+0.12）。用 `--context 今天的会议` 在引擎里实跑也还是「该到」。要让它翻过来 λ 需 >≈0.57。
- 产品里神经的上文是宿主已上屏的文字（`server/src/ipc/event_listener.cpp:621-635` 的 `g_rescoring_context`）；翻译模式草稿里的「今天的会议」是否算上文，我**没有核实**。

结论：错在 Trigram 词格的路径分（词级一元频率 + 回退），神经没救成，Unigram 的正确答案被排到第二。

### 2.3 修复建议（按推荐度）

1. **先建基线**：把下面的评测草案并入 `engine/tests/data/quanpin_sentence_cases.tsv`（类别 homophone），在当前代码上跑出「改动前」数字。用户配置下这 25 句首选 68%（17/25），前 3 命中 88%（22/25）；Unigram 52%、Trigram 60%、神经单开 44%（神经单开有异步等待伪影，忽略）。
2. **词格：高权重多音节词整词的边加成**。思路：在 `build_graph`（word_lattice.cpp）里，对跨度 ≥2 音节、词库权重高且读音占比高的整词边加一个小正加成 β（log10，量级 0.3~0.5），抵消「拆成单字路径靠高频单字占便宜」的系统偏置。本例差距是 0.47，β≈0.5 才够，所以要**用 300 句集、`eval_quanpin_sentence`（--mode trigram+neural_desktop 和用户配置）做前后对照**，确认首选率不降、每键耗时不升；风险是把「改道」类的双字词压过「改/到」之类的正确单字组合。
3. **提高神经 λ**：只在有上文时有效（见 2.2），无上文时神经本身偏「该到」，调 λ 无益，且会改变所有整句，不建议单独做。
4. **用户词库**（问题清单第 6 条的学习机制）可以让用户选一次「改到」后记住，但它不能修首选的默认值。
5. 不建议靠改词库权重修：词库层已正确，且词格里权重只是平局项。

### 2.4 评测文件草案

`research/quanpin_sentence_homophone_cases.draft.tsv`（25 条：改到/该到 9、在/再 6、的/得/地 5、做/作 5）。
格式：输入 / 期望首选 / 类别 / **上文（第 4 列，空）** / 备注。
**注意**：`eval_quanpin_sentence` 把第 4 列当神经重排上文。我第一版把备注写在第 4 列，结果「改到下午三点」居然通过了——就是因为备注文字成了上文。已改成备注放第 5 列。并入主评测集时要保持这个约定，不然会污染结果。

已知的歧义/偏弱的条目（并入前请人工确认期望）：`zhegedingdangaidaoshangwujiudian`（上午九点/商务酒店本身有歧义）、`nizhendegaidaozheli` 与 `nimenmingtiangaidaoxuexiaole`（刻意的「该到」反例，防止修完后把正确的该到全压掉）、`tadexingzhiyuanyuanbuzhiyuci`（他/它、兴致/性质歧义）。

## 附：产物与环境

- 本报告：`research/issue-1-2.md`
- 评测草案：`research/quanpin_sentence_homophone_cases.draft.tsv`
- 探针：`research/probe/probe_lm.cpp`（直接查 sc.lm 逐词分 + 神经 score_sentences；构建方式是在 engine/tests/CMakeLists.txt 加一个链接 `MetasequoiaIme::Engine` 的目标）；`research/probe/debug-dump.patch`（临时的 n-best/重排打印和候选清单打印，已从工作树还原）。
- 构建：`engine/tests` 的 `eval_quanpin_sentence`/`probe_lm` 在 `E:\yantai-wt\misc\engine\tests\build`（git 忽略）；数据副本 `E:\yantai-wt\misc-data`。构建用 `start /low` 低优先级。`build-low.sh` 不覆盖 engine/tests，所以用了本地脚本 `E:\yantai-wt\misc-eval-build.sh`。
- 未做：没有构建 TSF（没改代码）；没有跑 `scripts/ci/probe-server.ps1`，没有动 Server/Watchdog，没有读写用户数据目录以外的内容（只读复制）。
