# 设置页盘点与精简（yt/settings）

用户反馈「很多功能没必要给开关，描述也有点多」。本文列出每页每个设置项、处理方式与说明文字的前后对比。

约定：
- **设置项** = 一个绑定到配置键的控件（开关、下拉、勾选、单选组、步进器、颜色），一组共用同一用途的控件（如 11 个模糊音规则）按控件个数计。模型下载 / 导入 / 删除按钮、预览、词库表格、反馈链接不是设置项，不计。
- 处理：**保留**（含「保留（本机指定）」，即用户本机特意改过、必须保留的项）、**删除开关**（功能固定为默认值，控件移除）、**合并**（并入另一项，自身控件移除）、**移到高级**（控件仍在，收进页内默认折叠的「高级」）。
- 删除开关不删配置键的读取：`server/src/config/ime_config*.cpp` 一行未动，老 `config.toml` 里的值照旧生效，出厂默认值不变。设置页只是不再显示、也不再写这些键。
- 说明文字规则：每项最多一行、约 20 字，去掉重复解释与实现细节。
- 「默认」列取自 `server/assets/config/config.toml`。


## 输入

| 名称 | 配置键 | 默认 | 原说明 | 处理 | 现说明 |
|---|---|---|---|---|---|
| 输入方案（仅「全拼」一项） | input.schema | quanpin | — | 删除开关 | —（只有一个选项，固定全拼） |
| 乱序纠错 | quanpin.autocorrect_transposition | false | 相邻字母顺序打错时纠正，如 sahng → shang（上）；默认关闭 | 保留（本机指定） | 如 sahng → shang |
| 邻键纠错 | quanpin.autocorrect_neighbor | false | 误触相邻按键时纠正，如 shabg → shang（上）；默认关闭 | 保留 | 如 shabg → shang |
| 漏字纠错 | quanpin.autocorrect_deletion | false | 漏敲一个字母时纠正，如 shng → shang（上）；默认关闭 | 保留 | 如 shng → shang |
| 多字纠错 | quanpin.autocorrect_insertion | false | 多敲一个相邻按键时纠正，如 sjhang → shang（上）；默认关闭 | 保留 | 如 sjhang → shang |
| 显示纠错标记 | quanpin.autocorrect_marker | true | 纠错得到的候选末尾显示 * 号，便于和正常候选区分；不影响上屏文字 | 删除开关 | —（固定开） |
| 模糊音（总开关） | input.fuzzy_pinyin | false | 开启后两个读音互相匹配，如输入 zhang 也可命中 zang 开头的词；另有「开启总开关后生效」提示 | 保留 | 两个读音互相匹配，如 zhang 也能命中 zang（去掉「开启总开关后生效」提示） |
| 模糊音 声母 ×6（z/zh c/ch s/sh n/l f/h r/l） | input.fuzzy_z_zh 等 6 个 | 见 config.toml | — | 保留 | — |
| 模糊音 韵母 ×5（an/ang en/eng in/ing ian/iang uan/uang） | input.fuzzy_an_ang 等 5 个 | 见 config.toml | — | 保留 | — |
| 候选词英文行 | general.english_line | true | 在候选条下面固定一行，显示当前高亮候选的第一个英文词义；Ctrl+Enter 直接上屏，Ctrl+Shift+Enter 查看全部词义 | 保留 | 候选条下显示英文词义 |
| 以词定字 | input.word_to_character | false | 开启后，按所选键组的左键上屏高亮候选的首个汉字，右键上屏末个汉字 | 保留 | 左键上屏候选首字，右键上屏末字 |
| 以词定字快捷键（[ / ] 或 - / =） | input.word_to_character_keys | brackets | — | 保留 | — |
| Esc 保留已选词 | input.escape_keeps_selected_word | false | 造词时按 Esc 只清除还没选的拼音、保留已选的词，再按一次 Esc 整体取消（小狼毫风格） | 保留 | 造词时 Esc 只清除未选的拼音 |
| 回车上屏英文时入库 | input.enter_learns_english_word | true | 按回车上屏输入的英文时，把这个单词写入英文词库，之后可作为候选出现 | 删除开关 | —（固定开） |
| 固定标点（跟随 / 中文 / 英文） | input.punctuation_lock | follow | 切换中英文时的标点形态，三者互斥 | 保留 | （去掉说明，选项自明） |
| 顿号自定义：\ 反斜杠 | input.enumeration_comma_backslash | true | 中文标点状态下用哪些键输出顿号「、」，至少保留一个；未勾选的键按原样输出 | 删除开关 | —（固定开） |
| 顿号自定义：/ 斜杠 | input.enumeration_comma_slash | true | 同上 | 删除开关 | —（固定开） |
| 智能标点（总开关） | input.smart_punctuation | false | 中文标点模式下的标点自动转换；另有「开启总开关后生效」提示 | 保留 | 中文标点模式下自动转换标点 |
| 空格转英文标点 | input.smart_punctuation_space_convert | false | 。，！？；：、 以及单独出现的 “ ” 【 】 《 》 （ ） 转为对应英文标点 | 保留 | 中文标点后按空格转为英文标点 |
| 同键转回中文 | input.smart_punctuation_repeat_to_chinese | false | 转换或直出的英文标点，2 秒内再按同一标点键转回中文 | 保留 | 2 秒内再按同一键转回中文 |
| 数字后直出 | input.smart_punctuation_direct_digit | false | 数字后输入 , . : 直接输出英文标点（如 3.14） | 保留 | 数字后的 , . : 直接输出英文标点 |
| 字母后直出 | input.smart_punctuation_direct_letter | false | 字母后输入 , . : 直接输出英文标点 | 保留 | 字母后的 , . : 直接输出英文标点 |
| 成对标点自动补全 | input.paired_punctuation | true | 输入左侧符号时自动补全右侧符号，并将光标置于中间 | 保留 | 输入左符号时补全右符号 |
| 默认中英文 | input.default_ime_mode | chinese | — | 保留 | — |
| 中英文状态（按应用记忆 / 全局统一） | input.ime_mode_scope | app | — | 保留 | — |
| 简繁输入 | input.character_set | simplified | — | 保留 | — |
| Trigram 模型整句 | association.sentence_wordlattice | false | 本地生成，下载模型后可用：使用三元语言模型生成整句候选 | 保留（本机指定） | 需先下载模型 |
| Unigram 模型整句 | association.sentence_google | true | 本地生成，使用一元语言模型生成整句候选 | 删除开关 | —（固定开） |
| 神经网络模型整句（效果优先） | association.sentence_neural_desktop | false | 本地生成，下载模型后可用：结合已上屏的前文，从内部生成的最多 12 个备选句中选择，处理时间较长 | 保留（本机指定） | 需先下载模型，处理较慢 |
| 神经网络模型整句（速度优先） | association.sentence_neural_keyboard | true | 本地生成，使用轻量模型从最多 12 个备选句中选择；两种模式可同时独立给出结果 | 保留（本机指定） | 轻量模型，速度快 |
| 上文词库重排（实验） | association.context_candidate_rerank | false | 使用轻量神经模型，根据本次输入的上文调整单字和词组顺序，例如“冬天会下大”后输入 xue 优先选“雪” | 移到高级 | 按上文调整单字、词组顺序 |
| 长词前缀补全 | association.prefix_completion | true | 本地处理，首选已有 4 个字以上时，把词库里以它开头的最可能的长词放在第二位…… | 删除开关 | —（固定开） |
| 去重后补充次选 | association.sentence_show_next_on_duplicate | false | 本地处理，某个模型的首选与已有候选重复时，显示该模型排名最高的下一条不同结果 | 移到高级 | 首选重复时补充下一条结果 |
| 显示整句来源标签 | association.sentence_source_badge | false | 本地处理，在整句候选后显示〔Trigram〕〔Unigram〕〔神经D〕…等来源标签 | 移到高级 | 整句候选后显示来源标签 |
| octagram 语法整句 | association.sentence_collocation_enabled | false | 本地生成，下载模型后可用：一并接入该模型的全部能力——……（约 90 字） | 保留 | 需先下载并激活模型 |
| octagram 语法模型选择（单选） | association.sentence_collocation_model | （空） | 内置目录里的模型包各自下载、状态独立……（约 110 字） | 保留 | 下载后单选激活，可随时切换或删除 |
| 调频方式（关闭 / 一次置顶 / 折半 / 线性 / 一次置前） | frequency_adjustment.mode | promote | — | 保留（5 档精简为 3 档） | 「自动调频」：开启（promote）/ 一次置顶 / 关闭；halve / linear 显示为「开启」，配置值不改 |
| 触发频次 | frequency_adjustment.trigger_count | 1 | 触发频次(第几次上屏触发)——衰减学习后对中文已失效 | 删除开关 | — |
| 线性调频步长 | frequency_adjustment.linear_step | 1 | 对中文已失效（线性调频不再起作用） | 删除开关 | — |

## 外观

| 名称 | 配置键 | 默认 | 原说明 | 处理 | 现说明 |
|---|---|---|---|---|---|
| 候选窗口按文字收紧宽度 | appearance.candidate_window_fit_content | false | 窗口宽度严格按候选文字所需渲染，不保留最小宽度；皮肤自身开启时始终收紧 | 保留（本机指定） | 不保留最小宽度 |
| 候选窗口跟随光标 | appearance.candidate_window_follow_cursor | true | 关闭后保持首次位置，直到候选窗口消失 | 删除开关 | —（固定开） |
| 固定候选徽标 | appearance.candidate_fixed_badge | true | 为手动固定的候选词显示角标，便于辨认 | 删除开关 | —（固定开） |
| 固定候选徽标样式 | appearance.candidate_fixed_badge_style | paperclip | — | 删除开关 | —（固定回形针） |
| 候选窗主字体 | appearance.english_font | Segoe UI | — | 保留（本机指定） | — |
| 候选窗补充字体 | appearance.fallback_fonts | Noto Sans SC, Microsoft YaHei | 主字体缺字时按以下顺序回落，最后使用系统字体。 | 移到高级 | 主字体缺字时按顺序回落 |
| 候选窗字号 | appearance.font_size | 16 | — | 保留 | — |
| 候选窗预编辑字号 | appearance.candidate_window_preedit_font_size | 16 | — | 移到高级 | — |
| 候选文字颜色（含「跟随主题」） | appearance.cand_text_color | auto | — | 保留 | — |
| 每页候选项数量 | appearance.page_size | 8 | — | 保留（本机指定） | — |
| 主题模式 | appearance.theme_mode | system | — | 保留 | — |
| 设置界面 / 候选窗口 / 悬浮工具栏 / 菜单主题 ×4 | appearance.theme_settings 等 4 个 | follow | — | 保留 | —（本来就折叠在「主题模式」下） |
| 候选项排列方式 | appearance.candidate_window_layout | vertical | — | 保留 | — |
| 横向候选强制单行 | appearance.candidate_window_single_line | false | 仅横向排列时生效：候选窗口最宽可到当前屏幕宽度，超出屏幕才换行 | 保留 | 仅横向排列时生效，最宽到屏幕宽度 |
| 行内预编辑 | appearance.tsf_preedit_style | raw | — | 保留（本机指定） | — |
| 候选窗预编辑 | appearance.candidate_window_preedit_style | pinyin | — | 保留（本机指定） | — |

## 快捷键

| 名称 | 配置键 | 默认 | 原说明 | 处理 | 现说明 |
|---|---|---|---|---|---|
| 简繁切换 Ctrl+Shift+F | keybindings.toggle_character_set_ctrl_shift_f | true | 仅在中文输入状态生效，切换时保留正在输入的内容 | 保留 | 仅中文输入状态生效 |
| 中英文切换：Shift | keybindings.switch_language_shift | true | 勾选启用对应快捷键；Ctrl+Space 由 Windows 系统管理，此处不控制（另有一段已失效的「中文→日文→英文」循环说明） | 保留 | 勾选启用；Ctrl+Space 由系统管理（循环说明删除） |
| 中英文切换：Ctrl | keybindings.switch_language_ctrl | false | 同上 | 保留 | — |
| 中英文切换：Ctrl+Alt+Space | keybindings.switch_language_ctrl_alt_space | true | 同上 | 保留 | — |
| 输入状态切换 ×3（英文候选 / 全半角 / 标点） | keybindings.input_state_shortcuts | — | 支持 Ctrl 或 Ctrl+Shift 组合。英文候选模式仅在中文输入状态生效，再按一次回到拼音输入；全半角和标点快捷键在……（约 100 字） | 保留 | 支持 Ctrl 或 Ctrl+Shift 组合；绑定后会占用当前软件的同名快捷键 |
| Shift+Enter 提交转换后的全拼 | keybindings.commit_pinyin_shift_enter | true | Enter 提交原始输入，Shift + Enter 提交转换后的全拼。敲大写字母时 Shift 常常没松开就按了回车，容易误触，可在此关闭 | 保留 | Enter 提交原始输入 |
| 翻页键 ×8（- = , . [ ] Tab PgUp/PgDn ↑↓ ←→ 滚轮） | general.paging_* / candidate_*_arrow_navigation | 见 config.toml | 勾选启用对应的候选翻页键。左 / 右只在候选窗为横排时生效……滚轮翻页依赖系统设置……（约 130 字） | 保留 | 勾选启用对应的翻页键（滚轮项注明需系统开启悬停滚动） |

## 实用功能

| 名称 | 配置键 | 默认 | 原说明 | 处理 | 现说明 |
|---|---|---|---|---|---|
| 剪贴板管理 | utility.clipboard_history | false | 开启后，复制过的文本会进入剪贴板历史。关闭后立即清空已保存的记录，且只记录文本类型。 | 保留 | 记录复制过的文本；关闭后立即清空 |
| 候选混输（总开关） | utility.mixed_candidates | true | 全拼输入中文时，把下面这些内容直接混进候选，不用切换到对应的快捷模式。 | 保留 | 全拼下把这些内容直接混进候选 |
| 中英混输 | general.cn_en_mixed_input | true | 在候选项中补充英文单词 | 保留 | 补充英文单词候选 |
| 英文触发字符数 | general.cn_en_mixed_input_min_chars | 2 | 预编辑字母达到该长度后才出现英文候选项 | 删除开关 | —（固定 2） |
| emoji 混输 | general.emoji_mixed_input | true | 输入恰好等于某个 emoji 关键词时加入 emoji（排在候选项列表最后……） | 保留 | 输入关键词时加入 emoji |
| 颜文字混输 | general.kaomoji_mixed_input | false | 在候选项中加入匹配的颜文字（排在列表末尾的 emoji 之后） | 保留 | 输入关键词时加入颜文字 |
| 符号混输 | general.symbol_mixed_input | false | 输入恰好等于某个符号编码时加入符号……例如 alpha → α、infinity → ∞……（约 90 字） | 保留 | 输入符号编码时加入符号 |
| 快捷短语混入候选 | utility.quick_phrase_candidates | true | 输入的编码与快捷短语的编码完全一致时，快捷短语直接出现在候选首位……不用按 Shift+K | 删除开关 | —（固定开） |
| 快捷短语参与调频 | utility.quick_phrase_frequency | true | 常越过快捷短语选别的候选时，快捷短语会往后退……关闭后快捷短语始终在首位 | 删除开关 | —（固定开） |
| 日期时间混入候选 | utility.date_time_candidates | true | 输入 rq / riqi / date、sj / shijian / time、xq / xingqi / week 时，当前日期、时间或星期直接出现在首个候选之后，不用按 Shift+T | 保留 | 输入 rq / sj / xq 时直接给出 |
| 日期格式入口 | utility.date_time_menu | false | 日期时间混入候选时，在第三位放一个「📅日期」，选中后候选框换成全部日期格式，和 Shift+T 模式相同 | 保留（本机指定） | 在第三位放「📅日期」入口 |
| 语言栏（总开关） | language_bar.enabled | true | 在任务栏语言栏（输入法指示器旁）分别控制中/英模式、全/半角、中文标点三个按钮的显示。修改后需重启生效。 | 保留 | 在任务栏语言栏显示输入法按钮，重启生效（展开项并入总开关） |
| 语言栏：中/英模式按钮 | language_bar.show_ime_mode | true | 显示中/英文输入模式切换按钮 | 合并 | —（并入语言栏总开关） |
| 语言栏：全/半角按钮 | language_bar.show_double_single_byte | true | 显示全角/半角切换按钮（只在使用桌面语言栏时才显示） | 合并 | —（并入语言栏总开关） |
| 语言栏：中文标点按钮 | language_bar.show_punctuation | true | 显示中文/英文标点切换按钮（只在使用桌面语言栏时才显示） | 合并 | —（并入语言栏总开关） |
| 快捷短语(K 模式) | utility.quick_phrase | true | 中文模式下按 Shift+K，再输入编码即可调用快捷短语。要让快捷短语直接混进候选，见上方「候选混输」 | 保留 | 中文模式下按 Shift+K，输入编码调用 |
| 日期与时间快捷输入(T 模式) | utility.date_time_mode | true | 中文模式下按 Shift+T，再输入 rq / riqi / date 可输入当前日期；输入 sj …（约 100 字） | 删除开关 | 改为只读说明行：rq / sj / xq 输入日期、时间、星期 |
| U 模式（部件拆字） | utility.radical_mode | true | 中文模式下按 Shift+U，输入一个字各组成部分的拼音连写即可候选出该字……（约 90 字） | 删除开关 | 改为只读说明行：输入各部件拼音，如 ririri → 晶 |
| Emoji 快捷输入(E 模式) | utility.emoji_mode | true | 中文模式下按 Shift+E，再输入全拼 / 简拼 / 英文关键词即可输入 emoji……（约 70 字） | 删除开关 | 改为只读说明行 |
| 颜文字快捷输入(M 模式) | utility.kaomoji_mode | true | 中文模式下按 Shift+M，再输入……即可输入颜文字……（约 60 字） | 删除开关 | 改为只读说明行 |
| 符号快捷输入(S 模式) | utility.symbol_mode | true | 中文模式下按 Shift+S，再输入分组码或符号名即可输入符号……（约 110 字） | 删除开关 | 改为只读说明行 |
| 临时英文(Y 模式) | utility.y_mode | true | 中文模式下按 Shift+Y，之后输入的字母按英文处理……（约 70 字） | 删除开关 | 改为只读说明行 |
| 数字与计算(V 模式) | utility.v_mode | true | 中文模式下输入 V 再接数字或算式：V123 → 一百二十三 / 壹佰贰拾叁……（约 110 字） | 删除开关 | 改为只读说明行 |

## 悬浮工具栏

| 名称 | 配置键 | 默认 | 原说明 | 处理 | 现说明 |
|---|---|---|---|---|---|
| 在桌面显示悬浮工具栏 | general.floating_toolbar | true | 快速访问输入法状态与常用功能 | 保留 | （不变） |
| 自动隐藏工具栏 | general.floating_toolbar_auto_hide | false | 工具栏显示一段时间无交互后淡出隐藏；鼠标悬停不隐藏，切换中英等状态变化时重新显示 | 保留 | 一段时间无操作后淡出 |
| 自动隐藏延时（秒） | general.floating_toolbar_auto_hide_delay | 5 | 工具栏显示多久后开始淡出（秒） | 保留 | 多少秒后淡出 |
| 工具栏阴影 | general.floating_toolbar_shadow | true | 在工具栏周围显示与候选窗一致的柔和阴影 | 删除开关 | —（固定开） |
| 切换输入法状态时提示（光标状态提示） | general.caret_state_indicator | false | 切换输入模式、全半角或中英文标点时显示 | 保留（本机指定） | 切换中英文、全半角、标点时 |
| 切换输入框时提示 | general.caret_state_indicator_on_focus | false | 焦点移到另一个输入框时（包括同一窗口内和其他窗口中的），也提示当前中英文状态 | 保留 | 焦点换到另一个输入框时 |
| 提示位置 | general.caret_state_indicator_position | top-left | 选择光标状态提示显示在光标的哪个方向 | 保留（本机指定） | 提示显示在光标的哪个方向 |
| 工具栏缩放 | general.floating_toolbar_scale | 1.0 | 相对系统 DPI 的额外缩放，不改变系统显示缩放 | 保留 | 相对系统 DPI 的额外缩放 |
| 图标尺寸 | general.floating_toolbar_font_size | 22 | 图标基准大小（像素），再乘以上方缩放 | 合并 | —（并入「工具栏缩放」，固定 22） |
| 工具栏组件 ×5（译 / 全半角 / 标点 / 简繁 / 设置） | general.floating_toolbar_translate 等 | true | 勾选要显示在悬浮工具栏中的功能 | 保留 | 勾选要显示的功能 |

## 皮肤

| 名称 | 配置键 | 默认 | 原说明 | 处理 | 现说明 |
|---|---|---|---|---|---|
| 候选窗皮肤（卡片单选，含「预览浅色」） | appearance.candidate_skin | fluent | — | 保留（本机指定） | 外部皮肤说明：把含 skin.toml 的皮肤文件夹放进下面的目录后刷新 |

## 帮助

| 名称 | 配置键 | 默认 | 原说明 | 处理 | 现说明 |
|---|---|---|---|---|---|
| 启用游戏兼容名称 + 伪装名称 | （注册表，不在 config.toml） | — | 部分游戏（CS2、Dota 2）使用白名单……功能不稳定：已知问题包括……（约 130 字，两段） | 保留 | 部分游戏只允许白名单输入法；实验性，上屏后首个退格可能被游戏吞掉 |
| 网络代理（模式 + 地址） | （代理设置） | system | 语法模型下载等联网功能共用这里的设置，修改后立即生效；地址提示含 Clash、v2rayN 等举例 | 保留 | 模型下载等联网功能共用，立即生效；地址提示去掉举例 |

## 关于

| 名称 | 配置键 | 默认 | 原说明 | 处理 | 现说明 |
|---|---|---|---|---|---|
| 自动下载更新 | appearance.auto_download_update | false | 发现新版本时由输入法后台下载并校验，下载完成后询问是否安装 | 保留 | 后台下载并校验，完成后询问是否安装 |
| 设置窗口后台驻留 | appearance.settings_window_linger | off | 关闭设置窗口后在后台保留一段时间……驻留越久占用内存越久。 | 删除开关 | —（固定关闭后立即退出） |
| 启动动画 | appearance.settings_launch_animation | true | 打开设置窗口时先在窗口中央显示齿轮图标，再淡入主界面。关闭后只短暂显示纯色背景。 | 删除开关 | —（固定开） |
| Server 端日志 | general.diagnostic_log | false | 排查 Server 通信和输入延迟时开启……文件名带生成时间，例如 …（约 150 字） | 移到高级 | 记录慢请求与通信状态，不含输入内容 |
| TSF 端日志 | general.tsf_diagnostic_log | false | 排查应用内预编辑和输入延迟时开启……（约 120 字） | 移到高级 | 记录应用内预编辑与延迟，不含输入内容 |
| 包含已下载模型（导出选项，不写配置） | — | 开 | 勾选时把数据目录 models/ 下的模型一起打包；导入按 ZIP 内容恢复…… | 保留 | 一并打包 models/ 下的模型 |

## 词库、反馈

无配置开关。词库页的批量导入提示（`native_page_dictionary_panel.cpp`）只缩短了文字，保留格式说明。反馈页是链接卡片，未改。

## 数量对比

| 页面 | 精简前 | 删除开关 | 合并 | 移到高级 | 精简后（含高级内） | 精简后（默认可见） |
|---|---|---|---|---|---|---|
| 输入 | 48 | 9 | 0 | 3 | 39 | 36 |
| 外观 | 19 | 3 | 0 | 2 | 16 | 14 |
| 快捷键 | 16 | 0 | 0 | 0 | 16 | 16 |
| 实用功能 | 23 | 10 | 3 | 0 | 10 | 10 |
| 悬浮工具栏 | 14 | 1 | 1 | 0 | 12 | 12 |
| 皮肤 | 1 | 0 | 0 | 0 | 1 | 1 |
| 帮助 | 2 | 0 | 0 | 0 | 2 | 2 |
| 关于 | 6 | 2 | 0 | 2 | 4 | 2 |
| **合计** | **129** | **25** | **4** | **7** | **100** | **93** |

