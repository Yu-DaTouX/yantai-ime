# 水杉新一代引擎（msime）与语言模型（chinese-ime-lm）调研

调研日期 2026-11-08。阅读对象：`metasequoiaime/msime`（浅克隆到 `E:/yantai-wt/engine-probe/msime`，237 MB）、
`metasequoiaime/chinese-ime-lm`（`E:/yantai-wt/engine-probe/chinese-ime-lm`，14 MB）、
`metasequoiaime/msime-docs`、官网 msime.app、GitHub Releases / Issues API，以及我们自己的 fork `E:/水杉输入法`（v0.13.2 基线）。
下文凡写 `msime:路径` 都指 `E:/yantai-wt/engine-probe/msime/路径`，`lm:路径` 指 chinese-ime-lm 仓库内路径。

---

## 0. 结论

**「比肩商业输入法」这句话在官方渠道查不到原文，也没有任何一份把水杉和搜狗／微软拼音／QQ 输入法放在一起的评测。**
官方唯一有数字的评测是 chinese-ime-lm 的整句重排评测，比的是开源模型（青简、知意、四个 Qwen），
而且它自己写明：52 条样本上名次差异全在噪声里，**扩大到 1196 条带上文的用例后，自训模型在整句和选词上显著落后于这些开源对手**（87.0% 对 91–94.6%）。
所以这句话不能作为选型依据，能用的只有"模型体积小"（4.5 MB）和"离线、许可干净"这两条。

**技术上可行，但"基于 msime 引擎做轻量 Windows 全拼"不比继续精简我们现有 fork 更轻。**
新引擎是旧 C++ 引擎的 Rust 移植（同一套算法、同一批金样），神经整句／调频／纠错我们 fork 里已经有；
新增的实质上是"可嵌入"这件事本身：MPL-2.0 授权、38 个函数的 C ABI、crates.io 发布。
代价是 11 MB 的引擎 DLL + 78–102 MB 的词库、C ABI 里没有神经重排、官方 Windows 宿主安装包 172 MB（我们 fork 的 v0.1.0 是 80 MB，上游正式版 v0.13.2 是 61 MB）。

**要做的话，最省的路线是：`msime-engine-capi` 预编译库 + 自写 TSF DLL + 自绘候选窗**，
估计 40–85 人日（单人 2–4 个月），最大风险不在引擎而在 TSF 兼容性和词库许可。

---

## 1. 「比肩商业输入法」出自哪里

### 1.1 原文：未查到

查过的地方与结果：

| 位置 | 结果 |
| --- | --- |
| msime 仓库全部 Markdown（含 `docs/`、`crates/*/README.md`、`platforms/*/README.md`） | 无「比肩」「媲美」「商业输入法」的质量类表述；`商业输入法` 只出现在设计笔记里当作界面参照（如九键键位照某商业输入法截图核对，`msime:.agents/notes/implemented/feature/2026-10-10-nine-key-shuangpin.md:23`） |
| msime-windows / msime-docs 仓库 | 无 |
| 官网 <https://msime.app/>、`/features`、`/about`、`/engine/`、`/model/`、`/download/` | 无。`/model/` 反而明确写「样本只有 60 句……不能当成质量排名」 |
| 用户文档 wiki `github.com/metasequoiaime/msime-windows/wiki` | 无 |
| GitHub 搜索 API：`比肩`、`媲美`、`商业输入法` 于 `org:metasequoiaime` | `比肩` 0 条；`媲美` 返回异常；`商业输入法` 25 条，逐条看标题与命中的 issue #10879 正文，都是词库自动改进方案里的表述，不含该说法 |

**结论：逐字来源未查到。** 下面给出官方实际说过的、最接近的话，以及它背后的评测。

### 1.2 官方原话（有链接）

- 官网首页（<https://msime.app/>）：「神经联想——4.5 MB 起的语言模型看着前文，在整句候选里挑出更通顺的读法，模型和代码都开源。」
- 神经联想页（<https://msime.app/model/>）：「打一整句拼音时……每次按键都跑的快速模型只有 4.5 MB，手机键盘也装得下；电脑上在输入停顿后接手的高精度模型 25.5 MB。」
  同页给的评测表（60 句手写用例中 56 句的整句首选是引擎组出的句子）：

  | 配置 | 首选正确 / 56 | 说明 |
  | --- | --- | --- |
  | 引擎自己的首选 | 41 | |
  | 加 4.5 MB 快速模型 | 49 | 救回 10 句，弄错 2 句 |
  | 加 25.5 MB 高精度模型 | 52 | 救回 11 句，没有弄错 |
  | 候选里本来就有正确答案 | 52 | 任何重排的上限 |

- 引擎库页（<https://msime.app/engine/>）：「水杉的输入引擎可以单独嵌进别的输入法……开箱即用的引擎库，不是 Rime 那样靠方案配置驱动的框架。」

也就是说，官方主张是**「小模型 + 可嵌入 + 开源」**，不是"达到商业输入法水平"。没有任何一处把水杉和商业输入法做过比较。

### 1.3 评测怎么做的

来源：`lm:docs/measurements.md`（579 行，写得异常坦白）、`lm:eval/`、官网 `/model/` 的图表数据。

**数据集**

| 集合 | 规模 | 来源 |
| --- | --- | --- |
| 整句集 `sentences-v1` | 60 条手写整句，56 条引擎整句首选可用，52 条金标准落在候选池内 | `lm:eval/sentences-v1.tsv`（74 行，含表头/注释） |
| 词级集 `quanpin-words-v1` | 25,119 条 | `lm:eval/quanpin-words-v1.tsv`（25,124 行） |
| 带上文集 `sentences-ctx-v1` | 6,317 条：1,631 条整句（其中 1,196 条有上文）+ 4,686 条选词（全拼 2,356 / 声母 1,043 / 混打 1,287） | `lm:eval/sentences-ctx-v1/pool.jsonl`（6,317 行）；语料为 55 份国务院及部委文件 + KdConv/CrossWOZ/RiSAWOZ 三个对话集测试集，见 `lm:docs/measurements.md:216-218` |

**对比对象（关键）**：四个开源通用模型（Qwen2.5-0.5B、Qwen3-0.6B、Qwen3.5-0.8B、Qwen3.5-2B）+ 两个开源输入法自带的重排模型（青简的含章·通变、含章·知微；知意的两个版本）。
**没有搜狗、微软拼音、QQ 输入法、百度输入法，一家商业输入法都没有**（`lm:docs/measurements.md:80-136` 全表）。

**指标**：池内选中金标准的条数（配对比较用 McNemar 精确检验，区间用 Wilson 95%）；耗时是"一次决策"（给一个池打分一次）的中位数；体积是权重文件实际大小。
救回与弄错分开统计（`lm:docs/measurements.md:63-79`）。

**数字（52 条池，2026-10-30 重测）**：引擎原序 41/52（78.8%）；本项目 4.25M 49（94.2%）；24.9M 52（100%）；含章·通变 50、知微 49；知意 51/49；四个 Qwen 42–52（随打分细节跳，`lm:docs/measurements.md:180-195`）。

**方法是否站得住**

站得住的部分：
- 候选池固化在 `lm:eval/open-models/pool.jsonl`，逐例选择在 `picks.json`，权重哈希在 `models.json`，可复现；口径（只取覆盖全键、来源 `generated`/`fallback` 的候选）与运行时门控一致，而不是用答案长度作弊（`lm:docs/measurements.md:49-62`，专门写了两个测量陷阱）。
- 样本量不足时自己承认：「52 句上，一方至少要净多对 6 句才可能显著……这十个之间最多差 3 句，所以两两之间没有一对的差别达到显著」，并明确「不能当成质量排名」（`lm:docs/measurements.md:137-180`）。
- 只挑有利场景这件事上反而做过头了：Qwen 那一列取的是"对它们最有利"的打分口径。

站不住的部分（也就是"比肩"站不住的原因）：
- **60 条样本的绝对数字没有统计意义**。52/56 的 Wilson 95% 区间约 83%–97%（±7 个百分点），41 比 49 这类差距更宽。
- **和商业输入法零对比**。商业输入法的端到端准确率既没测，也没有任何第三方对比。
- **作者自己在 6,317 条上推翻了自己**：`lm:docs/measurements.md:216-245`，1,196 条有上文的整句上，24.9M 是 87.0%，含章两款 91–92%，知意 93.5–94.6%，Qwen 96.2–97.7%，**配对检验全部 p<0.0001**；选词三档（全拼/声母/混打）都显著低于知意和 Qwen（90.9% 对 92.3–94.1% 等）。435 条无上文的整句上差距消失（分不开）。
  原文：「**在同一个池里挑，有上文时知意、含章和通用的 Qwen 都比 24.9M 挑得准**……『排序质量不输』不能再说。」（`lm:docs/measurements.md:245`）
- 原因也查出来了：模型字表里没有标点，整句用例的上文几乎全以 `<unk>` 结尾（1192/1196），`lm:docs/measurements.md:281-304`。

**一句话**：官方评测做得比大多数开源项目严谨（承认噪声、承认上限、给复现材料），但它衡量的是"**这个小模型在给定候选池里挑得怎么样**"，不是"这个输入法能不能和商业输入法比"。按它自己的最新数据，在有上文的场景里它是落后的一方。

---

## 2. 引擎架构

### 2.1 仓库形态

`metasequoiaime/msime` 是跨平台主仓（Rust workspace + 各平台原生宿主），**不是** `MSIME-Engine` 那个独立引擎仓的延续。
原独立 C++ 引擎仓（`metasequoiaime/MSIME-Engine`，我们 fork 里的 `engine/` 就来自它，`E:/水杉输入法/engine/UPSTREAM.md`）已不再被新仓引用：

- 顶层：`crates/`（Rust）、`platforms/{android,ios,macos,linux,windows,harmony}`、`apps/desktop`、`packages/ui`、`shared/`、`plugins/`、`resources/`、`tools/`、`docs/`
- workspace 成员见 `msime:Cargo.toml`，`default-members` 排除了桌面壳。

### 2.2 核心模块

| 模块 | 职责 | 代码量 |
| --- | --- | --- |
| `crates/engine`（`msime-engine` 0.2.0） | 纯 Rust 输入引擎：组字状态机、10 种方案、词格解码、纠错、学习回放 | 191,614 行 / 364 个 .rs（含大量测试） |
| `crates/engine-capi`（`msime-engine-capi` 0.1.2） | 引擎的 C ABI，38 个带版本函数，cargo-c 出 so/dylib/dll + 头 + pkg-config | 4,103 行 |
| `crates/host-api` | 版本化 C 接口 + 线程绑定会话 + 显式释放，六个平台宿主都链它（含产品胶水：配置、词库管理、翻译、统计） | 101,292 行 |
| `crates/input-runtime` | 会话编排、焦点取消、候选分页、带代次的选择 | 27,135 行 |
| `crates/onnx-rerank` | ONNX Runtime 后端的逐键重排（可选 feature） | — |
| `crates/host-windows` | 只是 Win32 能力包装（SendInput、剪贴板、窗口定位），不含引擎 | 2,276 行 |

引擎内部分区（`msime:crates/engine/src/`）：`session/`（组字、候选、学习、预测、编辑）、`lattice/`（词格解码、bigram/trigram/octagram）、
`pinyin/`（切分、模糊音、纠错 `autocorrect*`、typo 表）、`dictionary/`、`user_dictionary/`（日志、回放、调频、个人词条）、
`quanpin/ shuangpin/ wubi/ zhuyin/ cantonese/ japanese/ korean/ vietnamese/ tibetan/ stroke/ nine_key/`、`local/`（日期、拆字、符号、单位换算）、`handwriting/`（Zinnia 移植）。

### 2.3 对外接口：能不能脱离宿主单独嵌入

**能，而且这是官方明确的设计目标。** 两条路（`msime:docs/ARCHITECTURE.md` 的分层图、`msime:crates/engine-capi/README.md`）：

1. **Rust**：`cargo add msime-engine`，公开面是 `Session` / `SessionOptions` + `prepare_runtime_paths`。
   `host` / `ordering` / `backend` / `web` 是水杉自己的产品门面，不进文档、不建议外部依赖。
2. **C ABI**（`crates/engine-capi`）：`cdylib` + `staticlib`，头文件 `msime_engine.h`（36 KB），**38 个函数，ABI 1.1**。
   典型调用：`msime_engine_prepare_runtime` → `session_new(JSON 选项)` → `key_char` / `key_punct` / `command` → `state_get` → `candidate_get` → `select` → `session_free` → `flush_learning`。
   JSON 只在冷路径（建会话、个人词典、调试快照），按键路径没有 JSON。

**重要的能力缺口**（`msime:README.md`「在你的输入法里使用水杉引擎」、`crates/engine-capi/README.md` 开头、官网 `/engine/`）：

- C ABI 1.1 **不含神经重排**、不含搭配（octagram）模型、不含触点纠错、不含九键/14 键、不含整句的其余读法（只给最好的那一种）、不含 `/` 指令和 `@` 名单。
- 想要神经整句就只能用 Rust crate（并自己在 TSF 侧搭桥），或者自己扩 ABI。
- 引擎有一条使用约束：同一个会话的调用不能并发（库内只 `try_lock`，冲突返回 `BUSY`），但可以在不同线程间迁移；一个进程一份，官方明说**不要 `dlclose` 后重载**。

**约定上的坑**：`rusqlite` 用 `bundled`，`libsqlite3-sys` 声明了 `links = "sqlite3"`，下游工作区必须把 rusqlite 对齐到 0.40；
`chinese-ime-lm` 在 dev 构建里会让候选窗卡几秒，下游要自己在工作区写 `[profile.dev.package.chinese-ime-lm] opt-level = 3`（`crates/engine/README.md`「从 crates.io 使用」）。

### 2.4 依赖

引擎 crate 直接依赖（`msime:crates/engine/Cargo.toml`）：`msime-path-trust`、`chinese-ime-lm 0.2`、`rusqlite`(bundled)、`memmap2`、`lru`、`serde/serde_json`、`exmex`、`rink-core`（单位换算，MPL-2.0）、`chinese-number`、`ewts`、`vi`、`lunar-lite`、`time`、`sha2`、`libc`。
workspace 还有 tokio、tauri、ort（可选）等，但都不在引擎的编译图里。**不需要 C++ 工具链**（原来的 C++ 引擎要 boost/fmt/spdlog/sqlite）。

### 2.5 体积（实测，非估算）

预编译 C 库归档 `msime-engine-capi-0.1.2-windows-x86_64-msvc.zip`（19.43 MB，GitHub Release `msime-engine-capi-v0.1.2`，2026-10-29）解开后：

| 文件 | 大小 |
| --- | --- |
| `bin/msime_engine.dll` | **11,498,496 字节 ≈ 11.0 MiB** |
| `lib/msime_engine.lib`（静态库，含 Rust std + bundled SQLite） | 56,166,604 字节 ≈ 53.6 MiB |
| `bin/msime_engine.pdb` | 4,665,344 字节 |
| `include/msime_engine.h` | 35,930 字节 |
| `THIRD_PARTY_NOTICES.txt` | 665,518 字节 |

动态库链接动态 CRT（要 VC++ 2015–2022 运行库）。Linux 归档 18.65 MB、macOS universal 34.45 MB。

### 2.6 词库与模型体积、许可

词库来自 `metasequoiaime/msime-dictionary` 的 `dict-v*` 发布，当前锁在 `dict-v2.1.2`（`msime:resources/desktop-dictionary.lock.json`）：

| 文件 | 体积 | 许可 |
| --- | --- | --- |
| `msime-pinyin.db` | 73,891,840 B ≈ 70.5 MiB | GPL-3.0（rime-ice + 补充）；与万象共有词条的权重改编自 rime-wanxiang，CC BY 4.0 |
| `msime-english.db` | 4,485,120 B ≈ 4.3 MiB | GPL-3.0（rime-ice-en + SCOWL 词表） |
| `msime-bigram.bin` / `msime-trigram.bin` | 12,000,016 B ×2 ≈ 22.9 MiB | 计数来自 zhwiki，CC-BY-SA-4.0 |
| `msime-others.db`（emoji/颜文字） | 1,323,008 B | 来源未记录，**再分发前要另行确认** |
| `msime-wubi.db` | 142,508,032 B ≈ 136 MiB | 混合五种许可 |
| `msime-japanese.dat` | 66,547,565 B | Mozc / BSD-3-Clause + 词典附加条款 |

全拼最小集 = pinyin + english + 两个 n-gram ≈ **102 MB 未压缩**；C 库发布里打好的数据包 `msime-engine-data-quanpin-2.0.41.zip` 是 53.28 MB。
**n-gram 可以省**：官方说表缺失时引擎不报错，只是整句路径不加权、顺序变差（`msime:README.md`「固定词库资源」），省 22.9 MB。

模型（`msime:resources/neural-model.lock.json`、`resources/rerank-model.lock.json`）：

| 模型 | 体积 | 许可 |
| --- | --- | --- |
| `model-v1` 键盘 `sentence-model.safetensors` | 4,486,280 B | Apache-2.0（权重文件内 `__metadata__`） |
| `model-v1` 桌面 `sentence-model-desktop.safetensors` | 25,480,184 B | Apache-2.0 |
| `rerank-v2` 8x448 `lm_step.ort`（ONNX） | 30,834,960 B | Apache-2.0 |
| `rerank-v2` 6x256 `lm_step.ort` | 8.2 MB（官方数字） | Apache-2.0 |

**代码许可分层**（这是选型关键）：
- 整仓 **GPL-3.0-only**（`msime:LICENSE`）。
- `crates/engine` + `crates/path-trust`：**MPL-2.0，不附 Exhibit B**，可与闭源代码组合、可上架 App Store（`msime:crates/engine/README.md`「许可证」）。
- `crates/engine-capi`：MPL-2.0。
- `crates/script-sdk`、脚本插件模板：MIT。
- `chinese-ime-lm`：仓库 GPL-3.0，但 `reference/`（Rust 推理 + C ABI `chinese-ime-lm-ffi`）与 Rime 插件另以 **Apache-2.0** 授权，权重也 Apache-2.0（`lm:README.md`「许可按目录区分」）。
- **词库不是 MPL**：默认中文词库是 GPL-3.0，n-gram 是 CC-BY-SA-4.0。要把引擎塞进闭源产品，数据得自己解决。

我们 fork 是 GPL-3.0，这一层不构成障碍；如果以后想闭源分发，引擎代码没问题、词库不行。

### 2.7 Windows 宿主现状

结构（`msime:platforms/windows/README.md`、`docs/windows-parity.md:257-280`）：

- **进程内 TSF TIP**：`MetasequoiaImeTsf.dll`，导出 4 个未修饰 COM 入口，链 Rust `msime_host_api` 的导入库；x64 / x86 / Arm64X 三份。
- **进程外 `MetasequoiaImeServer`**：窗口子系统程序，与 TIP 之间走命名管道（Main / Aux / Diagnostic 三个角色），带握手、PID+登录会话+TokenUser SID 校验、焦点租约、三态投递（`Sent` / `DefinitelyNotSent` / `DeliveryAmbiguous`）。
- 另有 `MetasequoiaImeWatchdog`（登录任务守护）与 `msime-client-prepare`（首次准备运行配置）。
- **候选窗、悬浮工具栏、托盘菜单**由自绘的 `msimeui`（Direct2D/DirectWrite）画；**设置**是现代 Windows 用 WinUI 3 的 `msime-client-settings.exe`（16 页），Win7 用 Win32 原生窗口。
- 安装器 Inno Setup，按 edition 打 full / pinyin / wubi / japanese / vietnamese / tibetan 六套，各有独立 CLSID、管道名、注册表键。
- 构建：CI 走 MinGW 交叉 + Wine 跑 ctest；发布走 MSVC + SignPath（**本仓的 SignPath 尚未启用**，`msime:README.md`「代码签名策略」）。

成熟度：

- 版本 **`platforms/windows/version.txt` = 0.4.1**，官网下载页把它标为**「新版预览」「仍在测试」**，正式版仍是 `msime-windows` 的 v0.13.2（我们 fork 的基线）。
- 累计下载：正式版 58,044，新版预览 3,409（<https://msime.app/> 首页数字，2026-11-08）。
- 安装包：新版 **172.38 MB**（`windows-v0.4.1`），正式版 **61.34 MB**（`v0.13.2`），我们 fork **80.32 MB**（`v0.1.0`，含 frost 词库）。
- 测试密度不低：`platforms/windows` 注册 129 个 ctest，`tsf` 另 31 个，另有 5 道"参考实现对照"脚本门禁（配置键 180 个、界面动作 46 个、源码树 274 个 .cpp/.h 全部有交代）。
- 但真实问题仍在：截至 2026-11-08 仓库 60 个开放 issue 里，与 Windows 有关的例如
  [#11126「输入法切换时桌面崩溃」](https://github.com/metasequoiaime/msime/issues/11126)（资源管理器里切换输入法即崩，v0.4.1 nightly）、
  [#11130「卸载后重新安装，无法使用」](https://github.com/metasequoiaime/msime/issues/11130)（卸载残留 `AppData\Local\metasequoiaime-pinyin\user\dictionaries\...` 导致反复报"运行资源未能准备完成"）。
- **文档自相矛盾，需要以代码为准**：`docs/windows-parity.md:196` 写「共享库只进入独立 Server，不加载到注入应用的 TSF DLL 中」；
  但 `platforms/windows/README.md:752` 写「TSF DLL 把同一个 `msime_host_api.dll` 加载进每个宿主进程」，
  且 `platforms/windows/tsf/CMakeLists.txt:388`（生产目标 `msime-tsf` 的源文件列表里有 `EngineSessionAdapter.cpp`）、`:419`（`target_link_libraries(msime-tsf PRIVATE "${MSIME_HOST_LIBRARY}")`），
  `tsf/Composition/CompositionProcessorEngine.cpp:125` 每个 TIP 实例都 `make_unique<EngineSessionAdapter>()`。
  **实际实现是：每个打字的应用进程里都有一份引擎会话**（ONNX 重排被显式关掉以免每进程各加载一份 31 MB 模型，见 `.agents/notes/implemented/architecture/2026-11-01-onnx-rerank-backend.md`「Windows 只在 Server 的会话里挂」）。
- 内存：同一篇笔记记录 Server 私有内存在多会话时从 112 MB 涨到 190 MB，且曾因每会话重复加载模型出现 2 CPU 打满的循环；后来改成进程级共享。**这一条对新 Windows 宿主的"轻量"是负面证据**（未在真机复测）。

---

## 3. 和我们 fork 的旧版（E:\水杉输入法，v0.13.2）对比

### 3.1 两边是什么关系

- 我们 fork = `msime-windows` v0.13.2 的精简分支（`E:/水杉输入法/README.md`「和上游有什么不同」），引擎是仓内 `engine/`：**C++ 62,736 行**，版本 0.9.0，上游 commit `c810d201`（`engine/UPSTREAM.md`）。
- 新引擎 = 那个 C++ 引擎的 **Rust 移植**：参考实现是 `metasequoiaime/msime-engine` 的 `a9b9f092` + 23 个 overlay，录了行为金样放在 `crates/engine/tests/golden/`（`msime:tools/engine-golden/README.md`）。
  `crates/engine/README.md` 第一段直说：「它是 GPL-3.0 的 C++ metasequoiaime/msime-engine 的 Rust 移植」。

所以"新版引擎改了什么"这个问题，**大部分答案是"换语言、拆库、加平台"，不是"加算法"**。

### 3.2 逐项对照

| 能力 | 我们 fork（C++ 0.9.0） | 新版（Rust 0.2.0） | 结论 |
| --- | --- | --- | --- |
| 神经整句重排 | 有：`engine/neural/sentence_model.cpp`、`int8_linear_avx2.cpp`、`rescore_worker.cpp`；配置 `sentence_neural_keyboard = true`（默认开）、`sentence_neural_desktop = false`（`installer/default_config/config.default.toml:90-92`）；模型锁 `neural-model/lock.json` 指 `model-v1` 的 4.5 MB / 25.5 MB | 有：换成 `chinese-ime-lm` crate，同一批 `model-v1` 权重（`resources/neural-model.lock.json` 与我们逐字节同源） | **不是新能力**，模型和档位都一样 |
| 逐键重排的新档位 | 无 ONNX | 新增 `crates/onnx-rerank`：`rerank-v2` 的 8x448 / 6x256 单图模型，跨按键前缀缓存，按设备自测选档，进程级共享 | **新东西**，但只影响手机/桌面「增强」档，且 C ABI 不暴露 |
| n-gram 整句打分 | 有：`engine/ngram/`（KenLM `sc.lm` + octagram） | 有：`lattice/` 内 bigram/trigram（MSNG 格式）+ `lattice/octagram.rs`（librime-octagram 算法） | 等价，数据格式换了 |
| 全拼纠错 | 有：`engine/quanpin/autocorrect_table.h` | 有：`pinyin/autocorrect*`、`pinyin/typos*`、`user_dictionary/typo_profile.rs` | 等价；新版多了"按用户学习打字错误的画像" |
| 调频 / 个人词库 | 有：`engine/user_dictionary/`（journal、personal_dictionary、dictionary_state） | 有：`user_dictionary/`（journal、replay、picks、habits、positions、ranking、removal、reset）+ 学习日志按代次回放 | 等价；新版做成可嵌入的独立模块 |
| 方案 | 全拼/双拼/五笔/日文/辅助码/拆字（引擎全在，我们只在宿主层裁掉） | 全拼/双拼/九键/14 键/五笔 86-98/注音/粤拼/笔画/日/韩/越/藏 + 触点纠错 + glide | 新版多语言更全，对"只做全拼"无增量 |
| 插件 / 脚本 | 无 | `script-runtime`（wasmi 沙箱、fuel 预算）+ `plugins/` | 新能力，与全拼无关 |
| 可嵌入性 | 无 C ABI，引擎和 server/windows 耦在一个 CMake 工程 | MPL-2.0 + 38 函数 C ABI + crates.io | **这才是主要区别** |
| 平台宿主 | Windows（TSF）+ macOS/Linux/Android/iOS 各仓 | 六平台同仓，共用 host-api | 工程收益，不是输入能力 |

### 3.3 哪些能直接移植过来

诚实结论：**在"神经整句、调频、纠错"这三项上，我们不需要移植——我们已经有了，而且用的是同一批模型权重。**
新引擎里真正可能值得搬回我们 C++ fork 的东西，按性价比排：

1. **`rerank-v2` 的 ONNX 逐键模型**（8x448 / 6x256）+ 跨按键前缀缓存。质量上有数字：ctx-v1 有上文 91.9%/90.4%，对比 `model-v1` 4.25M 同口径 79.5%（`msime:.agents/notes/implemented/architecture/2026-11-01-onnx-rerank-backend.md`）。代价：要引 ONNX Runtime，精简构建 1.5 MB、官方库 34 MB，而且 **Win7 加载不了**（用到 `CreateFile2` 等 Win8+ 接口），x86 也没有库。移植到 C++ 侧属于重活（模型格式 `lm_step.ort` + 门控谓词 + 分档自测），不建议。
2. **可信词典命中就不重排的门控**（<https://msime.app/model/>；机制说明见 `lm:docs/measurements.md:5-40`）：2052 个"词典里能整段对上的词"用例上，保持引擎首选 0.719，交给模型最好 0.690。这是**用数据证明"少用模型更准"**的一条策略，我们 fork 大概率已经等价（整句首选才走重排），值得核对一次。
3. **学习日志的代次回放 + 维护租约**（`user_dictionary/generation.rs`、`docs/data-formats.md:72-110`）：解决"改个人词典时会话正在写"的并发问题。我们 fork 有 journal，但没有"代次目录 + 独占租约"这套，属于可借鉴的工程改进。
4. **纯文本可替换的数据格式**（`docs/data-formats.md`）+ 我们自己的 frost 词库构建器（`E:/水杉输入法/scripts/dictionary_build/`，输出 `msime.db`，`format_version: 1`，来源 `gaboolic/rime-frost`）：**词库格式一脉相承**——老版 `dict-v2.0.1` 的 `msime.db` 在新引擎里就叫 `msime-pinyin.db`（`msime:tools/engine-golden/README.md` 的改名表）。理论上我们的 frost 词库可以直接喂新引擎，**需实测**（新引擎还要求 `metadata.format_version`、可选 `sentence_excluded` 表等，见 `docs/data-formats.md` §4）。

**反过来，新版并没有解决我们 fork 的痛点**：安装包 172 MB vs 我们 fork v0.1.0 的 80 MB（上游正式版 61 MB）；Windows 宿主代码量 tsf 41k + src 49k 行 vs 我们 windows/src 29.6k + server 68k；真实兼容性 bug 一样在（见 2.7）。

---

## 4. 若基于 msime 引擎做一个轻量 Windows 全拼输入法

### 4.1 需要哪些模块

| 模块 | 用什么 | 备注 |
| --- | --- | --- |
| 输入引擎 | `msime-engine-capi` 预编译 DLL（11 MB，38 函数）或 Rust crate | C ABI 没有神经重排；要神经就必须 Rust crate + 自建 FFI |
| 词库 | 官方数据包（53 MB 压缩 / 102 MB 解开），或用我们的 frost 构建器按 `docs/data-formats.md` 生成 | n-gram 可省（-22.9 MB，顺序变差） |
| 神经模型 | `sentence-model.safetensors` 4.5 MB（可选，C ABI 用不上） | 需 Rust 路线；dev 构建要 `opt-level = 3` |
| TSF TIP | 自写 DLL：`ITfTextInputProcessor` + 焦点/编辑会话/组合 + 上屏 + 32/64 位 | 这是工作量主体 |
| 候选窗 | 自绘（Direct2D/GDI）：跟随光标、DPI、暗色、翻页、数字选词、UILess 兜底 | 可先做单页竖排 |
| 配置 | TOML/JSON + 托盘菜单 | 不要抄 16 页设置 |
| 安装器 | Inno Setup：TIP 注册（`{sys}\IME` + `commonpf32`）、升级、卸载 | 参考 `platforms/windows/installer/` |
| 学习数据 | 引擎自带 SQLite（`msime_user.db`），无需自建 | 注意进程内/跨进程的租约 |

### 4.2 工作量估算

以 `msime:platforms/windows/tsf`（41,070 行 C++/H）和 `src`（48,842 行）为"完整产品"上限，以我们 fork 的 `windows/src`（29,604 行）为"精简产品"，**最小可用**大约是它们的 1/5：

| 项 | 人日 | 依据 |
| --- | --- | --- |
| 引擎接入跑通（C 示例 → 自己的宿主） | 1–3 | 官方带 `examples/c/example.c` + CMake + pkg-config |
| TSF TIP 骨架（x64+x86、preedit、上屏、Esc、焦点、UILess） | 10–20 | 参考实现 41k 行是含 IPC/游戏/语音的全量 |
| 候选窗（自绘） | 8–15 | 同上，`msimeui` 是 Direct2D 全套 |
| 配置 + 托盘 + 关于 | 5–10 | — |
| 安装器 + 注册表 + 32/64 + 卸载 | 5–8 | — |
| 兼容性打磨（Chrome/Office/Terminal/UWP/游戏/远程桌面/输入法切换） | 10–30（方差最大） | 新版自己的 issue #11126 就是切换即崩 |
| **合计** | **约 40–85 人日** | 单人 2–4 个月；有 TSF 经验可压到 6–8 周 |

**如果目标就是"简单、轻便"，性价比最高的其实不是重写**：我们 fork 已经把宿主层裁到只剩全拼（`E:/水杉输入法/README.md`），
换引擎换不来候选质量（同一个算法、同一批模型），换来的只有 MPL 许可、Rust 内存安全和跨平台复用。

### 4.3 风险

1. **许可**：引擎 MPL-2.0 没问题；**词库 GPL-3.0 + n-gram CC-BY-SA-4.0**。我们 fork 本身是 GPL，不冲突；但 CC-BY-SA 的署名与传染性要写进 NOTICE，`msime-others.db` 的来源官方自己都说"再分发前要另行确认"。
2. **C ABI 缺神经重排**。要"整句神经 + 轻量 C++ TSF"，只能在 Rust crate 上自己搭 FFI，或者放弃神经（那和继续用 C++ 引擎没区别）。
3. **32 位宿主**。官方只发 x86_64 的引擎库；32 位应用（大量安装器、老软件）加载的 x86 TIP 没有库可链，ONNX 那块明确没有 32 位。要么 x86 TIP 走"哑转发"，要么自己交叉编 `i686-pc-windows-msvc`。
4. **内存/体积**。官方自己的实现是"每个打字进程一份引擎会话"，Server 私有内存见过 112→190 MB；我们要"轻便"就必须把引擎集中在单个常驻进程（等于自己写 IPC）。
5. **数据格式文档未完成**：`crates/engine-capi/README.md`「要带的数据文件」末尾写「数据格式的规格文档还没有写完（引擎库方案的 P5），在那之前以 `crates/engine` 的读端为准」。自建小词库要读源码。
6. **API 稳定性**：Rust API 0.x，官方说小版本之间可能改；C ABI 1.x 才是稳定面。
7. **构建坑**：rusqlite 版本冲突、dev 构建神经重排卡顿、`dlclose` 不可用、引擎进程级缓存收不回。

---

## 5. 另一条路：librime

只做定性对比，**体积与性能未实测**（未克隆、未编译）：

| | msime 引擎 | librime |
| --- | --- | --- |
| 许可 | 代码 MPL-2.0 / 整仓 GPL-3.0 | BSD-3-Clause（`librime/README.md`） |
| 形态 | 内置方案的"开箱引擎" | 方案驱动的框架，`*.schema.yaml` DSL（同上 Features） |
| 接口 | `msime_engine.h`，38 函数，ABI 1.1 | C API `rime_api.h`（`extern "C"` + `RIME_API`） |
| 依赖 | rusqlite bundled，无 C++ 工具链 | C++17 + boost / leveldb / marisa / opencc / yaml-cpp（`librime/README.md` Build dependencies） |
| 神经重排 | Rust crate 有；C ABI 无 | 无（需外挂，chinese-ime-lm 有 `integrations/rime/` 插件，Apache-2.0） |
| Windows 前端 | 官方 TSF（172 MB 包） | weasel（官方前端，`rime/weasel`） |
| 词库 | GPL-3.0 的 rime-ice 系 | 方案自选，rime-ice 是 GPL-3.0 |

**适配"轻便全拼"的直觉判断**：librime 的编译产物和依赖链通常比"Rust + SQLite + 100 MB 词库"更可控，生态（雾凇、四叶草等方案）也更成熟；
代价是没有神经整句、要写 YAML 方案、C++ 依赖多。**未实测，不给数字。**

---

## 6. 未查到 / 未核实

- 「比肩商业输入法」的**逐字原文**：未查到（查了仓库、官网、wiki、msime-docs、GitHub 搜索 API）。
- 任何**水杉 vs 搜狗/微软拼音/QQ/百度**的端到端对比：未查到，官方也没有做过。
- 新版 Windows 宿主的**实际内存占用**：只有笔记里的数字（112→190 MB，2026-11-02 验收虚拟机），未在真机复测。
- `msime_host_api.dll` / 新版安装包内各 DLL 的确切大小：未取（172 MB 安装包未下载拆解）。
- librime 的编译产物体积、TSF 前端 weasel 的安装包大小、启动/按键延迟：未实测。
- 我们的 frost 词库能否**直接**被 Rust 引擎读取：未实测（格式同源，需按 `docs/data-formats.md` §4 核对表结构与 `format_version`）。
- 新版 0.4.1 是否已在真实 Windows 上做过完整安装验收：未查到明确记录。`docs/windows-parity.md` 早期章节写「安装包尚未在真实 Windows 上安装验收」「那一级证据本表从头到尾都没有过」（针对 0.1.0 时期），之后没有见到更新的验收声明；目前只能从 issue #11126/#11130 这类用户报告看出它在真机上确实在用、也确实在崩。

---

## 附：本次用到的命令与产出位置

```bash
mkdir -p E:/yantai-wt/engine-probe && cd E:/yantai-wt/engine-probe
git clone --depth 1 https://github.com/metasequoiaime/msime.git        # 237 MB
git clone --depth 1 https://github.com/metasequoiaime/chinese-ime-lm.git # 14 MB
git clone --depth 1 https://github.com/metasequoiaime/msime-docs.git    # 3.4 MB
curl -sL -o dl/capi.zip https://github.com/metasequoiaime/msime/releases/download/msime-engine-capi-v0.1.2/msime-engine-capi-0.1.2-windows-x86_64-msvc.zip
unzip -l dl/capi.zip          # 体积数据来自这里
curl -s https://api.github.com/repos/metasequoiaime/{msime,msime-windows,msime-dictionary}/releases
```

未改动 `E:/水杉输入法`，未运行输入法，未 push。
