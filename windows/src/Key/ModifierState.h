#pragma once

#include <windows.h>

// 修饰键读「正在处理的这条按键消息发出时」的状态。GetKeyState() 跟着线程的消息队列推进，
// GetAsyncKeyState() 读的是此刻的物理键盘：宿主卡顿时按键消息在队列里排队，等处理到 Ctrl+A 的 A
// 时 Ctrl 已经松开，用 GetAsyncKeyState() 就会把 Ctrl+A 读成字母 a，被吃进组字（问题清单第 1 条）。
// 按键路由、热键判定和 V 模式 Shift 判定都用这里的函数；Global::UpdateModifiers 用的也是同一口径。
//
// 有意读真实键盘的地方不要改成这些函数：
// - KeyEventSink_Hotkeys.cpp 的 IsOnlyModifierPhysicallyDown()：裸修饰键释放热键的预备。Word 会在
//   消息派发之外回调 sink，GetKeyState() 的快照可能还停在上一条键上，只能在释放时问物理键盘。
// - Composition.cpp 的 AreCaretModifiersPhysicallyDown()：合成方向键前检查，看的是「接下来宿主会
//   看到的」物理键盘。
inline bool IsKeyDownForCurrentMessage(int virtualKey)
{
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

inline bool IsShiftDownForCurrentMessage()
{
    return IsKeyDownForCurrentMessage(VK_SHIFT);
}

inline bool IsControlDownForCurrentMessage()
{
    return IsKeyDownForCurrentMessage(VK_CONTROL);
}

inline bool IsAltDownForCurrentMessage()
{
    return IsKeyDownForCurrentMessage(VK_MENU);
}

inline bool IsWindowsKeyDownForCurrentMessage()
{
    return IsKeyDownForCurrentMessage(VK_LWIN) || IsKeyDownForCurrentMessage(VK_RWIN);
}

// 原来在 KeyEventSink.cpp 里的 CaptureIpcModifiers()。位序与 key_route_policy::kShift/kControl/kAlt 一致：
// bit0 Shift、bit1 Ctrl、bit2 Alt。改成 GetKeyState() 之后，排队中的按键按它发出时的修饰键分类，
// 不再受处理它时物理键盘状态的影响。
inline UINT CaptureIpcModifiers()
{
    UINT modifiers = 0;
    if (IsShiftDownForCurrentMessage())
        modifiers |= 0b00000001;
    if (IsControlDownForCurrentMessage())
        modifiers |= 0b00000010;
    if (IsAltDownForCurrentMessage())
        modifiers |= 0b00000100;
    return modifiers;
}
