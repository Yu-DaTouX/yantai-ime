# Twemoji Mozilla（区域旗帜 emoji 字体）

候选窗里 🇨🇳 / 🇺🇸 这类由两个区域指示符（U+1F1E6–U+1F1FF）拼成的国旗要用带国旗图案的
字体。Windows 自带的 Segoe UI Emoji 没有国旗字形，会把它们画成两个方框字母；本目录的
`Twemoji.Mozilla.ttf` 由候选窗以 DirectWrite **私有字体集**加载（`msimeui::LoadPrivateFontCollection`），
只作用于国旗序列，不安装进系统字体。

| 项 | 值 |
| --- | --- |
| 文件 | `Twemoji.Mozilla.ttf` |
| 版本 | Twemoji Mozilla v0.7.0（字体内部的 name 表为 `Version 0.7.0`），图形取自 Twemoji v14.0.1 |
| 大小 | 1,474,284 字节 |
| SHA256 | `6d90152ee0d29e82fe2a87793af5aa4b7ad13e6538360889e141e81ed299ee8e` |
| 下载 | https://github.com/mozilla/twemoji-colr/releases/download/v0.7.0/Twemoji.Mozilla.ttf |
| 授权 | 字体构建代码 Apache-2.0（Mozilla Foundation），图形 CC-BY-4.0（Twemoji）；见根目录 `THIRD_PARTY_NOTICES.txt` |

打包时 `installer/Prepare-PackageFiles.ps1` 把它拷到数据目录的 `assets\fonts\`，并核对上面的
SHA256；`installer/msime_setup.iss` 的轻量包分支也单独复制这一份。候选窗在数据目录里找不到
这个文件时，国旗退回 Segoe UI Emoji 的方框字母，其他候选照常显示。

更新字体：用新版本文件替换本目录的 `Twemoji.Mozilla.ttf`，同时更新本文件的版本、大小与
SHA256，并确认 `THIRD_PARTY_NOTICES.txt` 的授权说明仍然成立。
