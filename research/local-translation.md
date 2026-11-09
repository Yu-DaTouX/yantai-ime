# 本地中译英（整句）方案调研

调研日期：2026-11-08 · 只调研与实测，未改输入法代码 · 实测目录：`E:\yantai-wt\mt-probe`

## 一、结论

**推荐：CTranslate2（int8）+ Helsinki-NLP/opus-mt-zh-en，模型文件 78 MB，20 字中文 ~25 ms，纯 CPU，MIT + CC-BY-4.0，C++ API 干净，按需下载不随包。**

理由（数字与方法见第二、三、五节）：

1. 唯一一个在本机（Win + MSVC 生态）**已经跑通并量出数字**的方案：78 MB 模型，4 线程 20 字句子中位数 24.5 ms，p90 33.5 ms，首字节级别足够给快捷键的同步反馈。
2. 许可最干净：CTranslate2 MIT、OPUS-MT 模型 CC-BY-4.0（署名即可，允许商用与再分发，与 GPL-3.0 主体不冲突）；没有 NC（非商用）条款。
3. 集成面最小：一个 `.dll` + 一个 `model.bin` + 一个 `shared_vocabulary.json`，C++ 侧 `ctranslate2::Translator` 直接调用，输出 token 后用 SentencePiece 解码。
4. 质量够用但不惊艳：FLORES-200 devtest（1012 句）实测 BLEU 22.79 / chrF++ 50.19，日常口语句子基本正确（见第五节）。

**质量升级路径（可选，二期）：Bergamot base zh-en**（Mozilla 给 Firefox 用的 int8 模型，模型 59.5 MB，Mozilla 自评 chrF++ 55.37 / COMET22 0.8613，均优于上面）。代价是 Windows 构建链：`bergamot-translator` 仓库 2024-05 后未再动，release 只出 Linux/macOS wheel，本次在 MSVC 下**构建失败**（详见第七节），需要 clang-cl 或自建 CI，属于"能上但要养"的方案，不适合作为第一版。

**明确不要**：NLLB-200-distilled-600M（CC-BY-**NC**，体积 6 倍，质量提升有限）、M2M100-418M（同代质量、体积 1.9 GB fp32）、Qwen2.5-0.5B/Qwen3-0.6B（237–580 ms、会漏译/改口）、Windows AI / Phi Silica（**中国大陆不可用**、CPU 不支持、2027-01 被 Aion Instruct 取代）。

## 二、实测环境与方法

| 项 | 值 |
| --- | --- |
| CPU | Intel Core i7-13700F（8P+8E，16C/24T，24 逻辑核） |
| OS | Windows（Git Bash / pwsh 7） |
| Python | 3.11.7；ctranslate2 4.8.1（pip wheel，含 oneDNN/MKL 运行时） |
| llama.cpp | 官方 release `b12199` 的 `llama-b12199-bin-win-cpu-x64.zip`，`llama-server`，`-t 4` |
| 解码 | CTranslate2：`beam_size=1`（贪心），`max_decoding_length=128/256`，`intra_threads=4`（另测 1/8 线程） |
| 计时 | `time.perf_counter()` 包住单句 `translate_batch`（不含分词）；每组跑 3 轮取后两轮（20 次）为中位数 |
| 测试句 | 10 句日常中文（12–22 字），见第五节 |

脚本与原始数据都在 `E:\yantai-wt\mt-probe`：

- `bench_opus_ct2.py`、`bench_ct2_{1,4,8}t.json`
- `bench_qwen05.py`、`bench_qwen05.json`
- `flores_eval_opus.py`、`flores_opus_outputs.txt`
- `flores_eval_qwen.py`、`flores_qwen_outputs.txt`、`qwen_flores.log`
- `bergamot-build.log`（构建失败日志）

## 三、候选方案对比

体积/内存/延迟三列里，标 **(实测)** 的是本机量出来的，标 **(来源)** 的来自官方文档，标 **(估算)** 的是我按参数量/量化倍率推的——报告里没有第三种来源。

| # | 方案 | 模型体积 | 常驻内存 | 20 字中文单句延迟（CPU） | 质量 | 许可证 | C++ 集成与依赖 | GPU |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **Opus-MT zh-en + CTranslate2 int8**（推荐） | **78 MB** (实测)；fp32 原重 312 MB (来源：HF 文件列表) | 模型加载后 +92 MB；进程峰值 639 MB (实测，含 pip wheel 的 oneDNN/MKL 运行时)；纯 C++ 静态构建 **估算** 150–250 MB | **24.5 ms 中位 / 33.5 ms p90 / 46 ms 最慢**（4 线程，实测） | FLORES-200 devtest：BLEU 22.79 / chrF++ 50.19 (实测)；模型卡 Tatoeba-test BLEU 36.1 / chrF 0.548 (来源) | 模型 CC-BY-4.0，CTranslate2 MIT | 最好：`ctranslate2.dll`（pip wheel 34 MB / 主 dll 59 MB，CPU-only 自建更小）+ 2 个数据文件；API 只有 `Translator` + `translate_batch` | 不需要 |
| 2 | **Bergamot base zh-en**（Firefox 现用） | 模型 59.5 MB + 词典 9.2 MB + vocab 1.36 MB ≈ **70 MB 解压**，下载包 50.2 MB (来源：Mozilla 模型注册表 byteSize=59,504,955；GCS 实测 Content-Length) | **未实测**；按 59.1M 参数 int8 估 80–150 MB (估算) | **未实测**；Firefox 官方定位是"消费级设备上 CPU 可用的低延迟翻译"，同级 Opus 实测可作参考 (估算) | Flores200-plus：spBLEU **32.41** / chrF 58.06 / chrF++ **55.37** / COMET22 **0.8613**（base）；base-memory 31.06/54.65/0.8596；tiny 29.14/53.26/0.8482 (来源：Mozilla 模型注册表 `models.json`) | 运行时 MPL-2.0，模型仓库 MPL-2.0 | 重：需要自己编 `bergamot-translator`（Marian + intgemm + sentencepiece + ssplit/pcre2 一整套）；**MSVC 构建失败**，release 只有 linux/macos wheel（linux wheel 11 MB 可作原生库体量参考）；配置走 YAML/服务对象 | 不需要（int8 intgemm，SSE4/AVX2） |
| 3 | NLLB-200-distilled-600M + CTranslate2 int8 | fp32 2.46 GB (来源：HF 文件列表)；int8 **约 0.6 GB** (估算，CT2 官方称量化可缩到 1/4) | 未实测；int8 估算 800 MB–1.2 GB | 未实测（磁盘预算不够，见第八节）；按 opus 的 token 吞吐与 600M 参数推算约 60–100 ms (估算) | zho_Hans→eng_Latn **chrF++ 52.9**（Flores-200，来自模型卡链接的官方 `metrics.csv`） | **CC-BY-NC-4.0**（模型卡原文），CTranslate2 MIT | 中：需 NLLB 专用 SentencePiece + 目标语言 token（`eng_Latn`），tokenizer 比 opus 麻烦 | 不需要 |
| 4 | M2M100-418M + CTranslate2 int8 | fp32 1.94 GB (来源：HF 文件列表)；int8 **约 0.5 GB** (估算) | 未实测；估算 700 MB–1 GB | 未实测；估算 50–90 ms (估算) | 模型卡无 zh-en 官方指标；M2M100 论文口径是 WMT 多语平均，"与 opus 同代、略优" 属经验判断 (估算) | **MIT**（最宽松），CTranslate2 MIT | 中：需 `src_lang`/`tgt_lang` 与 M2M 词表 | 不需要 |
| 5 | Qwen2.5-0.5B-Instruct Q4_K_M + llama.cpp | **491 MB**（来源：HF GGUF 文件列表） | 797 MB 峰值（实测，`llama-server`，4 slot / 2048 ctx） | **237 ms 中位 / 410 ms 最慢**（4 线程，实测）；FLORES 长句 582 ms/句 (实测) | FLORES-200 前 300 句：BLEU 15.98 / chrF++ 44.59 (实测)；日常 10 句里出现 3 处明显错误（漏掉"下午三点"、把"楼下"译成"foot of the stairs"、方向说反成 into Chinese） | Apache-2.0 | 中：`libllama` + ggml（win-cpu 发行包 24.8 MB），或干脆起 `llama-server` 子进程走 HTTP；要自己写提示词、停用词、非译文清洗 | 不需要 |
| 6 | Qwen3-0.6B（对照） | 官方 GGUF 仓库只提供 Q8_0 639 MB；fp16 safetensors 1.50 GB (来源：HF 文件列表) | 未实测；Q4 级估算 800 MB 上下 | 未实测；同量级，**估算** 200–400 ms | 无 zh-en 官方指标；0.6B 级与 Qwen2.5-0.5B 同档，不宜期待质变 (估算) | Apache-2.0 | 同上 | 不需要 |
| 7 | Windows AI / Phi Silica（系统自带） | 模型不随系统：Copilot+ PC 上 NPU 版预装，其他设备"几个 GB"通过 Windows Update 下载 (来源：MS Learn) | 未知（由系统托管） | 未实测；无 CPU 后端，无法在本机评估 | 无 zh→en 专用 API；需自写提示词让语言模型翻译 | 系统组件，非开源 | 最差：Windows App SDK 2.x + WinRT，**CPU 列明确 "Not supported"**；GPU 需 RTX 30/RX 9060+、开发者模式、Insider 26300.8553+；**"Phi Silica features are not available in China"**；2026-11 起由 Aion Instruct 接替，2027-01 移除 Phi Silica | **必需**（NPU 或独显） |

补充事实：

- Bergamot 侧 zh→en 只有 `base`、`base-memory`、`tiny` 三档，`tiny` 未进 Firefox release；`models/base/zhen/metadata.json` 里 `flores` 两栏为 null，质量数字要以注册表 `models.json` 的 `flores200-plus` 为准。
- CTranslate2 自己的 CPU 基准（OPUS-MT base en-de、4 线程、Xeon 8275CL）：int8 **696 token/s**、最大内存 **516 MB**，BLEU 27.65（fp32 27.92）——量级与本次实测一致，可以作为"别的机器上大概什么水平"的参照（来源：CTranslate2 README Benchmarks）。
- 量化代价：CT2 官方基准里 int8 相对 fp32 的 BLEU 掉 0.27（27.92→27.65），可以认为 int8 对这类 NMT 几乎无损（来源同上）。

## 四、质量数字放在一起看

| 系统 | 数据集 | BLEU | spBLEU | chrF++ | COMET22 | 来源 |
| --- | --- | --- | --- | --- | --- | --- |
| Bergamot base zh-en | Flores200-plus | — | 32.41 | 55.37 | 0.8613 | Mozilla 模型注册表 |
| Bergamot base-memory zh-en | Flores200-plus | — | 31.06 | 54.65 | 0.8596 | 同上 |
| Bergamot tiny zh-en | Flores200-plus | — | 29.14 | 53.26 | 0.8482 | 同上 |
| NLLB-200-distilled-600M | Flores-200 | — | — | 52.9 | — | 模型卡链接的官方 metrics.csv |
| **Opus-MT zh-en + CT2 int8** | **FLORES-200 devtest（1012 句）** | **22.79** | — | **50.19** | — | **本机实测** |
| Opus-MT zh-en | Tatoeba-test | 36.1 | — | chrF 0.548 | — | HF 模型卡 |
| Qwen2.5-0.5B Q4_K_M | FLORES-200 devtest 前 300 句 | 15.98 | — | 44.59 | — | 本机实测 |

**不要跨行比大小**：Flores200-plus ≠ Flores-200 devtest ≠ Tatoeba-test，参考译文数量和分词都不同。能得出的结论只有方向性的：Bergamot base ≳ NLLB-600M > Opus-MT > Qwen-0.5B，其中 Bergamot 与 NLLB 的数字同源（都是 200 语 FLORES 系列），可信度最高；Opus 与 Qwen 是我在同一份 FLORES-200 devtest 上跑的，两者之间可比（50.19 vs 44.59，且 Qwen 只取前 300 句，Opus 同 300 句为 50.90）。

## 五、10 句实测明细

Opus-MT+CT2（int8，4 线程，首轮；后续轮次同量级）：

| 源句 | 延迟 | 输出 |
| --- | --- | --- |
| 我们明天上午十点在公司楼下集合。 | 25.8 ms | We'll meet up at 10:00 tomorrow morning downstairs. |
| 这个功能能不能再快一点？ | 16.0 ms | Can you get this thing faster? |
| 帮我把这段话翻译成英文发给他。 | 33.0 ms | I want you to translate this into English and send it to him. |
| 今天的会议改到下午三点。 | 29.1 ms | Today's meeting has been rescheduled to 3 p.m. |
| 你觉得这个方案还有什么问题吗？ | 22.2 ms | Do you think there's any problem with this? |
| 我先去吃饭，回来再继续改代码。 | 27.4 ms | I'll go eat first and come back and change the code. |
| 输入法的候选窗有时候会闪一下。 | 24.0 ms | The candidate window for the input method sometimes flashes. |
| 麻烦你确认一下附件有没有收到。 | 19.4 ms | Please confirm if the attachments are received. |
| 这台电脑的内存只有八GB，跑不动大模型。 | 44.7 ms | The memory of this computer is only eight gigabytes, and it's not running a big model. |
| 能不能按快捷键直接把整句话翻译出来？ | 23.9 ms | Can you just use the shortcut to translate the whole sentence? |

线程数对比（同一模型、同一 10 句、warm 中位）：1 线程 52.5 ms、4 线程 24.5 ms、8 线程 29.2 ms（本机 8 个 P 核 + 16 个 E 核，8 线程已跨到 E 核，反而变慢）。**结论：intra_threads=2~4 就够，别开满。**

长句：42 字「我今天下午要跟客户开会，讨论一下下个季度的产品计划，可能会晚一点回家，你不用等我了。」→ 229 ms（冷启动后第一次）/ FLORES 平均 42.7 字长句 110.9 ms/句，输出 2 句英文，语义完整。

Qwen2.5-0.5B Q4_K_M（4 线程，首轮）：

| 源句 | 延迟 | 输出 |
| --- | --- | --- |
| 我们明天上午十点在公司楼下集合。 | 491.6 ms | We will meet up at the company's foot of the stairs at ten o'clock tomorrow. ❌ 生硬 |
| 这个功能能不能再快一点？ | 194.4 ms | Is it possible to make it faster? ✅ |
| 帮我把这段话翻译成英文发给他。 | 237.5 ms | Please translate this sentence into English and send it to him. ✅ |
| 今天的会议改到下午三点。 | 256.6 ms | The meeting is scheduled to be held in the afternoon. ❌ 丢了"三点" |
| 你觉得这个方案还有什么问题吗？ | 215.9 ms | Do you have any problems with this proposal? ✅ |
| 我先去吃饭，回来再继续改代码。 | 303.2 ms | I'll go to eat first, then continue working on the code. ✅ |
| 输入法的候选窗有时候会闪一下。 | 245.9 ms | The candidate windows of the input method sometimes flash. ✅ |
| 麻烦你确认一下附件有没有收到。 | 281.0 ms | Could you please confirm whether the attachment has been received? ✅ |
| 这台电脑的内存只有八GB，跑不动大模型。 | 413.2 ms | The memory of this computer is only eight GB, and it can't run the large model. ⚠️ |
| 能不能按快捷键直接把整句话翻译出来？ | 254.5 ms | **Can you translate the whole sentence directly into Chinese?** ❌ 方向说反 |

## 六、集成草案（CTranslate2 + opus-mt-zh-en int8）

### 6.1 模型按需下载，不随安装包

- 分发物：`model.bin`（79,567,635 B）+ `shared_vocabulary.json`（1,368,999 B）≈ 78 MB；`config.json` 233 B 可内嵌。
- 存放：`%LOCALAPPDATA%\Metasequoia\models\mt\zh-en-ct2-int8\`，与用户数据目录分开，卸载时可留可删。
- 触发：设置项「整句翻译（需下载 78 MB 模型）」，用户点确认后才下载；失败/断网/校验不通过 → 静默回退到现有 `english.db` 词级释义，不阻塞输入。
- 下载用已有 HTTP 客户端（支持断点续传/HTTPS），完成写 `.part` 再原子改名 + SHA256 校验，避免半包被当成可用模型。
- 源：HF `Helsinki-NLP/opus-mt-zh-en` 的 fp32 权重需在**发布时**离线转成 int8 CT2 目录（转换只需一次，别让用户机器跑 `ct2-transformers-converter`）。建议把这个 78 MB 目录放到自己的发布渠道（GitHub Release / 对象存储）而不是让最终用户直连 HF。

### 6.2 在 Server 里跑，不碰 UI 线程

- 全局单例 `ctranslate2::Translator`，`device="cpu"`、`compute_type="int8"`、`intra_threads=2`（本机 4 线程最优但会抢输入法主路径的核，2 是稳妥值；实测 1 线程 52 ms 也够用）。
- 一个专用的 `MTWorker` 线程 + 请求队列（`request_id`、文本、超时时间、cancel 标志）；结果通过 callback 回到服务端，再由服务端推给候选窗/候选服务。
- 首次加载实测 **0.36 s**（78 MB int8）；再做一次空跑（"你好"）把线程池和内存分配预热，避免第一句慢 3–10 倍。
- 空闲预载：设置项打开后 30 s 无输入时加载，或首次进入中文输入状态时后台加载；加载期间快捷键仍可响应，返回"正在准备翻译"。

### 6.3 超时与取消

CTranslate2 **没有** `cancel()` 之类的 API，`translate_batch` 是阻塞的。可行的做法：

1. 每次请求带 deadline（建议 800 ms 软 / 2000 ms 硬）。硬超时到点后把该请求标记为 cancelled，**结果丢弃**，worker 线程继续把这一句算完再取下一个（不能强杀，强杀会破坏 Translator 内部状态）。
2. 同一 session 里用户再次按快捷键 / 继续输入 → 把上一条未完成的请求标记 cancelled，队列里最多保留 1 条待处理，避免排队雪崩。
3. 输入超过 N 字（建议 120 字）先按标点切句，逐句翻再拼；`max_decoding_length` 设 2×源 token 数上限，防止病句导致长解码。
4. 取消后 UI 不发"失败"提示，保持候选窗原样即可。

### 6.4 内存预算

- 实测（Python 宿主）：`import ctranslate2` 后工作集 516 MB（这是 pip wheel 里 oneDNN/MKL 的映射，不是模型），加载模型 +92 MB，峰值 639 MB。
- C++ 静态链接 CPU-only 版（CMake 里关掉 MKL/CUDA、只留 oneDNN 或 OpenBLAS 后端）**估算** 150–250 MB；这一项没有实测，需要在集成阶段复测（`GetProcessMemoryInfo` 的 PeakWorkingSetSize 即可）。
- 输入法本体（Server 常驻）目前量级远小于此，建议把"开整句翻译前/后"的内存差写进设置页说明。

### 6.5 C++ 调用骨架

```cpp
// 一次性（后台线程）
ctranslate2::TranslatorOptions opts;
opts.model_dir      = model_dir;
opts.device         = ctranslate2::Device::CPU;
opts.compute_type   = ctranslate2::ComputeType::INT8;
opts.intra_threads  = 2;      // 实测 4 线程最快，但给输入法留核
opts.inter_threads  = 1;
ctranslate2::Translator translator(opts);

// 每句
std::vector<std::string> tokens = sp.Encode(src);   // SentencePiece（source.spm）
tokens.push_back("</s>");
ctranslate2::TranslationOptions topts;
topts.beam_size = 1;
topts.max_batch_size = 1;
topts.max_decoding_length = 128;
auto results = translator.translate_batch({tokens}, topts);
std::string en = Join(results[0].hypotheses[0]);    // 再把 "▁" 换空格
```

依赖只有：`ctranslate2`（静态库）、`sentencepiece`（CT2 已把它作为依赖带进来，直接复用其 `SentencePieceProcessor`）、C++17 运行时。**不需要 Python，不需要 torch。**

### 6.6 许可与打包

- 输入法主体按 GPL-3.0 发布（本仓库工作目录里没找到 LICENSE 文件，此处按任务描述假定为 GPL-3.0）。
- CTranslate2：MIT → 可静态链接进 GPL-3.0 程序，保留版权声明。
- OPUS-MT 权重：CC-BY-4.0 → 需在"关于"页写清模型名、作者（University of Helsinki NLP）、CC-BY-4.0 链接；允许商用与再分发，无 NC 限制。
- 若把 Bergamot 作为备选：MPL-2.0（运行时 + 模型仓库）。MPL-2.0 是文件级 copyleft，与 GPL-3.0 组合通常可行，但**这一条属于法律判断，建议正式发版前让法务确认**，我不把它当作已核实结论。
- 不要把模型打进安装包：78 MB 会让安装包翻倍，且 GPL 分发物里混入 CC-BY-4.0 权重会让"源码包"定义变复杂。

## 七、为什么不推荐另外几个

**Bergamot / Firefox Translations（作为第一版不推荐，可作为二期）**
- 优点是真的：模型 59.5 MB 比 opus 的 78 MB 还小，Mozilla 自评 chrF++ 55.37 / COMET22 0.8613 高于 opus 实测水平，且专为 CPU int8 设计。
- 但工程面很硬：`browsermt/bergamot-translator` 最后提交 2024-05，release 资产只有 manylinux/macosx wheel 和 wasm（5.1 MB），**没有 Windows 产物**。本次实测尝试从源码构建：CMake 4 先在 vendored pcre2 上失败（需 `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`），修好后 `3rd_party/ssplit-cpp/src/ssplit/ssplit.cpp` 在 MSVC 下报 `C2187 意外的"}"` / `C1075 "(" 未匹配`，加 `/utf-8` 后仍失败（见 `bergamot-build.log`）。Firefox 自己是用 clang-cl + Mozilla 构建链出的包。
- 结论：能上，但要接受"维护一个 Windows 构建"的成本；模型质量的优势（chrF++ 约 +5）不足以抵消第一版的构建风险。**本次没有拿到 bergamot 的实测延迟**，这是报告的缺口，不是它慢的证据。

**NLLB-200-distilled-600M**
- 许可 **CC-BY-NC-4.0**：模型卡写明是 research model、"not released for production deployment"。放进一个对外分发的输入法里，NC 条款是实质性风险。
- 体积 2.46 GB fp32 / 约 0.6 GB int8，内存与下载成本都是 opus 的 8 倍，而 chrF++ 52.9 相对 opus 实测 50.19 只高 2.7 分，且不同测试集，实际差距更不确定。
- 集成还要处理 NLLB 的目标语言 token，比 opus 多一层坑。

**M2M100-418M**
- 许可最好（MIT），但 fp32 1.94 GB、int8 约 0.5 GB，是 opus 的 6 倍体积，而模型卡没有任何 zh-en 指标可引用；同代模型在 zh-en 上并不比 opus-mt-zh-en（Tatoeba BLEU 36.1）明显更好。花 6 倍体积换不确定的收益，不划算。

**Qwen2.5-0.5B / Qwen3-0.6B + llama.cpp**
- 实测 237 ms 中位、最慢 410 ms，是 opus 的 **10 倍**；FLORES chrF++ 44.59 比 opus 低 5.6 分。
- 更致命的是错误类型：漏译关键信息（"下午三点"→"in the afternoon"）、把"公司楼下"译成 "the company's foot of the stairs"、以及**把翻译方向说反**（"translate the whole sentence directly into Chinese"）。这类错误在输入法里比慢更难受，用户没法预判。
- 体积 491 MB + 运行时 797 MB 峰值，对常驻进程不友好；还要写提示词清洗、处理模型偶尔的寒暄/解释。
- 0.6B/1.5B 档可能好转，但 1.5B Q4 约 1 GB、延迟按比例再涨，已经超出"按快捷键立刻出结果"的预算。

**Windows AI / Phi Silica**
- 官方 API 列表里根本没有"文本翻译"这一项；能用的只有通用语言模型，等于自己写提示词，质量不可控。
- 硬件门槛：CPU 明确 "Not supported"，必须 Copilot+ PC 的 NPU，或 RTX 30 系/RX 9060 系以上独显 + 开发者模式 + Insider 26300.8553+。
- **文档原话 "Phi Silica features are not available in China"** —— 对一个中文输入法来说这是致命的。
- 生命周期：2026-11 起由 Aion Instruct 接替，2027-01 移除 Phi Silica；今年做的集成明年就要重写。

## 八、没做到 / 没验证的部分

1. **Bergamot 没有实测延迟和内存**。原因：仓库无 Windows 二进制，源码在 MSVC 下构建失败（日志 `bergamot-build.log`），本机没有 clang-cl（VS2022 安装里未找到）。模型文件已下载并解压到 `E:\yantai-wt\mt-probe\bergamot\zhen\`（59.5 MB 模型 + 词典 + vocab），随时可用 clang-cl 或 Linux 机器接着测。
2. **NLLB-600M / M2M100 未实测**。原因：任务限定下载总量 ≤3 GB，NLLB 光 fp32 权重就 2.46 GB，转 int8 时还要同时占 ~0.6 GB。已有 opus（301 MB 源 + 78 MB int8）、Qwen GGUF（491 MB）、Bergamot（117 MB）、FLORES（102 MB），合计约 1.1 GB，再加 NLLB 会超预算。质量数字用官方指标代替。
3. **内存数字是"Python 宿主 + pip wheel"口径**，包含 oneDNN/MKL 的映射，比纯 C++ 静态构建高；C++ 侧 150–250 MB 是估算，需要在集成阶段复测。
4. **没有测 AVX-512/老 CPU 降级路径**。本机是 13 代酷睿，CT2 运行时按 CPU dispatch 选指令集；在只有 SSE4.2 的老机器上延迟会更高（未测）。
5. **没有测更长的段落**（>120 字）和标点切句效果，只测到 42 字。
6. 许可结论（CC-BY-4.0 / MPL-2.0 与 GPL-3.0 的组合）是我按常见理解写的，**不是法律意见**；正式分发前建议确认。
7. 输入法仓库本身未改动；实测全程只用 `E:\yantai-wt\mt-probe` 与 Python 环境，未触碰运行中的输入法进程和用户词库。

## 九、参考来源

- Helsinki-NLP/opus-mt-zh-en 模型卡：CC-BY-4.0、Tatoeba-test BLEU 36.1 / chrF 0.548 — https://huggingface.co/Helsinki-NLP/opus-mt-zh-en
- CTranslate2 README（CPU 基准、量化、支持模型类型）— https://github.com/OpenNMT/CTranslate2 ；OPUS-MT 转换指南 — https://opennmt.net/CTranslate2/guides/opus_mt.html
- Mozilla 模型注册表（Bergamot zh-en 各档 spBLEU/chrF/COMET、参数量、byteSize）— https://storage.googleapis.com/moz-fx-translations-data--303e-prod-translations-data/db/models.json ；仓库 — https://github.com/mozilla/firefox-translations-models
- bergamot-translator（MPL-2.0、构建方式、release 无 Windows 产物）— https://github.com/browsermt/bergamot-translator
- facebook/nllb-200-distilled-600M 模型卡（CC-BY-NC）与官方 metrics.csv（zho_Hans→eng_Latn chrF++ 52.9）— https://huggingface.co/facebook/nllb-200-distilled-600M
- facebook/m2m100_418M（MIT，文件体积）— https://huggingface.co/facebook/m2m100_418M
- Qwen2.5-0.5B-Instruct-GGUF（各量化体积）— https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF ；Qwen3-0.6B — https://huggingface.co/Qwen/Qwen3-0.6B
- llama.cpp（MIT、CPU 后端、release b12199 win-cpu-x64）— https://github.com/ggml-org/llama.cpp
- Windows AI APIs 总览（哪些 API 有 CPU 后端）— https://learn.microsoft.com/en-us/windows/ai/apis/ ；Phi Silica（中国大陆不可用、硬件门槛、Aion Instruct 接替时间表）— https://learn.microsoft.com/en-us/windows/ai/apis/phi-silica
- FLORES-200 数据集（devtest，1012 句）— https://tinyurl.com/flores200dataset
