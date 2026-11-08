# 本地翻译组件（实现底座）

当前已完成引擎、组件下载、草稿状态机与纯函数测试，**尚未完成翻译模式的实际输入链路**。
不能把这些提交当作已经可用的 Ctrl+T 翻译模式：按键、工具栏、候选窗与状态角标仍待接线。

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
  词级释义由调用者从 english.db 提供。等待/失败时 Enter 保留中文，返回需要提示的标记。
  不包含任何调频、词库写入或用户文本日志。
- `Worker`：单运行请求 + 单覆盖待处理请求，代际取消、迟到结果丢弃、超时与析构 join。
  完成回调在后台线程，**必须经过 Server 现有 owner/focus token 校验与任务队列才能修改 UI**。
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

待人工上传到 `Yu-DaTouX/yantai-ime` 的 `translate-model-v1` Release：

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

## 待完成（不是已实现功能）

1. `HandleImeKey`、`ProcessSelectionKey` 接入 State/Worker，翻译选词在任何调频/造词前截断；
   空格/数字/鼠标/标点均只进草稿，正在组字进入时接受当前候选；退出、失焦、重置取消旧请求。
2. IPC 末尾追加模式通知；TSF TestKeyDown/实际派发共用 `TranslationModeKeyPolicy.h`。
   此纯策略目前**仅在测试中使用**，未改变 TSF 实际行为。默认 Ctrl+T 仅组字/已在模式时接管，
   空闲必须给宿主；Enter、Shift+Enter、Ctrl+Enter、Backspace、Esc 交 Server。
3. `[keybindings] translate_mode = "Ctrl+T"`、出厂模板、设置读写映射；悬浮工具栏可隐藏的「译」按钮，
   空闲点击进入/退出；模式角标。未改现有配置格式/默认值。
4. 候选窗的草稿行/最新词下划线/前段省略，360 DIP 最小宽与两行英文省略、完整译文提交；
   Missing/等待/超时文案及跳设置动作。目前没有改候选绘制、ui/、skins/，避免与并行排版工作冲突。
5. 主线程每帧或定时 `TakeDueRequest`/`Tick`，后台完成经 owner/focus 校验后 `Apply`；
   初始化先调用 `translation_model::ResolveDirectory()` 处理待删除组件，再给 `LocalInference` 实际目录。
6. 真机 TSF/浏览器冲突、候选窗 DPI/主题/长句回归仍需完成；本次未部署、未向前台发键、未触碰运行中进程。
