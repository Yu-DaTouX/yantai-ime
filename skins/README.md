# msime-skins

水杉输入法（Metasequoia IME）的外部皮肤合集。

## 皮肤列表

| 目录 | 名称 | 说明 |
| --- | --- | --- |
| [`acrylic`](acrylic/) | 亚克力 Acrylic | 候选卡片背后垫系统亚克力，模糊后面的窗口（`base = "fluent"`，深浅主题各一套） |

内置外观只有 `fluent`（原装）；可选外观是随安装包分发的 `acrylic`。两者都支持横排 / 竖排布局以及深色 / 浅色主题。

所有皮肤的悬浮工具栏拖动条（`[toolbar.*].handle`）都与同一主题的 `accent` 同色，没写 `handle` 时自动取 `accent`；`examples/skin-examples/tests/test_manifests.py` 会检查写了的两者是否一致。

## 内置皮肤（`default/`）

[`default/`](default/) 不是外部皮肤，而是内置皮肤 `fluent` 与全贴图基底 `canvas` 的**出厂清单**：皮肤级的开关和完整外观（配色、圆角、版式、悬浮工具栏）都写在这里，候选框与悬浮工具栏按它绘制，输入法的代码里不再写死任何一个内置皮肤的参数。`canvas` 不能单独选用，开关沿用 `fluent` 的。

- 这些清单在构建时编进程序，同时随安装包放到数据目录的 `skins\default\<id>\skin.toml`，**每次安装或升级都整份覆盖**。
- 数据目录里那一份写了的键会覆盖出厂值，所以技术上可以直接改；但**不建议**这样做，升级会冲掉改动。那份清单缺失或有一项写错时，整份退回出厂外观。
- 想要自己的样式，复制一个内置清单的内容到 `skins\<新 id>\skin.toml` 做成外部皮肤：补上 `version`、`base`（填这个内置皮肤的 id）和 `[supports]`，只留想改的键即可。

外部皮肤没写的开关与外观都沿用它 `base` 对应的这份清单。清单里除了下面「皮肤结构」一节的通用键，还有这些只跟开关相关的键：

| 表 / 键 | 说明 |
| --- | --- |
| `schema_version` / `id` / `name` / `description` | `id` 必须与目录名一致；`name` 与 `description` 是设置页皮肤卡片上的名称和说明 |
| `[candidate_window]` `page_arrows` | 候选框里是否显示翻页箭头（`true` / `false`，出厂关闭） |
| `[candidate_window]` `fit_content` | 候选框是否严格按文字所需的宽度收紧（`true` / `false`，出厂关闭）。打开后不再垫出厂的最小宽度（160 DIP），同一次输入里后到的翻译等内容消失时宽度也立刻跟着缩回 |
| `[candidate_window]` `expand_panel` | 横排候选是否用卷轴展开代替翻页（`true` / `false`，出厂关闭），见下文[卷轴展开](#卷轴展开expand_panel) |

改完后在设置页刷新皮肤列表，或重新选择一次皮肤即可生效。设置页「外观」和「皮肤」里的候选框预览目前仍按内置样式绘制，只跟随这几个开关。

## 安装

这里的皮肤都随安装包装到数据目录的 `skins\<id>`。用户那边已有同名皮肤且内容和随包的不一样（改过、多了或少了文件）时，安装器先把它改名成 `<id>.bak` 保留下来（已有 `.bak` 则用 `<id>.2.bak`……），再装新版本；输入法不会把 `.bak` 目录列成皮肤。

手动安装时，把想用的皮肤目录整个复制到：

```
%LOCALAPPDATA%\metasequoiaime\skins\
```

例如 `%LOCALAPPDATA%\metasequoiaime\skins\acrylic\skin.toml`，然后在输入法设置中选择对应皮肤即可。

## 皮肤结构

每个皮肤是一个独立目录，至少包含一个 `skin.toml`，图片等资源放在目录内（如 `assets/`）：

```
my-skin/
├── skin.toml
└── assets/          # 可选
    ├── background.png
    └── character.png
```

`skin.toml` 主要字段：

| 表 / 键 | 说明 |
| --- | --- |
| `schema_version` | 皮肤格式版本，`1` 或 `2`，也是 msime 各平台共用的清单版本；`2` 配 `base = "canvas"` 才是全贴图皮肤，配别的基底时按 `1` 加载，皮肤照常可用，只是用不上九宫格面板与自由挂件 |
| `id` / `name` / `version` / `author` / `description` | 基本信息，`id` 建议与目录名一致 |
| `base` | 继承的内置皮肤，如 `fluent`；未写的键沿用 base。`canvas` 是只给 v2 皮肤用的全贴图基底（透明面板，外观来自九宫格面板图与挂件），不能单独选用，见 [schema 说明](../examples/skin-examples/schema/README.md) |
| `[supports]` | `layouts`（`horizontal` / `vertical`）、`themes`（`dark` / `light`） |
| `[candidate_window]` | `min_width_dip`、`corner_radius_dip`（0–32）、`border_width_dip`（0–4）、`item_corner_radius_dip`（高亮圆角，0–16）、`shadow`（`none` / `soft` / `strong`）、`font_family`（排在用户字体前面）、`page_arrows`（翻页箭头：横排在候选右侧，竖排在最后一行下方、与序号左对齐；不写时沿用 base 的[内置清单](#内置皮肤default)，出厂关闭）、`fit_content`（卡片严格按文字宽度收紧，`min_width_dip` 与装饰图宽度仍然生效；不写时沿用 base 的内置清单，出厂关闭）、`expand_panel`（横排卷轴展开，见[卷轴展开](#卷轴展开expand_panel)；不写时沿用 base 的内置清单，出厂关闭）、`expand_rows`（展开后一屏的网格行数，2–8，缺省 4） |
| `[candidate_window]` 版式 | 长度可以写 DIP 数值，也可以写 `"0.3em"` 按候选字号缩放（换算后取整）：`padding`（卡片内边距）、`item_gap`（候选项间距）、`item_extra_height`（候选项比 1.35 倍字号多出的高度）、`number_inset`（高亮左缘到序号的距离，不小于 5）、`item_padding_right`（候选文字后的留白）、`item_min_width`（横排每项最小宽度）、`number_gap`（序号与候选的间距）、`selected_bar_width`（选中竖条宽）；以及 `number_scale`（序号字号比例）、`selected_bar_height_em`（选中竖条高，按字号倍数）、`selected_bar_inside`、`item_corners_follow_card`（落在卡片四角上的高亮角跟卡片同圆角）、`justify_rows`（横排时高亮铺满到卡片右边）、`trim_hidden_preedit_gap`、`page_arrow_style`（`chevron` / `triangle`）、`preedit_margin_dip`（预编辑行外边距，上右下左） |
| `[candidate_window.horizontal]` / `[candidate_window.vertical]` | 同上的版式键，只对横排 / 竖排生效，并且优先于 `[candidate_window]` 里写的 |
| `[candidate_window.decoration]` | 卡片上方的装饰图：`image`、`top_inset_dip`、`width_dip`、`align`（`left` / `center` / `right`）；`candidate_overlay = true` 时把一个候选词叠加在装饰图上，字号、颜色、对齐、偏移等见 [schema 说明](../examples/skin-examples/schema/README.md#candidate_windowdecoration可选) |
| `[candidate_window.background]` | 卡片背景图：`image`、`fit`（`cover` / `contain` / `stretch`）、`opacity`（0–1） |
| `[candidate.dark]` / `[candidate.light]` | 候选配色：`accent`、`selected`、`hover`、`surface`、`border`、`text`、`number`、`translation`、`show_selected_bar`；细分配色 `candidate_text`、`preedit_text`、`preedit_caret`、`selected_text`、`selected_number`、`selected_translation`、`translation_pos`、`translation_separator`、`selected_translation_pos`、`selected_translation_separator`、`selected_bar`、`preedit_background`、`preedit_divider`、`page_arrow_divider`（三角翻页箭头前的分隔线） |
| `[candidate.dark.menu]` / `[candidate.light.menu]` | 候选窗右键菜单：`background`、`border`、`text`、`hover` |
| `[candidate_window.border_image]` | v2 九宫格面板图：`source`、`light_source`（浅色主题换用的面板图，几何与 `source` 相同）、`slice_dip`、`stretch_dip`、`width_dip`、`fill`、`repeat`（`stretch` / `repeat`，同 CSS `border-image-repeat`）；子表 `[candidate_window.border_image.vertical]` 可给竖排换一套图和几何，见 [schema 说明](../examples/skin-examples/schema/README.md) |
| `[toolbar]` / `[toolbar.dark]` / `[toolbar.light]` | 悬浮工具栏：`corner_radius_dip`、`icon_corner_radius_dip`（图标悬停底色的圆角，0–16），以及 `background`、`border`、`handle`（不写时取同主题的 `accent`）、`divider`、`icon`、`hover`；`mode = "image"` 时整个工具栏换成一张背景图（`image`、`image_width_dip`、`image_height_dip`），窗口按图片透明区域裁成异形，按钮用 `[toolbar.buttons.<按钮>]` 的 `x_dip`、`y_dip`、`width_dip`、`height_dip`、`icon_size_dip`、`visible`、`background`（按钮底图）、`image` 与按状态的 `[images]` 摆放；`[toolbar.button_defaults]` 给所有按钮设默认值，`[toolbar.button_flow]` 让没写坐标的按钮自动排成一行或一列，用户开关按钮时跟着增减，见 [schema 说明](../examples/skin-examples/schema/README.md#图片模式mode--image) |
| `[license]` | `code`、`assets`、`source`：代码与素材的授权信息 |

所有颜色键（`[candidate.*]` 里除 `show_selected_bar` 外的键、`[candidate.*.menu]` 与 `[toolbar.*]` 的全部键）都支持十六进制 `#RGB`、`#RGBA`、`#RRGGBB`、`#RRGGBBAA`，以及 `rgb(r, g, b)`、`rgba(r, g, b, a)` 和 `transparent`。十六进制必须带 `#`（清单格式的约定，与 CSS 写法一致；渲染器对不带 `#` 的写法有容错，但不要依赖它），不透明度在末尾（`#RRGGBBAA`，不是 `#AARRGGBB`），例如 `rgba(224, 138, 168, 0.28)` 与 `#e08aa847` 等价。图片路径相对于皮肤目录，且不能指向目录之外。完整的带注释示例见 [`acrylic/skin.toml`](acrylic/skin.toml)。

## 卷轴展开（`expand_panel`）

`[candidate_window] expand_panel = true` 把横排候选窗换成卷轴展开（不少第三方输入法的做法）：

- 候选行右端是一个“∨”按钮，不显示翻页箭头（这时 `page_arrows` 被忽略）；设置页的横排预览也会画出这个按钮。
- 点按钮展开：首行仍是当前页（带序号、可用数字键选），下面是后续候选的等宽网格，一屏是首行加 `expand_rows` 行网格，右侧一条细滚动条；点网格里的候选直接上屏。滚到底部时会自动再加载一批候选。
- 滚轮：展开后首行与网格一起上下滚动，滚到顶或底就停住；其他皮肤的滚轮是翻页，两者互不影响。展开后的滚动不受设置里「滚轮翻页」开关约束；收起时滚轮和其他皮肤一样按这个开关翻页。
- 展开与收起都只能点按钮（展开后变成“∧”，固定在右上角、不随内容滚动）。继续输入时保持展开，换了候选就回到顶部；候选窗关闭（上屏或取消输入）时收起。
- 只在横排生效；竖排时照常显示候选列表与翻页。

展开按钮、滚动条用 `number` 色，网格的悬停底色用 `hover`，字体、字号、行高都跟随首行。外部皮肤在自己的 `skin.toml` 里写 `expand_panel = true` 就能打开这套展开，配色沿用它的 `base`。

## 授权说明

本仓库以 [GNU General Public License v3.0](LICENSE) 开源。单个皮肤如在 `skin.toml` 的 `[license]` 中另有声明，以该声明为准。
