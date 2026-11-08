# Local Quanpin IME (a Metasequoia IME fork)

A Windows Chinese input method that does Quanpin only and works entirely on your computer: no account, no upload, no nagging.

> Name to be decided. This repository is a trimmed-down fork of [Metasequoia IME for Windows](https://github.com/metasequoiaime/msime-windows) v0.13.2, maintained and released independently.

[Download](https://github.com/Yu-DaTouX/yantai-ime/releases/latest) · [Privacy](PRIVACY.md) · [中文](README.md)

## How it differs from upstream

- **Quanpin only**: Shuangpin, Wubi, auxiliary codes, Japanese, voice input, handwriting and on-screen keyboard, the AI assistant, online translation, cloud candidates and input statistics are removed.
- **Offline by default**: pinyin parsing, sentence composition (including local neural reranking), frequency learning and your personal dictionary all stay on your machine. The only network requests are the update check and model downloads you start yourself. Logs never contain what you type.
- **Automatic updates**: checks only this repository's GitHub Releases and verifies SHA256. Can be turned off.
- **Steadier candidate window**: fixes the one-pixel per-keystroke jitter under the acrylic and mica skins.
- **Pure TSF** for Windows 10 / 11.

## Install

Download the latest installer from [Releases](https://github.com/Yu-DaTouX/yantai-ime/releases/latest), run it, then switch with `Win + Space`. If the IME does not appear, install the [Visual C++ 2015–2022 Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

Installers are currently unsigned; SmartScreen may warn. SHA256 checksums are published with every release.

## Origin

The input engine, dictionaries and most features come from the work of the Metasequoia IME authors and contributors. This project is not affiliated with Metasequoia IME; please report issues here.

## License

[GPL-3.0](LICENSE), same as upstream.
