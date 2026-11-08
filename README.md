# 砚台输入法

一款注重隐私的 Windows 中文输入法。打字在本机完成，不登录、不上传、不打扰。

[下载](https://github.com/Yu-DaTouX/yantai-ime/releases/latest) · [隐私说明](PRIVACY.md) · [功能说明](docs/features.md) · [English](README.en.md)

[![Release](https://img.shields.io/github/v/release/Yu-DaTouX/yantai-ime?include_prereleases&label=release)](https://github.com/Yu-DaTouX/yantai-ime/releases)
[![License](https://img.shields.io/github/license/Yu-DaTouX/yantai-ime)](LICENSE)

## 为什么做砚台

输入法看得见你打的每一个字，密码、聊天、病历、合同都要经过它。很多输入法把"云端"当默认值，把"上传"写进几千字的协议里。

砚台反过来：**默认什么都不发出去**。模型下载与检查更新都需要你主动触发，不在后台自动进行。

## 特点

- **本机优先**：拼音解析、整句组词、调频和个人词库都在本机完成，个人词库只存在你自己的电脑上。
- **默认不联网**：全拼输入和候选词释义都在本机完成；模型下载与检查更新只在你自己点的时候发出请求，日志里不记录你打的字。
- **自动更新**：只从本项目的 [GitHub Releases](https://github.com/Yu-DaTouX/yantai-ime/releases) 检查新版，下载后校验 SHA256，安装前会先问你。检查更新只会让 GitHub 看到你的 IP，不会附带任何输入内容。可以在设置里关掉。
- **输入方案**：全拼。
- **纯 TSF 实现**：面向 Windows 10 / 11，在浏览器、Office、终端和游戏里都按系统标准输入框架工作。
- **候选窗更稳**：修复了亚克力 / 云母皮肤下候选文字逐键上下跳一像素的问题。

## 安装

1. 到 [Releases](https://github.com/Yu-DaTouX/yantai-ime/releases/latest) 下载最新的安装包。
2. 运行安装包，按向导装完。
3. 用 `Win + 空格` 切换到砚台输入法。

切换不过去、或者服务反复退出时，多半是缺少 [Visual C++ 2015–2022 运行库（x64）](https://aka.ms/vs/17/release/vc_redist.x64.exe)，装上后重新登录即可。

> 目前的安装包没有代码签名，Windows SmartScreen 可能提示"无法识别的应用"，点"更多信息 → 仍要运行"即可。每个版本的 SHA256 都附在 Release 页上，可以自行核对。

## 版本

砚台从 **v0.1.0** 开始独立发版，更新记录见 [CHANGELOG](CHANGELOG.md)。0.x 阶段功能和设置项可能还会调整。

## 从源码构建

本机构建需要 Visual Studio 2022（C++ 桌面开发）、CMake 和 vcpkg，各组件的构建方式见 [CONTRIBUTING.md](CONTRIBUTING.md)。

没有本机工具链也可以用 GitHub Actions：推送任意特性分支会自动触发 `dev-build` 工作流，在 Actions 页面下载 `dev-installer` 产物即可得到安装包。

## 反馈

遇到问题或有想法，欢迎在 [Issues](https://github.com/Yu-DaTouX/yantai-ime/issues) 里提。提交日志或截图前，请确认里面没有你的输入内容、账号或 API Key。

## 来源与致谢

砚台输入法是 [水杉输入法 Windows 版](https://github.com/metasequoiaime/msime-windows)（v0.13.2）的分支，在此基础上精简代码、收紧隐私默认值，并独立维护和发版。感谢水杉输入法的作者和所有贡献者，砚台的输入引擎、词库和大部分功能都来自他们的工作。

本项目与水杉输入法官方无关，问题请在本仓库反馈，不要提到上游。

## 开源协议

[GPL-3.0](LICENSE)，与上游一致。原项目的版权声明保留在源码和 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) 中。
