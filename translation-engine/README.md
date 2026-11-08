# 本地翻译组件

Server、TSF、候选窗、工具栏和设置入口已完成接线；操作说明与真机回归清单见
[docs/translation-mode.md](../docs/translation-mode.md)。已通过构建/自动测试及真实后台模型推理冒烟，
未部署到已安装输入法，尚未进行真实宿主人工验收。

## 已验证

- CTranslate2 **4.8.1 / 0d8bcd362ac75ef860ef161d6f0efad0ae439ff0** 从源码编译，静态封进
  `yantai_translate.dll`；oneDNN 3.12、SentencePiece 0.2.1（vcpkg）。CPU-only、int8、2 线程，
  `WITH_MKL=OFF`、`WITH_CUDA=OFF`、`OPENMP_RUNTIME=COMP`，不依赖 Python。
- DLL 的版本化 C ABI 位于 `server/src/translation/engine_api.h`。Server 只链接轻量包装，
  首次后台推理才 `LoadLibraryExW`；搜索范围限组件目录和 System32。没有引擎或依赖时返回 Missing。
- **运行时必须 pin 到进程退出**：已复现 oneDNN/MSVC OpenMP 在推理后 `FreeLibrary` 崩溃；
  当前销毁模型并释放内存，但不卸载运行时 DLL。已测试翻译→取消→释放模型→重载→缺失 DLL，安全退出。
  删除组件沿用 deferred-delete，释放 DLL 需要重启 Server。
- 组件路径为 `<DataDir>/models/translate-zh-en/`；设置页有下载、浏览器下载、导入、删除，
  下载/导入走 `ModelDownloader`，核对整个 ZIP 大小与 SHA256，后台解包，最后发布 `.installed` 收据。
  不保留第二份 ZIP；压缩包携带所有非系统 DLL、模型、source/target SentencePiece 模型及许可证。
- 独立 smoke 使用给定的 OPUS-MT int8 权重，输出
  `Today's meeting has been rescheduled to 3 p.m.`。非 ASCII 模型目录实测：加载 275 ms，
  翻译 55 ms，峰值工作集 118,599,680 字节。这是一次冷启动冒烟，不是延迟分位数基准。
- `State`：词级草稿/标点、退格、Esc 两阶段、三种 Enter、100 ms 去抖、1 s 超时、代际隔离；
  控制器从 english.db 取得词级释义。翻译中按 Enter 会异步等待当前请求，超时/失败才上屏中文并提示。
  不包含任何调频、词库写入或用户文本日志。
- `Worker`：单运行请求 + 单覆盖待处理请求，代际取消、迟到结果丢弃、超时与析构 join。
  完成回调只写单槽结果，Server 任务线程每约 20 ms 在模式中轮询，经 client/epoch 校验后发布不可变界面快照。
- 已用实际 ZIP 跑设置组件 SHA256→导入→解包→删除测试；所有测试只使用隔离临时 DataDir。

## 构建与发布资产

新依赖限定放在工作树或相邻 `translate-deps`；脚本不安装、不部署、不启动 Server。
例如（PowerShell 7，CMake/VS 2026 已在 PATH）：

```powershell
pwsh -File translation-engine/build.ps1 -VcpkgRoot E:/vcpkg -DependenciesRoot E:/yantai-wt/translate-deps
python translation-engine/package.py --build E:/yantai-wt/translate-deps/engine-build --ct2 E:/yantai-wt/translate-deps/CTranslate2 --dependencies E:/yantai-wt/translate-deps/installed/x64-windows --model E:/yantai-wt/mt-probe/models/opus-mt-zh-en-int8 --source-spm E:/yantai-wt/translate-deps/source.spm --target-spm E:/yantai-wt/translate-deps/target.spm --output E:/yantai-wt/translate-deps/release
```

本次 vcpkg 编译单项最大约 7.6 GiB（oneDNN，含 Debug/Release 中间文件）；完成后已清理 Debug
中间文件。CT2 v4.8.1 的 GPU thrust 子模块引用已失效，因此脚本只获取 CPU 所需的两个子模块。
SentencePiece 的 Windows port 强制静态库，脚本保留其 protobuf-lite/Abseil 的传递链接依赖。

组件已由维护者上传到 `Yu-DaTouX/yantai-ime` 的 `translate-model-v1` Release，代码固定指向该资产：

- `translate-zh-en-v1.zip`：**81,237,882 字节**；
  SHA256 **c18b9d8b9962c1a1eea9b365b70b21d5f559299f35a8a35cec517f6507fe4cf1**。
- 建议同时上传 `component.lock.json`、`SHA256SUMS.txt`；它们与 ZIP 位于
  `E:/yantai-wt/translate-deps/release/`。分文件清单也已提交到本目录。
- 引擎二进制合计 **39,347,200 字节**（桥接/CT2/SentencePiece 1,549,824；oneDNN 34,280,960；
  protobuf-lite 1,507,328；Abseil 2,009,088）。不包含系统 VC/OpenMP 运行库。
- 包含桥接 GPL-3.0 代码，发布时同时提供本仓对应源码及构建说明；上游许可均在 ZIP 的 `licenses/`。
  模型 CC-BY-4.0、CT2 MIT、oneDNN/SentencePiece Apache-2.0。未创建或推送 Release。

**重编译会改变 DLL 和 ZIP 的摘要**：重打包后须同步本目录 lock 与
`server/src/settings/translation_model.cpp` 的固定大小/摘要，再上传对应的原文件；不要仅重压 ZIP。

## 输入链路与兼容性

- `server/src/translation/translation_mode.cpp` 是控制器；所有按键和鼠标选词在调频/造词前进草稿，
  Enter 经现有 TSF 非阻塞提交轮询返回，失焦/重置取消请求。右键词库变更在模式中禁用。
- 新增可选协议能力 `TranslationMode`（bit 9），旧客户端不会进入此模式；旧字段和帧大小不变。
  模式/鼠标组合恢复通知追加在 Worker opcode 43/44；长译文按回复 opcode 16/17 分片，
  TSF 完整拼接后只做一次 edit session 提交，不受候选窗两行省略或 199 UTF-16 码元单帧限制。
- 空闲 Ctrl+T 交宿主，工具栏「译」进入/退出；键位和按钮可在设置页改动。
- 独立附加行放在 `candidate_translation_panel.h`，未更改通用 ui/ 或皮肤清单；主线的
  `contentPadRight = contentPadLeft + textPadLeft` 及 emoji 字体/候选对齐回归均保持通过。
- `translation_worker_smoke` 用真实组件验证后台加载、中文路径、INT8 输出、deadline 和 shutdown。
  `test_translation_mode.cpp` 使用真实引擎/词典/控制器，仅替换 IPC/窗口边界，不启动 Server。

真机 TSF/浏览器冲突、不同 DPI 实际绘制和宿主接受 edit session 的行为仍须由维护者部署后验收；
本次没有部署、没有向前台发键、没有触碰运行中输入法进程或用户数据目录。
