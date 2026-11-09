#include "window/selection_translate_tip.h"

#include <algorithm>
#include <string>

namespace SelectionTranslateTip
{
namespace
{
constexpr wchar_t kClassName[] = L"MetasequoiaImeSelectionTranslateTip";
constexpr UINT_PTR kHideTimer = 1;
constexpr UINT kHideAfterMs = 3000;

HWND g_window = nullptr;
std::wstring g_text;
UINT g_dpi = 96;

int Scale(int value)
{
    return MulDiv(value, static_cast<int>(g_dpi), 96);
}

HFONT CreateTipFont()
{
    return CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

LRESULT CALLBACK TipProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_TIMER:
        if (wParam == kHideTimer)
        {
            Hide();
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        HBRUSH background = CreateSolidBrush(RGB(38, 38, 38));
        FillRect(dc, &rect, background);
        DeleteObject(background);
        HFONT font = CreateTipFont();
        HGDIOBJ previous = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(245, 245, 245));
        DrawTextW(dc, g_text.c_str(), static_cast<int>(g_text.size()), &rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, previous);
        DeleteObject(font);
        EndPaint(hwnd, &paint);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool EnsureWindow()
{
    if (g_window && IsWindow(g_window))
        return true;
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSW cls{};
        cls.lpfnWndProc = TipProc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = kClassName;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
        registered = true;
    }
    g_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT, kClassName, L"",
                               WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    return g_window != nullptr;
}
} // namespace

void Show(HWND toolbar, const wchar_t *text)
{
    if (!toolbar || !IsWindow(toolbar) || !text || !*text || !EnsureWindow())
        return;
    g_text = text;
    g_dpi = GetDpiForWindow(toolbar);
    if (g_dpi == 0)
        g_dpi = 96;

    HDC dc = GetDC(g_window);
    HFONT font = CreateTipFont();
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE extent{};
    GetTextExtentPoint32W(dc, g_text.c_str(), static_cast<int>(g_text.size()), &extent);
    SelectObject(dc, previous);
    DeleteObject(font);
    ReleaseDC(g_window, dc);

    const int width = extent.cx + Scale(24);
    const int height = extent.cy + Scale(12);
    RECT bar{};
    GetWindowRect(toolbar, &bar);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(toolbar, MONITOR_DEFAULTTONEAREST), &monitor);
    const int gap = Scale(6);
    int x = (std::min<int>)(bar.left, monitor.rcWork.right - width);
    x = (std::max<int>)(x, monitor.rcWork.left);
    int y = bar.top - height - gap;
    if (y < monitor.rcWork.top)
        y = bar.bottom + gap;

    SetWindowRgn(g_window, CreateRoundRectRgn(0, 0, width + 1, height + 1, Scale(10), Scale(10)), TRUE);
    SetWindowPos(g_window, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_window, nullptr, TRUE);
    SetTimer(g_window, kHideTimer, kHideAfterMs, nullptr);
}

void Hide()
{
    if (!g_window || !IsWindow(g_window))
        return;
    KillTimer(g_window, kHideTimer);
    ShowWindow(g_window, SW_HIDE);
}
} // namespace SelectionTranslateTip
