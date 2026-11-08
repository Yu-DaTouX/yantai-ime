# 本地全拼输入法（水杉输入法分支）

一款只做全拼、只在本机工作的 Windows 中文输入法。不登录、不上传、不打扰。

> 名称待定。本仓库是 [水杉输入法 Windows 版](https://github.com/metasequoiaime/msime-windows) v0.13.2 的精简分支，独立维护和发版。

[下载](https://github.com/Yu-DaTouX/yantai-ime/releases/latest) · [隐私说明](PRIVACY.md) · [功能说明](docs/features.md) · [English](README.en.md)

[![Release](https://img.shields.io/github/v/release/Yu-DaTouX/yantai-ime?include_prereleases&label=release)](https://github.com/Yu-DaTouX/yantai-ime/releases)
[![License](https://img.shields.io/github/license/Yu-DaTouX/yantai-ime)](LICENSE)

## 和上游有什么不同

输入法看得见你打的每一个字。这个分支把和全拼本地输入无关的东西都删了，剩下的部分默认不联网。

- **只留全拼**：删掉了双拼、五笔、辅助码、日语、语音输入、手写和屏幕键盘、AI 助手、在线翻译和云候选、输入统计。
- **默认不联网**：拼音解析、整句组词（含本机神经网络重排）、调频和个人词库都在本机完成。联网只有两处：检查更新，以及你自己点的模型下载。日志里不记录你打的字。
- **自动更新**：只从本仓库的 [GitHub Releases](https://github.com/Yu-DaTouX/yantai-ime/releases) 检查新版，下载后校验 SHA256。检查更新只会让 GitHub 看到你的 IP，不附带任何输入内容；可以在设置里关掉。
- **候选窗更稳**：修复了亚克力 / 云母皮肤下候选文字逐键上下跳一像素的问题。
- **纯 TSF 实现**：面向 Windows 10 / 11，在浏览器、Office、终端和游戏里都按系统标准输入框架工作。

## 安装

1. 到 [Releases](https://github.com/Yu-DaTouX/yantai-ime/releases/latest) 下载最新的安装包。
2. 运行安装包，按向导装完。
3. 用 `Win + 空格` 切换过去。

切换不过去、或者服务反复退出时，多半是缺少 [Visual C++ 2015–2022 运行库（x64）](https://aka.ms/vs/17/release/vc_redist.x64.exe)，装上后重新登录即可。

> 目前的安装包没有代码签名，Windows SmartScreen 可能提示"无法识别的应用"，点"更多信息 → 仍要运行"即可。每个版本的 SHA256 都附在 Release 页上，可以自行核对。

## 版本

从 **v0.1.0** 开始独立发版，更新记录见 [CHANGELOG](CHANGELOG.md)。0.x 阶段功能和设置项可能还会调整。

## 从源码构建

本机构建需要 Visual Studio（C++ 桌面开发）、CMake 和 vcpkg，各组件的构建方式见 [CONTRIBUTING.md](CONTRIBUTING.md)。仓库路径请用纯英文，含中文的路径会让 MSVC 链接失败。

没有本机工具链也可以用 GitHub Actions：推送任意特性分支会自动触发 `dev-build` 工作流，在 Actions 页面下载 `dev-installer` 产物即可得到安装包。

## 跟进上游

上游修了的问题按提交对照着手工移植：`bash scripts/upstream-review.sh` 列出上次看过以后、改到本仓仍保留文件的上游提交；看完用 `--mark` 记下位置。

## 反馈

遇到问题或有想法，欢迎在 [Issues](https://github.com/Yu-DaTouX/yantai-ime/issues) 里提。提交日志或截图前，请确认里面没有你的输入内容或账号。

## 来源与致谢

输入引擎、词库和大部分功能都来自 [水杉输入法](https://github.com/metasequoiaime/msime-windows) 作者和贡献者的工作，在此致谢。本项目与水杉输入法官方无关，问题请在本仓库反馈，不要提到上游。

## 开源协议

[GPL-3.0](LICENSE)，与上游一致。原项目的版权声明保留在源码和 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) 中。
