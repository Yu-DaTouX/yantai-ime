# Yantai IME (砚台输入法)

A privacy-first Chinese input method for Windows. Typing stays on your computer: no account, no upload, no nagging.

[Download](https://github.com/Yu-DaTouX/yantai-ime/releases/latest) · [Privacy](PRIVACY.md) · [中文](README.md)

## Highlights

- **Local first**: pinyin parsing, sentence composition, frequency learning and your personal dictionary all stay on your machine.
- **Network features are off by default**: cloud candidates, candidate translation, AI suggestions and cloud speech recognition ship disabled and are opt-in. Logs never contain what you type.
- **Automatic updates**: checks only this project's GitHub Releases, verifies SHA256, and asks before installing. Can be turned off.
- **Input schemes**: Quanpin, nine Shuangpin layouts, Wubi 86, and optional auxiliary codes.
- **Pure TSF** for Windows 10 / 11.

## Install

Download the latest installer from [Releases](https://github.com/Yu-DaTouX/yantai-ime/releases/latest), run it, then switch with `Win + Space`. If the IME does not appear, install the [Visual C++ 2015–2022 Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

Installers are currently unsigned; SmartScreen may warn. SHA256 checksums are published with every release.

## Origin

Yantai IME is a fork of [Metasequoia IME for Windows](https://github.com/metasequoiaime/msime-windows) (v0.13.2), trimmed down, with stricter privacy defaults, maintained and released independently starting at v0.1.0. Thanks to the Metasequoia IME authors and contributors. This project is not affiliated with Metasequoia IME; please report issues here.

## License

[GPL-3.0](LICENSE), same as upstream.
