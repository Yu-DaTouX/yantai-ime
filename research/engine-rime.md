# 用 librime 做轻量 Windows 全拼输入法：可行性调研

调研日期 2026-11-08。源码来源：`E:/yantai-wt/engine-probe/` 下 `git clone --depth 1` 的
librime（HEAD `dd705412`，1.17.0）、weasel（HEAD `f96ed24a`，0.17.4）、librime-lua、librime-octagram、
librime-predict、moqi-im-windows；以及本仓 `E:/水杉输入法` 的现有实现。

---

## 结论

**可行，而且比"继续删水杉新引擎的特性"更省事；但它换掉的是引擎，不是前端，也不会自动变轻。**
具体四条：

1. **接口面很小。** 我们的 TSF 组件（`windows/`）只 include `engine/contracts/` 里的纯头文件，
   不链接引擎、不 include 引擎头（`windows/src/IPC/Ipc.h:12`、`windows/CMakeLists.txt:188` 只链 `fmt`）。
   换后端时 **TSF 一行都不用改**。真正要改的只有 server 里的 `IInputSession` 实现——38 个虚函数
   （`server/src/session/input_session.h`，`grep -c virtual` = 38），全仓只有 7 个文件引用这个接口。
2. **能保留 D2D 候选窗和所有"外面"的功能。** 候选窗（`server/src/window/` + `ui/` + d3d11/dcomp，
   `server/CMakeLists.txt:422-427`）、翻译、英文、emoji、符号、日期时间、简繁、快捷短语都活在
   `IInputSession` 之上（候选页在 `server/src/ipc/event_listener_candidates.cpp:631,823` 组装），
   换引擎不碰它们。**但这些功能的排序/来源语义依赖引擎给的 `WordItem`，要重新标定。**
3. **体积确实能小。** librime 1.17.0 的 Windows MSVC x64 发布包实测 `rime.dll` = 3,739,136 B（3.57 MiB），
   且 lua/octagram/predict 三个插件已合并进这一个 DLL（`dist/version-info.txt` 的 `RIME_PLUGINS=`，
   实测解包自 `rime-33e7814-Windows-msvc-x64.7z`，7,428,809 B）。我们现在的产物是
   `MetasequoiaImeTsf.dll` 2,483,712 B + `MetasequoiaImeServer.exe` 8,897,024 B + msimeui + SQLite 词库
   159,899,648 B（`docs/dictionary-frost-results.md` 表：152.49 MiB）。
4. **整句质量是唯一真风险，而且没有免费答案。** 我们实测：万象 `.gram` 单模型首选 87.50%，
   Trigram+神经重排 92.81%（`engine/tests/data/quanpin_sentence_eval_300.results.md`）。
   librime 的整句同样只靠词频 + 同一份 `.gram`（见 §1.5），**大概率落在 87% 这一档，拿不到神经那 5 个点**；
   这一点我没有实测 librime，属于判断，不是结论。

工作量粗估 **6–10 人周（1 名熟手，含回归）**，最大风险不是 C API，而是词库/用户词库/候选语义迁移。
先做 2 周 spike（只实现按键+候选+预编辑，接现成 rime-ice 词库跑我们那 320 句评测）再决定，是更稳的走法。

---

## 1. librime

### 1.1 架构：engine / schema / segmentor / translator / filter / gear

- **Engine 是流水线，不是单体。** `ConcreteEngine` 持 6 组组件：`processors_ / segmentors_ /
  translators_ / filters_ / formatters_ / post_processors_`，全部由 schema 配置里的列表创建
  （`librime/src/rime/engine.cc:558-592`，`CreateComponentsFromList` 在 `:507-530`）。
  按键进来先过 processor（`speller`、`selector`、`navigator`、`punctuator`…），再按 segmentor 切段，
  translator 出 `Translation`（惰性候选流），filter 改写候选列表。
- **schema 是 DSL。** 一份 `<id>.schema.yaml` 里写 `engine/processors|segmentors|translators|filters`、
  `speller/algebra`、`translator/dictionary`、`switches`、`punctuator`、`key_binder`。装载时
  `ApplySchema` 清 context、按列表重建组件（`engine.cc:365-378`）。
- **组件按名字注册、按 Ticket 创建。** `Config` 里写 `lua_filter@*corrector` 这样一行，就解析成
  `klass=lua_filter, namespace=corrector`，去 `Registry` 取 `Component<Filter>`（`engine.cc:524-528`）。
- **gear 就是内置组件全家桶。** `src/rime/gear/` 下 60 多个文件，`gears_module.cc:37-90` 注册
  `speller / selector / navigator / punctuator / express_editor / abc_segmentor / matcher /
  script_translator / table_translator / uniquifier / simplifier / key_binder ...`。
- **filter 的契约只有一行**：`Apply(an<Translation>, CandidateList*)`（`src/rime/filter.h:29-33`），
  所以"加一行候选/改排序/加注释"都可以做成 filter，不用碰 translator。
- **整句在 Poet 里。** `script_translator` 把词图交给 `Poet::MakeSentence`；
  **没有 grammar 走动态规划，有 grammar 走 beam search**（`src/rime/gear/poet.cc:246-253`），
  每一步用 `Grammar::Evaluate(context, word, weight, is_rear, grammar)` 累加分
  （`poet.cc:218-221,287`，`src/rime/gear/grammar.h:18-27`）。

### 1.2 C API 怎么嵌

头文件 `src/rime_api.h`（610 行，纯 C，`extern "C"`，Windows 下 `RIME_EXPORTS/RIME_IMPORTS` 控制
`__declspec`）。**入口不是一堆散函数，而是一个版本化结构体**：`RimeApi` 里全是函数指针
（`rime_api.h:269-520`），用 `RimeGetApi()`/`rime_get_api()` 取（`:530`），第一个字段是 `data_size`，
调用方用 `RIME_STRUCT` / `RIME_STRUCT_HAS_MEMBER` 做前向兼容（`:62-80`）。

典型嵌入顺序（字段名都来自头文件，非伪代码）：

```c
RIME_STRUCT(RimeTraits, traits);
traits.shared_data_dir = "<安装目录>/data";
traits.user_data_dir   = "%APPDATA%/...";
traits.app_name        = "rime.yantai";
traits.distribution_name = "..."; traits.distribution_code_name = "...";
traits.distribution_version = "0.1.0";
RimeApi* api = rime_get_api();
api->setup(&traits);                    // 设日志、目录
api->set_notification_handler(cb, ctx); // deploy / option 通知
api->initialize(&traits);               // 装载模块
api->start_maintenance(False);          // 有 schema 变更时后台部署
RimeSessionId s = api->create_session();
RIME_STRUCT(RimeContext, ctx);        // 每个输出结构体都要先 RIME_STRUCT_INIT
api->process_key(s, keycode, mask);     // 返回 True = 已消费
api->get_context(s, &ctx);              // composition + menu + commit_text_preview
RIME_STRUCT(RimeCommit, commit);
api->get_commit(s, &commit);
api->free_context(&ctx); api->free_commit(&commit);
api->destroy_session(s); api->finalize();
```

要点（都有源码依据）：

- **键码是 X11/IBus keysym，不是 VK。** 字母就是 `'a'`，特殊键是 `0xFF08`(BackSpace)、`0xFF0D`(Return)、
  `0xFF1B`(Escape)；修饰位 `kShiftMask=1<<0, kLockMask=1<<1, kControlMask=1<<2, kAltMask, kSuperMask=1<<26,
  kReleaseMask=1<<30`（`src/rime/key_table.h:15-43`）。Weasel 有一份现成的 VK→keysym 映射可抄：
  `weasel/WeaselTSF/KeyEvent.cpp:4-60` 与 `weasel/include/KeyEvent.h:35-247` 的 `ibus::Keycode` 枚举。
- **输出的三个结构体**：`RimeComposition{length,cursor_pos,sel_start,sel_end,preedit}`、
  `RimeMenu{page_size,page_no,is_last_page,highlighted_candidate_index,num_candidates,candidates,select_keys}`、
  `RimeCandidate{text,comment}`（`rime_api.h:119-145`）。也就是说 librime 只给"词 + 注释"，
  **没有徽标、来源、英文行、译文这些字段**——那些要么做成 comment 里的约定，要么留在我们 server 层。
- **没有"预编辑文本"以外的编辑模型**：选区是 `sel_start/sel_end`（拼音串里的音节高亮），
  光标用 `get_caret_pos/set_caret_pos`（`:443,452`）。
- **部署是异步的**：`start_maintenance / is_maintenance_mode / join_maintenance_thread`（`:301-304`），
  首次装词库时必须跑，否则 `get_context` 拿不到候选。
- **官方还带 `rime_deployer.exe` 与 `rime_dict_manager.exe`**（发布包 `dist/bin/`，459,776 B / 278,016 B），
  可以在安装器里调用它们预构建，避免首启卡住。

### 1.3 依赖、构建、产物体积

依赖（`librime/.gitmodules`、`deps.mk:1-5`、`CMakeLists.txt:64-150`）：

| 依赖 | 作用 | 说明 |
|---|---|---|
| Boost ≥1.77（Windows README 写 ≥1.83） | regex、algorithm、preprocessor、filesystem | `CMakeLists.txt:64-73`；Windows 侧静态运行库 |
| glog | 日志 | 可用 `ENABLE_LOGGING=OFF` 关掉 |
| leveldb | 用户词库 `*.userdb` | `src/rime/dict/level_db.cc` |
| marisa-trie | 词典 `prism.bin` 双数组 | `src/rime/dict/prism.cc` |
| opencc | 简繁、emoji 转写（`simplifier`） | `src/rime/gear/simplifier.cc`、`opencc.cc` |
| yaml-cpp | schema/config 解析 | `src/rime/config/` |
| googletest | 测试，可关 | `BUILD_TEST=OFF` |

构建方式：**不是 vcpkg 包**（`https://raw.githubusercontent.com/microsoft/vcpkg/master/ports/librime/vcpkg.json`
返回 404），要用自带 `build.bat deps` + `build.bat librime`（`README-windows.md:1-60`），
依赖源码是 submodule，静态链进 `rime.dll`。默认 `BUILD_MERGED_PLUGINS=ON`（`CMakeLists.txt:19`），
所以插件也进同一个 DLL。

实测产物（解包 `rime-33e7814-Windows-msvc-x64.7z`，GitHub release 1.17.0）：

| 文件 | 字节 |
|---|---:|
| `dist/lib/rime.dll` | 3,739,136 |
| `dist/lib/rime.pdb` | 50,098,176（不随产品发） |
| `dist/bin/rime_deployer.exe` | 459,776 |
| `dist/bin/rime_dict_manager.exe` | 278,016 |
| `dist/bin/rime_patch.exe` | 217,600 |

`dist/bin` 里没有任何第三方 DLL，说明依赖是静态链的。**词库构建产物（`build/*.bin`）体积我没有实测，
「未查到」**；能确定的是它由 marisa/leveldb 从 `*.dict.yaml` 构建，和上游词库规模正相关，
不能直接拿我们 152 MiB 的 SQLite 去类比。

源码规模：`src/` 229 个 `.cc/.h`，33,941 行；Weasel 前端 6 个模块合计约 17.5k 行（TSF 7,332 / UI 4,707 /
Server 2,125 / IPC 963 / IPCServer 737 / RimeWithWeasel 1,607）。

**许可证**：BSD-3-Clause（`librime/LICENSE` 首行 "Copyright (c) 2014, RIME Developers"）。
插件同为 BSD-3：`librime-lua/LICENSE`、`librime-octagram/LICENSE`、`librime-predict/LICENSE`。
本仓是 GPL-3.0（`E:/水杉输入法/LICENSE`），BSD-3 代码可以并入 GPL 产品，**无冲突**。

### 1.4 插件：lua 与 octagram

- **librime-lua**：给 Rime 加 processor / segmentor / translator / filter 的脚本层
  （`librime-lua/README.md:1-12`），schema 里写 `lua_filter@*corrector` / `lua_translator@*date_translator`，
  脚本放 `rime.lua`（`librime-lua/sample/`）。内置 lua 5.4 源码可静态编入
  （`librime-lua/CMakeLists.txt:1-40`）。
- **librime-octagram**：把 `.gram` 语法模型接成 `grammar` 组件（`src/grammar_module.cc:11-18`），
  供 `Poet` 用。`.gram` 文件格式：`Metadata{format[32], db_checksum, double_array_size, OffsetPtr<char> double_array}`
  + Darts 双数组（`src/gram_db.h:12-20`），值是 `int(ln(共现频次) * 10000)`（`kValueScale = 10000`，
  `gram_db.h:26`）；查询返回 `max(值/10000 + 惩罚)`，全不命中返回 `non_collocation_penalty`
  （`src/octagram.cc:14-19,107-140`）。默认参数 `max_length=4, min_length=3, penalties -12/-12/-24/-18`。
- **librime-predict**：预测/联想（发布包里也合并了）。

### 1.5 rime-ice / 白霜 / 万象 怎么用模型提升整句

| 方案 | grammar 配置 | 说明 |
|---|---|---|
| 雾凇 rime-ice | **没有 `grammar:` 段**（`rime_ice.schema.yaml` 全文无该键），整句靠词频 + `max_sentences`（注释里写 librime ≥1.17.0）+ 一堆 lua filter | 它的"智能"主要来自 lua（corrector、long_word、reduce_english）和 simplifier，不是语法模型 |
| 白霜 rime-frost | `grammar: language: zh-moqi`、`non_collocation_penalty: -4`；`translator: grammar_penalty: -4, max_sentences: 3`（`rime_frost.schema.yaml:102-105,240-248`） | 注释原话："这个很小几乎不起作用但是不能不配置，为了让 non_collocation_penalty 生效"；真正调的是**组词惩罚** |
| 万象 | `grammar: language: wanxiang-lts-zh-hans`，`collocation_max_length: 6 / min: 2 / penalty: -14 / non: -6 / weak: -100 / rear: -18`（`wanxiang.schema.yaml:116-123`） | 与模型配套的一套参数，和我们的默认值一致 |

模型体积（我们自己的实测，比网上的说法可靠）：
`engine/tests/data/quanpin_sentence_eval_300.results.md` 表里 —— 万象 `wanxiang-lts-zh-hans.gram`
398.7 MiB、进程峰值 268.1 MB、首选 87.50%；八股文词级 196.8 MiB / 79.69%；紧凑版 39.0 MiB / 80.62%；
白霜 7.0 MiB / 74.38%。**`.gram` 是 mmap 的，运行内存远小于文件**（同文件"内存与体积不成正比"一节）。

对我们最重要的一条：**我们已经有 `.gram` 读取端**——`engine/ngram/octagram/octagram_gram.h:1-31`
明确写着"格式与查询语义按 lotem/librime-octagram 移植"，参数默认值也照万象。也就是说
若改用 librime，这份模型文件可以原样继续用（给 librime-octagram），**模型不是迁移成本**。

### 1.6 用户词库：dee / tick 算法与迁移

librime 的用户词库是 leveldb，一条记录是 `key = "拼音 \t 词"`、`value = "c=<commits> d=<dee> t=<tick>"`
（`src/rime/dict/user_db.cc:18-30` 的 `UserDbValue::Unpack/Pack`，结构体在 `user_db.h:20-33`）。

更新规则（`src/rime/dict/user_dictionary.cc:783-840`）：

- 每次学习 `++tick_`，并把新 tick 写进记录；
- 选中一次：`commits += 1`，`dee = formula_d(commits, tick, dee, old_tick)`；
- `commits == 0` 的"只衰减"更新用 `k = 0.1`；
- 删除：`commits = min(-1, -commits)`，dee 按 0 衰减，读的时候 `commits < 0` 直接跳过；
- 查询时若 `tick < present_tick`，先补衰减 `formula_d(0, present_tick, dee, tick)`，再算
  `weight = formula_p(0, commits/present_tick, present_tick, dee)`，取 `log(weight)` 参与排序
  （`user_dictionary.cc:426-440`、`988-1010`）；
- 衰减到 `dee <= 1e-200` 的历史条目会被丢弃（`user_dictionary.cc:984,1005`）。

两个公式在 `src/rime/algo/dynamics.h:6-14`：

```
formula_d(d, t, da, ta) = d + da * exp((ta - t) / 200)
formula_p(s, u, t, d)   = m + 修正项，m = s - (s-u) * (1 - e^(-t/10000))^10
```

**迁移含义**：我们现在的用户词库是 SQLite，带 `weight`、`selection_count`、
`fixed_candidate_positions`、快捷短语、英文槽位（`engine/user_dictionary/user_dictionary_journal.cpp:206-235`）。
能对上 userdb 的只有 `(code, word, 次数)`；**置顶/固定位置/每候选选择计数没有对应物**。
官方迁移通道是文本快照：`code \t phrase \t c=.. d=.. t=..`，由 `rime_dict_manager.exe -e <dict> out.txt`
导出、`rime_dict_manager.exe -i <dict> in.txt` 导入（`librime/tools/rime_dict_manager.cc:35-38,80-110`；
解析/写回在 `user_db.cc:71-125` 的 `userdb_entry_parser/formatter` 与 `UniformBackup/UniformRestore`）。
如果不想丢语义，另一条路是**保留 SQLite 作为权威库**，写自定义 translator/filter 去读它——
零迁移，但等于把用户词库那套逻辑再实现一遍。

---

## 2. Weasel（小狼毫）作为参照

### 2.1 进程与 IPC

- **两个进程**：`WeaselTSF.dll` 注入宿主（`WeaselTSF/Server.cpp` 的 `DllGetClassObject`），
  `WeaselServer.exe` 独立进程持 librime、托盘、候选 UI（`WeaselServer/WeaselServerApp.cpp:1-70`
  里 `m_ui.Create(m_server.GetHWnd())`、`m_handler->Initialize()`）。
- **IPC 是命名管道 + 消息包 + boost 序列化**：`PipeChannel<Msg,Res>` 的 `Transact` 一次写一收
  （`weasel/include/PipeChannel.h:120-125`），写入后 `FlushFileBuffers`（`WeaselIPC/PipeChannel.cpp:76`）。
  每次按键 = 一次同步往返（`RimeWithWeasel.cpp:266-293`：`process_key` → `_Respond` → `_UpdateUI`）。
- **回包里塞候选串**：`_Respond` 把 `ctx.preedit`、`status.*` 和 `CandidateInfo` 用
  `boost::archive::text_woarchive` 序列化回客户端（`RimeWithWeasel.cpp:748-900`）。
- **管道 ACL 专门兼容 UWP / IE 保护模式 / 提权进程**：SDDL 里低完整性标签 `NO_WRITE_UP` +
  `Everyone` + `ALL_APP_PACKAGES(AC)`（`WeaselIPCServer/SecurityAttribute.cpp:61-72`）。
  同文件注释给出参考链接（StackOverflow: accessing named pipe servers from IE EPM BHO，`SecurityAttribute.cpp:61-72`）。
- **体积参照**：`weasel-0.17.4.0-installer.exe` = 12,431,118 B（GitHub release 0.17.4 资产）；
  我们当前 dev 安装包 = 29,158,232 B。Weasel 是 GPL-3.0（`weasel/LICENSE.txt`），与本仓同许可，
  参考/移植其 TSF 代码没有额外许可障碍。

### 2.2 候选窗绘制

- **DirectComposition + DXGI swapchain + Direct2D/DirectWrite**：窗口样式
  `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP`
  （`WeaselUI/WeaselPanel.cpp:374-377`），`DCompositionCreateDevice`（`WeaselUI/d2d.cpp:76`）。
- **两套候选 UI 并存**：TSF 侧 `CCandidateList`（实现 `ITfCandidateListUIElement(Behavior)`，用
  `ITfUIElementMgr::BeginUIElement` 注册，`WeaselTSF/CandidateList.cpp:286-320`）在宿主进程内画；
  server 侧 `weasel::UI` 也建了一个面板，`InServer()` 参与决定 ASCII/半角等状态提示画不画
  （`WeaselUI/WeaselPanel.cpp:280-285`），也用来显示消息（`RimeWithWeasel.cpp:546-549`）。
  候选数据本身是回传给客户端的（§2.1），所以**日常绘制在宿主进程内**。
- **定位**：靠 TSF 的异步 edit session 里 `GetTextExt` 拿光标矩形
  （`WeaselTSF/Composition.cpp:137-244`），再 `UpdateInputPosition`；落点由
  `WeaselPanel::_Reposition` 用 `MonitorFromRect` + `rcWork` 夹取，放不下就翻到光标上方
  （`WeaselPanel.cpp:825-891`）。

**和我们的差别（重要）**：我们这一支是 **server 进程画候选窗**：`windows/` 只链 `fmt`
（`windows/CMakeLists.txt:188`），TSF 通过 `SendShowCandidateWndEventToUIProcess` /
`SendMoveCandidateWndEventToUIProcess` 把显示/移动事件发给 server（`windows/src/IPC/Ipc.cpp:1331-1338`），
server 侧 `server/src/window/` + `ui/`（msimeui + d3d11/dcomp，`server/CMakeLists.txt:422-427`）画。
好处是候选窗只有一份代码、跨宿主表现一致；代价是**锚点要跨进程传**（TSF 拿 `GetTextExt`，server 画），
`候选窗文字抖动修复.patch` 就是在修 D2D 这一层。Weasel 的"宿主内画"不存在这个跨进程锚点问题，
但它有另外一堆位置/DPI issue（§2.3）。

### 2.3 已知痛点（带 issue 号）

| 类别 | 证据 |
|---|---|
| 候选窗上下抖 1px | rime/weasel#913「部分情况下，Word 中候选框会发生上下抖动」 |
| 光标/位置漂移 | #1327「Win11 Edge/Chrome 中打字总是从光标位置左边飞入输入框」；#1651「多屏时候选窗可能先在主屏幕出现然后闪现到当前屏幕」 |
| 混合 DPI | #367「多屏且分辨率不同，高分屏候选框太小」；#1971「fix(ui): fix candidate position and DPI scaling on mixed-DPI screens」（PR） |
| Chromium / 浏览器 | #997「高优先度接管输入事件导致 Chrome 快捷键失效」；#1413「輸入法在 Chrome 使用一段時間會卡頓」；#605「VS Code 终端无法输入中文」 |
| UWP / Metro | #106「Fix compatibility with metro apps and UWP」（PR）；#1499「fix(tsf): ime status」；#1579「某些窗口需先输英文字符才弹候选」 |
| 游戏/全屏 | #321 Dota2；#1010 Fortnite；#1500 FF14 掉帧 |
| 性能/卡顿 | #1250「关于一部分 weasel 用户卡顿的原因和改进建议」（CPU 降频，非 IME 计算）；#1478「小狼毫+白霜词库打字卡顿」 |
| 缓存/体积 | #532「小狼毫 0.14.3 缓存文件十分庞大」 |
| 杀软误报 | #914「0.15.0 安装程序被 Symantec 识别为木马」 |

**管理员程序**：管道 ACL 那条（§2.1）说明设计上是允许高完整性客户端写入的；但我**没有查到**
一条被确认的"管理员权限程序里小狼毫不能用"的 issue，也**没有实测**，所以这里不下结论（「未查到」）。

### 2.4 有没有更轻的 Windows 前端可参考

- **libIME2**（`EasyIME/libIME2`，LGPL-2.1）——把 TSF 那套样板封装成库，墨奇输入法就是
  "TSF + libIME2 + librime + Go 后端"（`moqi/README.md:5-9`）。
- **墨奇 moqi-im-windows**（C++，白霜作者）：TSF 前端 + librime，自带候选窗与主题；README 自述
  "很大程度上参考了 PIME 以及小狼毫"。可直接看它怎么把 librime 塞进 TSF。
- **WindInput 清风输入法**（Rust，GPL-3.0，Windows/macOS/Linux）——自带引擎与方案文件，
  README 明确把 Weasel 当"Windows TSF 集成语义的对照实现"；是"更轻"的现实样本。
- **XimeYao**（Rust + Rime）、**TypeAnything / ContextIME / inklink** 等多为 Weasel fork。

结论：**没有比 Weasel 明显更轻、又成熟可直接复用的 TSF+librime 前端**；最轻的组合就是
我们已有的 TSF（2.4 MB）+ 一个薄 server + `rime.dll`（3.6 MB）。

---

## 3. 把后端换成 librime：要改哪些接口

### 3.1 现状（都已核对）

| 层 | 位置 | 与引擎的关系 |
|---|---|---|
| TSF DLL | `windows/`（29.6k 行） | 只 include `engine/contracts/*.h`（IPC 线协议、翻译模式、UILess 页），**不链接引擎** |
| Server | `server/`（68k 行） | `CreateInputSessionFromConfig()` 按配置字符串造 `EngineInputSession`（`server/src/session/session_factory.cpp:35-57`） |
| 会话接口 | `server/src/session/input_session.h` | 38 个纯虚函数，是唯一的后端缝 |
| 候选窗 | `server/src/window/` + `ui/`（18.2k 行） | 拿 `CandidateViewItem`（`server/src/window/candidate_view_model.h:10-30`），不碰引擎 |
| 引擎 | `engine/`（62.7k 行） | 全拼解码、词格、kenlm、octagram、神经重排、SQLite 用户词库 |

**关键结论：缝在 `IInputSession`。** 实现一个新的 `RimeInputSession : IInputSession`，
在 `session_factory.cpp` 里加一个 `backend == "rime"` 分支即可；TSF 与候选窗零改动。

### 3.2 方法映射（逐条）

已有对应（直接调 librime）：

| IInputSession | librime |
|---|---|
| `handle_key(vk, mods, wch)` | `process_key(session, keysym, mask)`（VK/字符→keysym 抄 Weasel `KeyEvent.cpp`） |
| `get_candidates()` | `get_context()` → `menu.candidates[]` + `comment` |
| `get_pinyin_sequence()` / `with_cases` | `ctx.composition.preedit`（大小写信息要看 raw input / `get_input`） |
| `select(index)` | `select_candidate_on_current_page()` |
| `reset_state()` | `clear_composition()` |
| `set_caret(pos)` / `prefix_end()` | `set_caret_pos()` / `get_caret_pos()` |
| `store_user_phrase(pinyin, word)` | 造词要走 `user_dictionary` 的 `UpdateEntry`（无公开 C API，需自定义 translator 或直接写 userdb） |
| `remove_candidate` | `delete_candidate_on_current_page()`（语义只覆盖"当前页"） |
| 标点 | `punctuator` / `punct_translator`，由 schema 配置 |
| 简繁 | `simplifier` + OpenCC（我们现在是 `server/src/conversion/` 自己的表） |

**没有对应、必须自己补或砍掉**：

| 能力 | 现状 | 换 librime 后 |
|---|---|---|
| 候选来源/徽标（`CandidateSource`：Database/Cloud/Ai/Emoji/Collocation/Neural…） | 引擎给 | librime 只给 text/comment；要么编码进 comment，要么在 server 侧重判 |
| 首页槽位策略（首选/emoji/英文/字母词的位置） | `server/src/ipc/ordinary_slot_placement_policy.h` 等按 source+weight | 权重语义变了（`log(prob)` vs 我们的 `weight`），要重标定 |
| 固定位置 / 置顶（`fixed_position`） | 引擎 + SQLite 表 | 无对应，得留在 server 侧另存 |
| 前缀补全、字母词、拆字反查（U 模式）、日期时间 | 引擎与 server 混着 | 部分能用 `affix_segmentor`/`lua_translator` 复刻，部分要重写 |
| 云候选 / AI / 在线翻译 | server 层 | **原样保留**（不经过引擎） |
| 英文行 / 中译英释义（`english_gloss`） | `server/src/english/` + mixed | **原样保留**（server 层，候选附注） |
| emoji / 颜文字 / 符号 | `server/src/mixed/mixed_candidates.cpp` + `others.db` | **原样保留** |
| 候选翻译面板 | `server/src/translation/` + `candidate_translation_panel.h` | **原样保留** |
| 神经整句重排 | `engine/neural/` | **丢掉**（librime 没这个口子；除非自己写 translator） |
| kenlm `sc.lm` 词格 | `engine/ngram/language_model.*` | **丢掉**（librime 只有 octagram 搭配模型） |

也就是说：**翻译/emoji/英文行能保住，整句的"神经那 5 个点"保不住**。

### 3.3 用户词库迁移

三条路，按代价排：

1. **导出成 userdb 文本再导入**（`code \t phrase \t c=.. d=.. t=..`，`rime_dict_manager`）。
   最省事，但 `weight` 要折成 `commits/dee/tick` 的近似，**置顶、固定位置、英文/emoji 学习记录丢失**。
2. **保留 SQLite 为权威库，写自定义 translator/filter** 读它。零迁移、语义完整，但要实现
   "用户词参与整句/排序"的 Rime 侧逻辑，工作量约等于把现在 `user_dictionary` 那层搬一次。
3. **双写**：新词进 userdb，旧词一次性导入，两套并存一段时间。工程上最稳，代码最脏。

注意 librime 的 `dee/tick` 是**全局单调 tick**，不是时间戳：`++tick_` 存在 userdb meta 的 `/tick` 里
（`user_dictionary.cc:783-840` 的 `db_->MetaUpdate("/tick", ...)`）。迁移时 `t` 只能造一个递增序列，
不能用真实时间。

另外，**快捷短语不用迁移**：librime 的 `table_translator@custom_phrase` + `db_class: stabledb/tabledb`
就是吃 `custom_phrase.txt` 的只读/可调频短语表（rime-ice 的做法，见 `rime_ice.schema.yaml` 的
`custom_phrase:` 段）。我们 `quick_phrase_slots` 那张表可以导出成这份 txt。

### 3.4 工作量估计（估算，非实测）

前提：复用现有 TSF、IPC、D2D 候选窗、server 策略层；只做全拼单方案；不做神经/kenlm。

| 工作项 | 估计 | 依据 |
|---|---:|---|
| `RimeInputSession` 骨架 + 键映射 + 候选/预编辑/选择 | 5–8 天 | 38 个虚函数；现成 `EngineInputSession` 318 行可当模板；Weasel 的键映射可抄 |
| 数据管线：SQLite 词库 → `*.dict.yaml` → 安装时 `rime_deployer` 预构建（prism/table/reverse） | 5–10 天 | librime 只能吃 yaml/自建 bin；我们 152 MiB / 202 万条要重新构建并校验 |
| 特性对齐：来源/徽标、首页槽位、简繁、标点、字母词、U 模式 | 10–15 天 | 这些都写在 server 策略里，且依赖 source/weight 语义 |
| 用户词库（迁移或桥接）+ 学习/删除/固定位置 | 5–10 天 | userdb 无固定位置/每候选计数 |
| 评测与回归（320 句集、`bench_input_session`、TSF 兼容矩阵） | 5–10 天 | 现有工具可直接复用 |
| **合计** | **约 6–10 周** | 单人、熟手、不含发布与签名 |

对照：继续删水杉新引擎（日语/五笔/辅助码/双拼）在 `slim-notes.txt` 里被记为"改公共契约 + 改行为"的量级，
双拼 124 个文件触点、辅助码 120、五笔 68，且日语一轮未完成即回滚。**换后端的边界比删特性清楚。**

### 3.5 主要风险

1. **整句质量回退**（高）：失去 kenlm 词格与神经重排，只剩词频 + `.gram`。我们实测单 `.gram` 87.5%、
   神经组合 92.8%；librime 若走同样模型，量级应当接近 87%，但**未实测**。
2. **用户词库语义丢失**（高）：`dee/tick` 与我们的 `weight` 不是一回事；固定位置/置顶无对应。
3. **首次部署**（中）：librime 必须在安装或首启构建 `build/*.bin`，大词库耗时长；不预构建就会首启卡住。
   我们现在的做法是直接发 SQLite。
4. **构建链**（中）：需要 Boost ≥1.83（Windows）、自带 `deps.mk` 静态依赖、无 vcpkg port；
   CI 要新增一套构建与缓存。
5. **稳定性/杀软**（中）：leveldb 多进程锁与崩溃恢复、userdb 损坏修复（`user_db_recovery_task.cc`）、
   Weasel 有过杀软误报（#914）与缓存膨胀（#532）。
6. **候选语义重标定**（中）：`WordItem.weight` 是整数权重，librime 是 `log(prob)`；
   排序、首页槽位、前缀补全的阈值全部要重调。
7. **API/ABI**（低）：`RimeApi` 用 `data_size` 版本化，插件合并进 DLL；升级要重建而不是换 DLL。
8. **许可**（低）：librime 与插件都是 BSD-3，可并入 GPL-3.0；若引入 libIME2 则要注意 LGPL-2.1。

---

## 4. 建议的下一步（如果要继续）

1. **两周 spike，不动产品代码**：在 `E:/yantai-wt/engine-probe` 里写一个最小 `RimeInputSession`
   （只做 handle_key / get_candidates / preedit / select），接 rime-ice 与白霜词库，
   用 `engine/tests/data/quanpin_sentence_eval_300.tsv` 跑首选率、用 `bench_input_session` 跑每键延迟。
   这一步能把"87% 还是 92%"和"每键多少 ms"从猜测变成数字。
2. 数字能接受，再决定用户词库走"文本导入"还是"SQLite 桥接"。
3. 不建议重写 TSF；`windows/` 与 `ui/` 原样保留是这次方案最大的确定性来源。

---

## 5. 没查到 / 没验证的

- librime 每键延迟、常驻内存：**未实测**（没有在本机跑 librime）。
- librime 用同一份万象 `.gram` 在我们 320 句集上的首选率：**未实测**。
- librime 构建后的词库 `.bin` 体积：**未查到**（未构建）。
- 小狼毫在管理员权限程序中的实际表现：**未查到已确认 issue，未实测**。
- Weasel 官方是否发布"轻量版"或裁剪构建：**未查到**。
- 我们 61 MB 安装包口径（`docs/installation.md:19`）与实测
  `安装包/MetasequoiaIME_Setup_v0.1.0-dev.20261108.207698f59275.exe` 29,158,232 B 不一致，
  未查明差异来源（可能是文档未更新或打包内容不同）。

## 6. 本次读过的关键文件

- `E:/yantai-wt/engine-probe/librime/src/rime_api.h`、`src/rime/engine.cc`、`src/rime/gear/{poet.cc,grammar.h,gears_module.cc}`、
  `src/rime/filter.h`、`src/rime/dict/{user_db.h,user_db.cc,user_dictionary.cc}`、`src/rime/algo/dynamics.h`、
  `CMakeLists.txt`、`deps.mk`、`README-windows.md`、`LICENSE`
- `E:/yantai-wt/engine-probe/librime-octagram/src/{octagram.cc,gram_db.h,grammar_module.cc}`、
  `librime-lua/{README.md,CMakeLists.txt}`
- `E:/yantai-wt/engine-probe/weasel/{WeaselTSF/*,WeaselServer/*,WeaselIPC/*,WeaselUI/*,RimeWithWeasel/*,include/{PipeChannel.h,KeyEvent.h,WeaselUI.h}}`
- `E:/yantai-wt/engine-probe/moqi/README.md`
- `E:/水杉输入法/{windows/CMakeLists.txt,windows/src/IPC/Ipc.h,server/CMakeLists.txt,server/src/session/*,server/src/ipc/event_listener_candidates.cpp,server/src/window/candidate_view_model.h,engine/ngram/octagram/octagram_gram.h,engine/tests/data/quanpin_sentence_eval_300.results.md,docs/*.md,slim-notes.txt}`
- 发布页：`https://github.com/rime/librime/releases/tag/1.17.0`、`https://github.com/rime/weasel/releases/tag/0.17.4`
- 方案：`https://raw.githubusercontent.com/iDvel/rime-ice/main/rime_ice.schema.yaml`、
  `https://raw.githubusercontent.com/gaboolic/rime-frost/master/rime_frost.schema.yaml`、
  `https://raw.githubusercontent.com/amzxyz/rime-wanxiang/wanxiang/wanxiang.schema.yaml`
