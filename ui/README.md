# msimeui

`msimeui` 是一个基于 Win32 的 C++ GUI 工程，渲染层使用 Direct2D / DirectWrite，文本输入使用 TSF。

- 常规界面元素使用 Direct2D / DirectWrite 渲染
- 使用原生 Win32 窗口作为宿主
- 文本输入控件内置 TSF 实现
- 界面结构基于控件树和布局系统

## 当前内容

当前仓库包含：

- `msimeui` 静态库
- D2D/DWrite 设备资源管理
- 基础布局和控件树
- 共享布局属性：`margin` / `padding` / 显式尺寸 / `min-max` 约束 / 对齐
- `StackPanel` / `HorizontalStackPanel` / `WrapPanel` / `ScrollViewer` / `Grid`
- 通用单子元素容器：`Container` / `Border`
- `TextBlock` / `Button` / `CheckBox` / `ProgressBar` / `Slider` / `Separator` / `ListView` / `TreeView` / `TabControl` / `Accordion`
- `ComboBox`
- `CandidateList`
- `ContextMenuHost`
- `Card`
- `Image`（WIC 解码与 Direct2D 位图缓存）
- `TextBox` 输入控件

## 图片渲染

`Image` 控件可以从文件加载 WIC 支持的图片格式，例如 PNG、JPEG、BMP、ICO、TIFF，以及 APNG、GIF 动图：

```cpp
auto image = std::make_shared<msimeui::Image>(L"assets/sticker.png");
image->SetWidth(160.0f);
image->SetHeight(120.0f);
image->SetStretch(msimeui::ImageStretch::Uniform);
```

可用的缩放方式包括 `None`、`Fill`、`Uniform` 和 `UniformToFill`。位图按文件路径缓存在 `DeviceResources` 中，D2D 设备目标重建后会自动重新解码。

### 动图

`AnimatedImage` 按 [APNG 规范](https://wiki.mozilla.org/APNG_Specification) 解析 `acTL` / `fcTL` / `fdAT`（校验序号、CRC 与帧边界），把每帧重建成独立 PNG 交给 WIC 解码，再按 `dispose_op` / `blend_op` 合成到整张画布上；GIF 走 WIC 多帧解码，按 `/grctlext` 的延时与处置方式、`/imgdesc` 的子矩形和 NETSCAPE 循环次数合成；WIC 编解码器暴露多帧的 WebP 按每帧 100 ms 播放。延时规则同浏览器：分母为 0 按 100，≤ 10 ms 按 100 ms；播完 `num_plays` 后停在最后一帧。损坏、超过 1024 帧或解码后超过 64 MB 的动画退回静态首帧，不引入第三方依赖。

`Image`、`Card` 的背景与九宫格都通过 `DeviceResources::GetImageFrame` 取当前帧：

- 帧时间在 `BeginFrame` 采样，`SetAnimationEpoch` 设起点；`GetNextAnimationDeadline()`（或 `Scene::GetNextFrameDeadline()`）给出下一次换帧的时刻，按需重绘的宿主据此排一次定时器。`msimeui::Window` 自己会这样做。
- `SetAnimationsEnabled(false)` 按静态解码显示（APNG 的默认图、GIF 首帧，与 `GetBitmapFromFile` 相同）、不再给出截止时间；是否减少动画由宿主按系统设置决定。`ClearImageCaches()` 丢掉已解码的图片，磁盘上换过的文件下次绘制时重新读取。
- 动图的每帧都是 GPU 位图，合计受 `SetAnimatedBitmapBudget`（默认 64 MB）约束，按最近绘制淘汰；单个动画超出预算时只显示首帧。设备丢失后按需重建。

`DrawNineSlice` / `Card::SetBackgroundNineSlice` 实现 CSS `border-image`（`stretch`）：切片以源图像素计，边框宽度以 DIP 计，`fill` 决定是否画中心；各块用夹取到自身的图像画刷绘制并对齐设备像素，接缝不会互相渗色。

## 构建

如果已经配置好本机的 `vcpkg` 路径，可以直接使用 preset：

```powershell
cmake --preset vcpkg-debug
cmake --build --preset debug
```

如果要编译 `Release`：

```powershell
cmake --preset vcpkg-release
cmake --build --preset release
```

也可以继续使用普通 CMake 命令：

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

preset 里的 vcpkg 路径取自环境变量 `VCPKG_ROOT`，配置前设好它即可，不需要改文件：`$env:VCPKG_ROOT = 'C:\path\to\vcpkg'`。要写死本机路径的话，改 [CMakeUserPresets.json](CMakeUserPresets.json) 里的 `VCPKG_ROOT` 和 `CMAKE_TOOLCHAIN_FILE`。

也可以直接使用 `scripts/` 里的 PowerShell 脚本：

```powershell
./scripts/configure.ps1
./scripts/build.ps1
```

指定 `Release`：

```powershell
./scripts/build.ps1 -Configuration Release
```

如果切换过 CMake generator、Visual Studio 版本或者 preset，可以加 `-Fresh` 重新生成构建目录：

```powershell
./scripts/build.ps1 -Fresh
```

## 目录

- `include/msimeui`：新框架公开头文件
- `src`：框架实现
- `src/tsf`：内置 TSF 文本输入实现

## 后续工作

- 输入框要支持 Ctrl + Z 和 Ctrl + Y 的撤销和恢复这些功能
- 继续把场景级 invalidation 细化为局部布局和局部重绘，减少全树重排成本
- 基于现有 parent 链继续引入更细粒度的 subtree dirty 标记和可缓存测量结果
- 为高频交互控件继续补局部重绘区域计算，而不是默认整窗 `InvalidateRect`
- 继续完善 `TextBox` 的输入、样式和布局能力
- 增加菜单、弹出层、overlay 和更完整的命中/焦点管理
- 引入资源字典、主题和声明式界面描述

## 许可

GPL-3.0，与水杉输入法其余仓库一致。详见 [LICENSE](LICENSE)。
