# 国外与系统输入法、Rime 生态的翻译功能调研

调研日期：2026-11-08。范围：Gboard、Microsoft SwiftKey、iOS 系统翻译、macOS 系统翻译、Windows 自带输入法、Rime 生态（rime-ice、rime-frost、万象拼音，以及相关 lua 插件）。方法：只读厂商帮助页、GitHub 仓库和方案文档；**没有安装或实测任何输入法**。文中“未查到”表示没有找到可核对的官方说明或代码，不是“没有”。

## 结论

1. **真正把“翻译”做进键盘的只有 Gboard 和 SwiftKey。** 两家都是“键盘上方的翻译面板/翻译条 + 明确的语言方向 + 整句翻译”，Gboard 还能把译文直接送进消息输入框；SwiftKey 的强项是复制一段外文就能在键盘上方出译文，并支持装 Microsoft Translator App 后离线。
2. **Apple 走的是系统级“选区翻译”，不是输入法级翻译。** iOS 和 macOS 都是选中文本 → Translate → 在选区旁弹出译文，可 Replace with Translation / Copy / Open in Translate；语言包可按需下载离线，macOS 还有 On-Device Mode。iOS 26/27 的 Messages Live Translation 则是整条消息的自动翻译，模型完全在设备上跑。
3. **Windows 自带输入法没有翻译。** Microsoft 拼音官方功能列表只有中英模式、U/V 模式、人名输入、Bing 文本建议，没有翻译入口。Windows 上真正的系统级翻译是 Live Captions 的实时字幕翻译，而且翻译只在 Copilot+ PC 上提供，与输入法无关。
4. **Rime 生态没有“通用翻译功能”。** rime-ice、rime-frost、万象拼音提供的都是英文输入 / 中英混输（本地词表），不是中译英。rime-ice 作者甚至明确说过：试过用 `show_in_comment` 显示英文释义，但找不到好的简短翻译数据，而且 Rime 的 comment 区和候选挤在一起很难看，最后放弃。真正做翻译的是第三方 lua 插件和墨奇输入法。
5. **可参考的 Rime 翻译实现有两类：** 一是 `JACKCHAN000/Rime-Lua-GoogleTranslate`，Ctrl+T 把当前整段组字文本发到 Google Translate，译文作为一个候选出现在候选窗；二是白霜系的墨奇输入法，把“中译英”做成可配置的 AI 动作（Ctrl+Shift+E，OpenAI 兼容接口，prompt 模板，20 秒超时），返回结果按行拆成候选。

**给水杉的建议（一句话）**：采用“选区/翻译条 + 分层（词级本地词表、句级本地 MT）+ 明确的上屏动作（采纳/替换/复制）+ 离线优先”的组合；不要把译文塞进每个候选的注释里，也不要在空闲时抢占 Ctrl+T。

---

## 一、Gboard（Android / iOS）

- **触发**：Android 在键盘顶部点“打开功能菜单”→“Translate”，再选源语言和目标语言；iOS 在键盘顶部点 Translate 图标（二手教程称需要给 Gboard 开 Full Access，官方帮助页未提这一点）。参见 [Gboard 官方帮助：Translate as you type](https://support.google.com/gboard/answer/7421372)。
- **翻译单词还是整句**：以整句/整段为主。官方步骤是“Enter your text. You'll get a preview as you type”，输入到翻译框里的整段文字都会译；也可以只输一个单词。
- **结果显示在哪**：键盘上方的翻译面板里实时预览；点预览可以看不同的译法。点 Select / Translate message 后，译文进入消息输入框。
- **在线还是离线**：官方帮助页没有提离线语言包；**Gboard 能否复用 Google Translate App 的离线包，未查到**。按现有资料只能确认它走 Google Translate 能力，是否必须联网需要实测。
- **怎么上屏**：在翻译面板里点 Select（二手教程写作“Translate message”），译文出现在消息输入框，然后正常发送或继续编辑。它不是候选词，不进入候选列表。
- **补充**：Gboard 把“翻译”做成一个独立面板，而不是在每个候选下面加小字；译文和原文是分开的两块，语言方向始终可见。

链接：[官方帮助（Android）](https://support.google.com/gboard/answer/7421372?hl=en&co=GENIE.Platform%3DAndroid) · [官方帮助（iOS）](https://support.google.com/gboard/answer/7421372?hl=en&co=GENIE.Platform%3DiOS) · [二手教程（含 iOS Full Access 步骤）](https://gadgetsnow.indiatimes.com/how-to/how-to-translate-messages-quickly-with-gboard-and-microsoft-swiftkey/articleshow/82239252.cms)

---

## 二、Microsoft SwiftKey（Android / iOS）

- **触发**：在键盘菜单里点 Translator 图标，选 From / To 语言；或者复制一段外文，SwiftKey 会自动检测语言并把译文条显示在键盘上方。官方说明见 [How to use Microsoft Translator with your Microsoft SwiftKey Keyboard](https://support.microsoft.com/en-us/swiftkey-keyboard/how-to-use-microsoft-translator-with-your-microsoft-swiftkey-keyboard)（页面分 Android/iOS 两栏，下面以 Android 流程为主，iOS 是否完全一致未逐项核对）。
- **翻译单词还是整句**：整句/整条消息。它同时处理两个方向：收到外文时翻译给你看；你输入回复时，边打边把回复译成对方语言。
- **结果显示在哪**：键盘上方的翻译条（translation bar），不在候选窗里。
- **在线还是离线**：默认在线；如果安装 Microsoft Translator App 并下载离线语言包，可以离线。官方原文：“designed to work online, but if you download the Microsoft Translator app … you’ll be able to use it offline too.”
- **怎么上屏**：官方步骤只写到“display the translated text above your keyboard”“begin to type your reply … translates on the fly”，**没有写如何把译文一键插入/替换输入框，未查到**。能确定的是译文条不是候选词；它显示在键盘上方，官方未说明是否会替换输入框内容。
- **补充**：SwiftKey 的“复制即译”很适合读消息：先复制对方的话，翻译条出现，点 X 或 Reply 清掉，然后用自己语言回复。

链接：[官方帮助](https://support.microsoft.com/en-us/swiftkey-keyboard/how-to-use-microsoft-translator-with-your-microsoft-swiftkey-keyboard) · [Microsoft Translator 官方博客](https://www.microsoft.com/en-us/translator/blog/2018/09/27/swiftkey-translator-integration) · [Notebookcheck 2018 年上手](https://www.notebookcheck.net/SwiftKey-for-Android-now-integrates-Microsoft-Translator.336358.0.html)

---

## 三、iOS 系统（翻译服务，不是输入法）

- **触发**：在 Safari、Messages、Mail 或支持第三方 App 里选中文本 → 点 Translate（如果没有，点“更多”）；再选目标语言。Messages 里另有 Apple Intelligence 的 Live Translation，会自动把收到的外文消息译成你的首选语言。
- **翻译单词还是整句**：任意选区，单词、短语、整句、整段都可以；Messages 的 Live Translation 是整条消息。
- **结果显示在哪**：选区旁的翻译面板/表单，显示原文和译文；Messages 里是消息内联显示。
- **在线还是离线**：Translate App 可以下载语言包，“translate entirely on your iPhone, even without an internet connection”；Live Translation 使用 Apple 自建模型，完全在设备上运行。系统选区翻译的面板里也有“Download Languages”入口。
- **怎么上屏**：可编辑文本里选 **Replace with Translation**（用译文替换原文）；也可以 **Copy Translation**、**Open in Translate**、**Add to Favorites**、听原文/译文。Messages 的 Live Translation 只显示，不替换消息。
- **补充**：iOS 没有“输入法级”翻译按钮；它把翻译做成系统文本服务，任何 App 的文本选区都能调用。这是 Apple 和 Gboard/SwiftKey 最大的路线差异。

链接：[Translate text in apps on iPhone](https://support.apple.com/en-mt/guide/iphone/iphab4dcff1d/26/ios/26) · [Translate words and phrases on iPhone（离线说明）](https://support.apple.com/en-md/guide/iphone/iphd74cb450f/ios) · [Translate text in Messages（Live Translation，on-device）](https://support.apple.com/guide/iphone/translate-texts-iphyrz4rgtgk/ios)

---

## 四、macOS 系统（翻译服务，不是输入法）

- **触发**：选中文本 → 右键/Control-click → 选 **Translate [_text_]**；在语言下拉菜单里选原文语言和目标语言。图片里的文字可以先用 Live Text 选中再翻译。
- **翻译单词还是整句**：任意选区，词、句、段都可以。
- **结果显示在哪**：选区旁弹出的翻译面板；面板里有播放按钮、Copy Translation。
- **在线还是离线**：可以在系统设置里下载语言包离线翻译；设置里有 **On-Device Mode**，即使联网也在本机处理。注意 Apple 的说明：**Siri 和 Safari 始终在线翻译**，不走离线模式。
- **怎么上屏**：写作场景里可直接点 **Replace with Translation**，用译文替换选中的原文；也可以 **Copy Translation** 复制到剪贴板，或点播放听译文。
- **补充**：和 iOS 一样，macOS 没有输入法级翻译；它是系统级服务，所以能覆盖大多数 App，但不参与候选词、不参与输入法状态机。

链接：[Translate text on Mac（含 Replace 与离线下载）](https://support.apple.com/en-is/guide/mac-help/mchldd8b3c15/mac) · [Change Language & Region settings on Mac（Translation Languages / On-Device Mode）](https://support.apple.com/en-lk/guide/mac-help/intl163/mac)

---

## 五、Windows 自带输入法

### 5.1 Microsoft 拼音（Microsoft Simplified Chinese IME）

- **触发**：**没有翻译入口**。官方功能列表里与“转换”有关的只有：Shift 中英模式切换、Shift+Space 全半角、Ctrl+. 中英标点、U 模式（笔画/部件/符号）、V 模式（数字、日期、时间、公式、Unicode）、`;R` 人名输入、Tab 展开 Bing 文本建议。
- **翻译单词还是整句**：不适用；没有翻译功能。
- **结果显示在哪**：不适用。
- **在线还是离线**：不适用；Bing 文本建议是在线补全，不是翻译。
- **怎么上屏**：不适用；正常选候选上屏。
- **补充**：这意味着在 Windows 上，系统自带拼音并没有“中文打一半出英文”的能力；水杉如果做翻译，是在补一个系统输入法的空白。

链接：[Microsoft Simplified Chinese IME 官方文档](https://support.microsoft.com/en-us/windows/hardware/input-devices/microsoft-simplified-chinese-ime)

### 5.2 Windows Live Captions（系统级字幕翻译，不是输入法）

- **触发**：Win+Ctrl+L，或快速设置/辅助功能里打开 Live Captions；在 Copilot+ PC 上，输出语言选 English (US) 或 Chinese (Simplified) 时会自动带翻译。
- **翻译单词还是整句**：连续语音，按句出字幕并翻译，不是文本输入。
- **结果显示在哪**：屏幕上的字幕窗口，可设置位置（屏幕上方/下方/覆盖）。
- **在线还是离线**：音频处理和字幕生成都在本机；官方说“Captions can be provided for audio even when disconnected from the internet”。翻译仅 Copilot+ PC、Windows 11 24H2+，支持 40+ 种语言→英语、27 种语言→简体中文。
- **怎么上屏**：只显示字幕，不写入当前应用，也不是输入法候选。
- **补充**：这是 Windows 上最接近“系统级翻译”的东西，但它是无障碍字幕，不是输入法。

链接：[Use live captions to better understand audio](https://support.microsoft.com/en-gb/accessibility/windows/use-live-captions-to-better-understand-audio)

---

## 六、Rime 生态

Rime 本身只是引擎；有没有翻译，取决于方案和 lua 插件。下面三个主流方案都没有通用翻译，只有英文输入/中英混输。

### 6.1 rime-ice（雾凇拼音）

- **仓库/文件**：英文输入由 [`melt_eng.schema.yaml`](https://github.com/iDvel/rime-ice/blob/main/melt_eng.schema.yaml) 定义（schema_id `melt_eng`，名称 Easy English Nano，`table_translator`，词典 `melt_eng`，`spelling_hints: 9`）；词表由 [`melt_eng.dict.yaml`](https://github.com/iDvel/rime-ice/blob/main/melt_eng.dict.yaml) 挂载，import `en_dicts/en_ext` + `en_dicts/en`；英文主词库是 [`en_dicts/en.dict.yaml`](https://github.com/iDvel/rime-ice/blob/main/en_dicts/en.dict.yaml)（README 说约 2 万常见词），扩展是 `en_dicts/en_ext.dict.yaml`（缩写/互联网词）。中英混输另有 [`en_dicts/cn_en.txt`](https://github.com/iDvel/rime-ice/blob/main/en_dicts/cn_en.txt) 等，由 [`rime_ice.schema.yaml`](https://github.com/iDvel/rime-ice/blob/main/rime_ice.schema.yaml) 里的 `table_translator@cn_en` 挂载。
- **触发**：中文模式下直接打英文字母，`melt_eng` 作为次翻译器出英文候选；也可以切到 Easy English 方案。**没有翻译快捷键**。
- **翻译单词还是整句**：英文单词/词组补全；`rime_ice.schema.yaml` 的 `melt_eng:` 段配置了 `enable_sentence: false`，不造句，更不翻译。
- **结果显示在哪**：候选窗。
- **在线还是离线**：完全离线，本地词表，没有模型。
- **怎么上屏**：正常选候选上屏英文。
- **翻译相关**：README 的“词汇别名”只是少量常用词的翻译/别名/化学式/简称注释，不是通用翻译。作者 Dvel 在博客里明确写过：`show_in_comment: true` 可以显示英文单词的翻译，类似哈利路亚输入法，但“没有找到比较好的简短翻译数据”，而且“Rime 的 comment 区域并不是一个独立存在的窗口，翻译和候选项挤在一起看起来乱糟糟的”，所以没有做。
- **词表/模型**：`en` + `en_ext` 英文词表；`cn_en*.txt` 中英混输词表；无翻译模型。

链接：[rime-ice](https://github.com/iDvel/rime-ice) · [Dvel 博客（show_in_comment 的取舍）](https://dvel.me/posts/rime-ice) · [melt_eng 上游 rime-melt](https://github.com/tumuyan/rime-melt)

### 6.2 rime-frost（白霜拼音）

- **仓库/文件**：[`melt_eng.schema.yaml`](https://github.com/gaboolic/rime-frost/blob/master/melt_eng.schema.yaml) 与 rime-ice 同源（文件头写明复制自 `tumuyan/rime-melt`，只改了拼写派生），词典 `melt_eng`；[`melt_eng.dict.yaml`](https://github.com/gaboolic/rime-frost/blob/master/melt_eng.dict.yaml) 同样 import `en_dicts/en_ext` + `en_dicts/en`；[`en_dicts/en.dict.yaml`](https://github.com/gaboolic/rime-frost/blob/master/en_dicts/en.dict.yaml) 是英文主词库（文件头写明来源：2 万常见词 `20k.txt` + 融合拼音方案 `rime-melt` 的英文词库）；另有 `en_dicts/en_ext.dict.yaml`、`en_dicts/cn_en*.txt` 和 `lua/en_spacer.lua`、`lua/autocap_filter.lua` 等英文辅助脚本。仓库里**没有** translate.lua 之类的翻译插件。
- **触发**：与 rime-ice 相同，中文模式下直接打英文，或切 Easy English；没有翻译快捷键。
- **翻译单词还是整句**：英文单词/词组补全；不翻译。
- **结果显示在哪**：候选窗。
- **在线还是离线**：离线词表，无模型。
- **怎么上屏**：选候选上屏英文。
- **词表/模型**：`en`（约 2 万词）+ `en_ext`；`cn_en*.txt` 中英混输；无翻译模型。
- **补充**：白霜拼音的 README 主要讲词频、辅助码、安装，没有单列“英文/翻译”章节；英文文件是从雾凇继承的结构。白霜真正的 Windows 客户端是墨奇输入法，见 6.5。

链接：[rime-frost](https://github.com/gaboolic/rime-frost) · [白霜英文方案](https://github.com/gaboolic/rime-frost/blob/master/melt_eng.schema.yaml) · [白霜英文词库](https://github.com/gaboolic/rime-frost/blob/master/en_dicts/en.dict.yaml)

### 6.3 万象拼音（rime-wanxiang）

- **仓库/文件**：英文能力由 [`lua/wanxiang/super_english.lua`](https://github.com/amzxyz/rime-wanxiang/blob/wanxiang/lua/wanxiang/super_english.lua) 实现（T 翻译器 + F 过滤器 + P 处理器：英文候选、候选数量限制、单字母大小写派生、句内空格恢复、连续英文自动空格、英文造词、输入记忆）；词表是 [`dicts/en.dict.yaml`](https://github.com/amzxyz/rime-wanxiang/blob/wanxiang/dicts/en.dict.yaml)（`name: wanxiang_english`，`sort: by_weight`，文件约 1.57 MB）；拼写派生在 [`custom/wanxiang_english.custom.yaml`](https://github.com/amzxyz/rime-wanxiang/blob/wanxiang/custom/wanxiang_english.custom.yaml)；说明文档是 [`docs/doc/english.md`](https://github.com/amzxyz/rime-wanxiang/blob/wanxiang/docs/doc/english.md)。仓库另有 `lua/data/english_chinese.txt`（约 920 KB，英文词条→中文释义，例如 `abandon 抛弃`），但**具体由哪个 lua 加载、是否用于显示，未查到**。
- **触发**：英文连续输入，可以挂到中文主方案，也可以作为独立方案运行；没有翻译快捷键。文档重点讲 smart spacing、大小写推导、混合编码（`AD钙奶`、`Type-C` 等）。
- **翻译单词还是整句**：英文单词、连续英文句子、中英数字符号混合编码；**不是翻译**。
- **结果显示在哪**：候选窗；编码未完整匹配时会出现 `~` 中间态。
- **在线还是离线**：离线词表，无模型。
- **怎么上屏**：按空格/回车或选候选；自动空格会根据连续输入状态补词间空格。
- **词表/模型**：`wanxiang_english` 英文词表；`english_chinese.txt` 是英中释义表（用途未查到）；无翻译模型。

链接：[rime-wanxiang](https://github.com/amzxyz/rime-wanxiang) · [万象英文文档](https://github.com/amzxyz/rime-wanxiang/blob/wanxiang/docs/doc/english.md) · [super_english.lua](https://github.com/amzxyz/rime-wanxiang/blob/wanxiang/lua/wanxiang/super_english.lua)

### 6.4 Rime-Lua-GoogleTranslate（真正做中译英的第三方 lua 插件）

- **仓库/文件**：[`JACKCHAN000/Rime-Lua-GoogleTranslate`](https://github.com/JACKCHAN000/Rime-Lua-GoogleTranslate)。中译英在 [`rime/lua/c2e.lua`](https://github.com/JACKCHAN000/Rime-Lua-GoogleTranslate/blob/main/rime/lua/c2e.lua)，触发在 [`rime/lua/c2etrigger.lua`](https://github.com/JACKCHAN000/Rime-Lua-GoogleTranslate/blob/main/rime/lua/c2etrigger.lua)；英译中对应 `e2c.lua` / `e2ctrigger.lua`；另有 `google.lua` 做搜索建议。
- **触发**：README 写得很清楚：中译英 **Ctrl+T**，英译中 **Ctrl+E**，Google 搜索建议 **Ctrl+G**。需要把 `lua_processor@xxx_processor, lua_translator@xxx_translator` 加进方案。
- **翻译单词还是整句**：当前整个组字文本。`c2etrigger.lua` 在组字状态下用 `context:get_commit_text()` 取整段文本，`c2e.lua` 把这段文本整个发出去，所以是整句/整段，不是逐词。
- **结果显示在哪**：候选窗。`c2e.lua` 构造 `Candidate("translate", seg.start, seg._end, output, "翻譯")`，译文本身就是候选词，comment 是“翻譯”。
- **在线还是离线**：在线。请求 `http://translate.googleapis.com/translate_a/single?client=gtx&sl=zh_TW&tl=en&dt=t&q=...`，用 `simplehttp` + `json.lua` 解析；没有本地模型，也没有离线回退。
- **怎么上屏**：选这个候选，译文就正常上屏。
- **词表/模型**：没有词表、没有模型；完全依赖 Google Translate 的非官方 `translate_a/single` 接口。源语言写死 `zh_TW`，译文质量/可用性受接口影响，属于“能用但会坏”的方案。
- **补充**：这是 Rime 生态里最接近“Ctrl+T 整句翻译”的现成实现，和常见的“草稿 + 底部英文行”思路相近；差别是它把译文直接放进候选窗，而不是单独草稿行/英文行。

链接：[仓库](https://github.com/JACKCHAN000/Rime-Lua-GoogleTranslate) · [c2e.lua](https://github.com/JACKCHAN000/Rime-Lua-GoogleTranslate/blob/main/rime/lua/c2e.lua) · [c2etrigger.lua](https://github.com/JACKCHAN000/Rime-Lua-GoogleTranslate/blob/main/rime/lua/c2etrigger.lua)

### 6.5 墨奇输入法（白霜系的 Windows 输入法，AI 动作）

- **仓库/文件**：Windows TSF 层是 [`gaboolic/moqi-im-windows`](https://github.com/gaboolic/moqi-im-windows)，输入法核心是 [`gaboolic/moqi-ime`](https://github.com/gaboolic/moqi-ime)。AI 客户端在 [`input_methods/rime/ai_client.go`](https://github.com/gaboolic/moqi-ime/blob/main/input_methods/rime/ai_client.go)，默认动作在 [`input_methods/rime/ai_config.json`](https://github.com/gaboolic/moqi-ime/blob/main/input_methods/rime/ai_config.json)。用户配置路径按官方文档是 `C:\Users\<用户名>\AppData\Roaming\Moqi\Rime\ai\_config.json`。
- **触发**：`ai_config.json` 里预置了动作“中译英”，快捷键 **Ctrl+Shift+E**；动作名、快捷键、prompt 都可改。prompt 是“请把当前中文内容翻译成自然、地道的英文。`{{candidate_1}}` 只输出 1 条英文翻译，不要解释。”
- **翻译单词还是整句**：默认是 **`{{candidate_1}}`，即当前第一候选**（词/短语，长句候选时才是整句），不是整段组字文本。想整句可以把模板改成 `{{composition}}`；代码支持 `{{previous_commit}}`、`{{composition}}`、`{{raw_input}}`、`{{candidate_1}}`~`{{candidate_3}}`、`{{candidates_top3}}` 等占位符。
- **结果显示在哪**：AI 返回内容按行拆成候选（`parseReviewCandidates` 逐行转成字符串），由 TSF 层显示在候选窗。官方文档没有给截图/步骤，**“显示在候选窗、选候选上屏”是按代码推断，未实测**。
- **在线还是离线**：在线。走 OpenAI 兼容的 `POST {base_url}/chat/completions`，`Authorization: Bearer <api_key>`，超时 20 秒，temperature 0.8；需要自己填 `base_url`、`api_key`、`model`。没有本地模型。
- **怎么上屏**：按代码推断是选 AI 候选上屏；不是自动替换原文。
- **模型**：任意 OpenAI 兼容模型；仓库示例配置写的是 `gpt-5-mini`。**仓库示例 `ai_config.json` 里带了一个公开的 API key，报告不复制；产品不能沿用，也不应把 key 打进安装包。**
- **补充**：这是目前最接近“Windows 本地拼音输入法 + 翻译”的现成参考：TSF 层只管候选窗/上屏，翻译作为一个可配置动作挂在引擎侧；缺点是依赖云 API、没有离线降级、默认只译第一候选。

链接：[墨奇输入法说明（AI 配置）](https://moqiyinxing.chunqiujinjing.com/index/mo-qi-shu-ru-fa-shuo-ming) · [ai_client.go](https://github.com/gaboolic/moqi-ime/blob/main/input_methods/rime/ai_client.go) · [ai_config.json](https://github.com/gaboolic/moqi-ime/blob/main/input_methods/rime/ai_config.json) · [moqi-im-windows](https://github.com/gaboolic/moqi-im-windows)

### 6.6 未查清：墨奇音形的 Ctrl+E“中英互译”

墨奇音形文档有一页“中英互译”，只写“在输入过程中按下 ctrl+e，可开启中英互译”，没有说明是本地词表、在线接口还是 AI 动作。仓库 `gaboolic/rime-shuangpin-fuzhuma` 里有 `easy_en.schema.yaml` / `easy_en.dict.yaml`（英文输入方案），但**没有找到对应的翻译 lua 或中英对照词表**；`ctrl+e` 的具体实现**未查到**。因此本节不把它当成已确认的翻译实现，只记录“文档声称有”。

链接：[中英互译文档](https://moqiyinxing.chunqiujinjing.com/index/mo-qi-yin-xing-gao-ji-gong-neng/zhong-ying-hu-yi) · [墨奇音形仓库](https://github.com/gaboolic/rime-shuangpin-fuzhuma)

---

## 七、对比表

| 方案 | 触发 | 翻译单位 | 结果显示 | 在线/离线 | 上屏方式 |
| --- | --- | --- | --- | --- | --- |
| Gboard | 键盘功能菜单 → Translate；iOS 顶部图标 | 整句/整段（也可单词） | 键盘上方翻译面板，随打随预览 | 在线；离线能力**未查到** | 点 Select / Translate message，译文进消息输入框 |
| SwiftKey | 键盘菜单 Translator 图标；或复制文本自动出翻译条 | 整句/整条消息，双向 | 键盘上方翻译条 | 在线；装 Translator App + 离线语言包可离线 | 官方只写“边打边显示译文”，一键插入方式**未查到** |
| iOS 系统 | 选中文本 → Translate；Messages Live Translation 自动 | 任意选区；消息整条 | 选区旁翻译面板；Messages 内联 | 可下载语言包离线；Live Translation 全在设备上 | Replace with Translation / Copy / Open in Translate；Messages 只显示 |
| macOS 系统 | 选中文本 → 右键 Translate | 任意选区 | 选区旁弹出面板 | 可下载语言包离线；On-Device Mode；Siri/Safari 始终在线 | Replace with Translation / Copy Translation / 播放 |
| Windows Microsoft 拼音 | 无翻译入口 | 不适用 | 不适用 | 不适用 | 不适用 |
| Windows Live Captions | Win+Ctrl+L | 连续语音，句级字幕 | 屏幕字幕窗 | 本机处理，可离线；翻译仅 Copilot+ PC | 只显示，不写入应用 |
| rime-ice / rime-frost | 中文模式直接打英文，或切 Easy English | 英文单词/词组（不翻译） | 候选窗 | 离线词表 | 选候选上屏英文 |
| 万象拼音 | 英文连续输入（主方案或独立方案） | 英文词/句/混合编码（不翻译） | 候选窗，`~` 为中间态 | 离线词表 | 空格/回车/选候选 |
| Rime-Lua-GoogleTranslate | Ctrl+T（中译英）/ Ctrl+E（英译中） | 当前整段组字文本 | 候选窗，译文候选 comment“翻譯” | 在线（Google 接口） | 选候选上屏译文 |
| 墨奇输入法 AI 动作 | Ctrl+Shift+E（可配置） | 默认 `candidate_1`（词/短语） | 候选窗（按代码推断） | 在线（OpenAI 兼容 API） | 选 AI 候选上屏（推断） |

---

## 八、值得借鉴的 2–3 种做法

### 做法 1：Apple 的“选区翻译 + 原位替换 + 离线包/On-Device Mode”

Apple 不把翻译塞进输入法，而是做成系统文本服务：选中即译，结果在选区旁，动作只有几个但每个都很明确——**替换原文、复制、打开 Translate、朗读**。离线不是默认全量下载，而是按语言对按需下载；macOS 还给了 On-Device Mode，让用户在联网时也强制本机处理。

对水杉的启发：
- 翻译结果默认**不自动上屏**，先给用户看；Enter 采纳、Shift+Enter 保留中文、Esc 关闭，另外提供“复制/替换/追加”。
- 离线模型按语言对下载，设置里明确写“本机处理/需要联网”，不要含糊。
- 选区翻译和输入法内翻译可以共用同一个引擎，但 UI 入口分开：选中文本走系统菜单/悬浮按钮，组字过程走候选窗翻译条。

### 做法 2：Gboard / SwiftKey 的“键盘内翻译条 + 明确语言方向 + 一键采用”

Gboard 把翻译做成键盘上方的一个面板：源语言/目标语言始终可见，输入时实时预览，点预览还能看不同译法，最后用 Select / Translate message 把译文放进消息框。SwiftKey 把“复制一段外文”也纳入同一套条：复制即出译文，回复时边打边译，离线则依赖 Translator App 的语言包。

对水杉的启发：
- **翻译条是独立区域，不是候选注释**。候选窗可以上下分栏：候选区 + 一条 1–2 行的译文条；不要把英文小字塞进每个候选下面（Rime 作者已经踩过这个坑）。
- 语言方向要可见（中→英/英→中/自动检测），并且允许一键交换。
- 采用动作要明确：Enter/点击“采纳”把译文送进输入框；Esc 关闭；不要“边打边偷偷替换”。

### 做法 3：Rime 的分层与克制——英文输入 ≠ 翻译，词级释义 ≠ 句级翻译

rime-ice、rime-frost、万象都只做英文输入/中英混输：本地词表、候选窗、离线、零模型。它们没有为了“看起来像翻译”而把英文释义塞进每个候选；rime-ice 作者甚至试过 `show_in_comment` 显示英文翻译，最后因为数据质量和 comment 拥挤放弃。真正做翻译的插件（Rime-Lua-GoogleTranslate）反而是独立动作：Ctrl+T，整段文本发出去，译文作为一个候选回来；墨奇则把中译英做成可配置的 AI 动作，prompt 模板、快捷键、模型都可换，但默认只译 `candidate_1`，并且依赖云 API。

对水杉的启发：
- **词级和句级分开**：词级释义用本地词表/小模型（现有 `english.db` 可以继续做降级），句级用本地 MT（`research/local-translation.md` 已实测 Opus-MT + CTranslate2，20 字约 25 ms）；不要用一个模型同时承担“候选词释义”和“整句翻译”。
- **翻译是显式模式/动作，不是隐式注释**：进入翻译模式才跑句级模型；候选词释义按需显示（例如停留/快捷键），不要每个候选都自动加一行。
- **失败要降级**：云 API 方案（墨奇）没有离线回退；水杉应本地优先，模型缺失时降级到词表并提示下载，而不是整块功能不可用。
- **不要把 key 打进安装包**：墨奇的示例配置里带了公开 key，这是反面教材；水杉如果做云增强，应让用户自填，默认关闭。

---

## 九、未查到 / 不能确认的清单

- Gboard 的离线翻译能力：官方帮助页没有提离线语言包，是否复用 Google Translate App 的离线包**未查到**。
- SwiftKey 译文条如何一键插入/替换输入框：官方步骤只写到“显示译文/边打边译”，**未查到**插入按钮说明。
- 万象 `lua/data/english_chinese.txt` 的具体加载位置和展示方式：**未查到**；只能确认它是英文→中文释义表。
- 墨奇音形 Ctrl+E“中英互译”的实现：文档只有一句话，仓库未找到对应 lua/词表，**未查到**。
- 墨奇 AI 动作的结果显示与上屏：官方文档没有截图/步骤，本文按 `ai_client.go` + `parseReviewCandidates` 推断为“候选窗 + 选候选上屏”，**未实测**。
- iOS / macOS 是否有“输入法级”翻译：没有找到；两家都是系统文本服务/选区翻译，不是输入法候选。
- Windows 自带拼音是否有隐藏翻译开关：官方功能列表没有；本文按“没有”处理，**未做真机逐项验证**。

## 十、主要来源

- Gboard：[Translate as you type（Android）](https://support.google.com/gboard/answer/7421372?hl=en&co=GENIE.Platform%3DAndroid)、[iOS](https://support.google.com/gboard/answer/7421372?hl=en&co=GENIE.Platform%3DiOS)
- SwiftKey：[Microsoft Translator 帮助](https://support.microsoft.com/en-us/swiftkey-keyboard/how-to-use-microsoft-translator-with-your-microsoft-swiftkey-keyboard)、[Microsoft Translator 博客](https://www.microsoft.com/en-us/translator/blog/2018/09/27/swiftkey-translator-integration)
- Apple：[iPhone 选区翻译](https://support.apple.com/en-mt/guide/iphone/iphab4dcff1d/26/ios/26)、[iPhone 离线语言包](https://support.apple.com/en-md/guide/iphone/iphd74cb450f/ios)、[Messages Live Translation](https://support.apple.com/guide/iphone/translate-texts-iphyrz4rgtgk/ios)、[Mac 翻译/替换/离线](https://support.apple.com/en-is/guide/mac-help/mchldd8b3c15/mac)
- Microsoft：[Simplified Chinese IME](https://support.microsoft.com/en-us/windows/hardware/input-devices/microsoft-simplified-chinese-ime)、[Live Captions](https://support.microsoft.com/en-gb/accessibility/windows/use-live-captions-to-better-understand-audio)
- Rime：[rime-ice](https://github.com/iDvel/rime-ice)、[Dvel 博客](https://dvel.me/posts/rime-ice)、[rime-frost](https://github.com/gaboolic/rime-frost)、[rime-wanxiang](https://github.com/amzxyz/rime-wanxiang)、[Rime-Lua-GoogleTranslate](https://github.com/JACKCHAN000/Rime-Lua-GoogleTranslate)、[moqi-ime](https://github.com/gaboolic/moqi-ime)
