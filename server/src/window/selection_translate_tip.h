#pragma once

#include <windows.h>

// 选中翻译的一行提示（没选中文字、组件未安装……）。贴在悬浮工具栏旁边，不抢焦点、鼠标穿透，几秒后自己消失。
// 所有函数都在拥有工具栏窗口的 UI 线程上调用。
namespace SelectionTranslateTip
{
void Show(HWND toolbar, const wchar_t *text);
void Hide();
} // namespace SelectionTranslateTip
