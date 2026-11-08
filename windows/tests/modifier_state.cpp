// 修饰键按「正在处理的这条按键消息发出时」读取（GetKeyState），不是此刻的物理键盘
// （GetAsyncKeyState）。宿主卡顿时按键消息在队列里排队：处理到 Ctrl+A 的 A 时 Ctrl 已经松开，
// GetAsyncKeyState() 会把 A 读成字母 a 吃进组字（问题清单第 1 条）。
//
// SetKeyboardState() 写的是调用线程的键盘状态表——GetKeyState() 读的那一张——所以这里能在
// 不注入真实按键、不抢前台的前提下构造「消息发出时 Ctrl 按着、物理键盘上早已松开」。

#include "Key/KeyRoutingPolicy.h"
#include "Key/ModifierState.h"

#include <windows.h>
#include <cstdio>

namespace
{
int failures = 0;

void Check(const char *what, bool actual, bool expected)
{
    if (actual != expected)
    {
        std::fprintf(stderr, "FAIL %s: expected=%d actual=%d\n", what, expected, actual);
        ++failures;
    }
}

void CheckModifiers(const char *what, UINT actual, UINT expected)
{
    if (actual != expected)
    {
        std::fprintf(stderr, "FAIL %s: expected=0x%x actual=0x%x\n", what, expected, actual);
        ++failures;
    }
}

// Rewrite the calling thread's key state table, the one GetKeyState() reads. No key event is
// injected and the physical keyboard is left alone.
bool SetKeyStateInTable(int virtualKey, bool down)
{
    BYTE state[256] = {};
    if (!GetKeyboardState(state))
    {
        std::fprintf(stderr, "FAIL GetKeyboardState: %lu\n", GetLastError());
        ++failures;
        return false;
    }
    if (down)
    {
        state[virtualKey] |= 0x80;
    }
    else
    {
        state[virtualKey] &= static_cast<BYTE>(~0x80);
    }
    if (!SetKeyboardState(state))
    {
        std::fprintf(stderr, "FAIL SetKeyboardState: %lu\n", GetLastError());
        ++failures;
        return false;
    }
    return true;
}
} // namespace

int main()
{
    // 反向用例：状态表里没有 Ctrl 时采集不到 Ctrl，A 走本地分类（进组字）。
    SetKeyStateInTable(VK_CONTROL, false);
    SetKeyStateInTable(VK_SHIFT, false);
    SetKeyStateInTable(VK_MENU, false);
    CheckModifiers("empty table captures no modifiers", CaptureIpcModifiers(), 0);
    Check("a with no modifiers classifies locally",
          key_route_policy::RouteModifiedKey(CaptureIpcModifiers(), false, false) ==
              key_route_policy::ModifiedKeyDisposition::ClassifyLocally,
          true);

    // 修复前失效的那一步：消息发出时 Ctrl 按着，物理键盘上早已松开。老实现读
    // GetAsyncKeyState(VK_CONTROL)，这里必然读成 0，A 就被当成字母 a 吃掉了。
    SetKeyStateInTable(VK_CONTROL, true);
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)
    {
        // 有人真的按着 Ctrl 跑测试：物理键盘这一半不成立，留给采集断言。
        std::puts("NOTE: Ctrl is physically down; skipping the physical-keyboard half of the check");
    }
    else
    {
        Check("physical keyboard reports no ctrl", (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, false);
    }
    CheckModifiers("ctrl comes from the message-time table", CaptureIpcModifiers(), key_route_policy::kControl);
    Check("ctrl+a belongs to the application",
          key_route_policy::RouteModifiedKey(CaptureIpcModifiers(), false, false) ==
              key_route_policy::ModifiedKeyDisposition::Application,
          true);

    // Shift 与 Alt 走同一张表和同一套位序。
    SetKeyStateInTable(VK_SHIFT, true);
    SetKeyStateInTable(VK_MENU, true);
    CheckModifiers("shift+alt from the table", CaptureIpcModifiers(),
                   key_route_policy::kControl | key_route_policy::kShift | key_route_policy::kAlt);
    SetKeyStateInTable(VK_CONTROL, false);
    SetKeyStateInTable(VK_SHIFT, false);
    SetKeyStateInTable(VK_MENU, false);
    CheckModifiers("cleared table captures nothing", CaptureIpcModifiers(), 0);

    // Windows 键和 Ctrl/Alt 一样让路由把键交还宿主。
    SetKeyStateInTable(VK_LWIN, true);
    Check("left win from the table", IsWindowsKeyDownForCurrentMessage(), true);
    Check("win+a belongs to the application",
          key_route_policy::RouteModifiedKey(0, IsWindowsKeyDownForCurrentMessage(), false) ==
              key_route_policy::ModifiedKeyDisposition::Application,
          true);
    SetKeyStateInTable(VK_LWIN, false);
    SetKeyStateInTable(VK_RWIN, true);
    Check("right win from the table", IsWindowsKeyDownForCurrentMessage(), true);
    SetKeyStateInTable(VK_RWIN, false);
    Check("no win key in the table", IsWindowsKeyDownForCurrentMessage(), false);

    if (failures != 0)
        return 1;
    std::puts("PASS: modifiers are read as of the key message, not the physical keyboard");
    return 0;
}
