#include "ToastWindow.h"

#include <algorithm>
#include <string>

#pragma comment(lib, "msimg32.lib")

namespace {

static constexpr wchar_t kToastClass[] = L"CPPToastWnd";
static bool s_classRegistered = false;

struct ToastState {
    std::wstring message;
    DWORD        durationMs{2000};
    HWND         hwnd{};
};

static LRESULT CALLBACK ToastProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Kick the auto-dismiss timer
        auto* ts = reinterpret_cast<ToastState*>(
            reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ts));
        SetTimer(hwnd, 1, ts->durationMs, nullptr);
        return 0;
    }

    case WM_TIMER:
        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY: {
        auto* ts = reinterpret_cast<ToastState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        delete ts;
        return 0;
    }

    case WM_PAINT: {
        auto* ts = reinterpret_cast<ToastState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!ts) break;

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right;
        int h = rc.bottom;

        // Draw into a memory DC so we can call UpdateLayeredWindow
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        SelectObject(memDC, bmp);

        // Background: dark translucent rounded rect
        HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 30));
        HPEN   noPen   = CreatePen(PS_NULL, 0, 0);
        SelectObject(memDC, bgBrush);
        SelectObject(memDC, noPen);
        RoundRect(memDC, 0, 0, w, h, 12, 12);
        DeleteObject(noPen);
        DeleteObject(bgBrush);

        // Text
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(240, 240, 240));
        HFONT font = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        HFONT oldFont = static_cast<HFONT>(SelectObject(memDC, font));

        RECT textRc = { 12, 8, w - 12, h - 8 };
        DrawTextW(memDC, ts->message.c_str(), -1, &textRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(memDC, oldFont);
        DeleteObject(font);

        // Blit to layered window
        POINT ptSrc = { 0, 0 };
        SIZE  szWnd = { w, h };
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 220, 0 }; // 220/255 ~ 86% opaque
        UpdateLayeredWindow(hwnd, hdc, nullptr, &szWnd, memDC, &ptSrc, 0, &bf, ULW_ALPHA);

        DeleteDC(memDC);
        DeleteObject(bmp);

        EndPaint(hwnd, &ps);
        return 0;
    }

    default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void EnsureClassRegistered() {
    if (s_classRegistered) return;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = ToastProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // layered window — WM_ERASEBKGND not needed
    wc.lpszClassName = kToastClass;
    RegisterClassExW(&wc);
    s_classRegistered = true;
}

} // namespace

void ToastWindow::Show(const std::wstring& message, DWORD durationMs) {
    EnsureClassRegistered();

    // Measure the text to size the window
    HDC refDC = GetDC(nullptr);
    HFONT font = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(refDC, font));
    RECT textRc = {};
    DrawTextW(refDC, message.c_str(), -1, &textRc, DT_CALCRECT | DT_SINGLELINE);
    SelectObject(refDC, oldFont);
    DeleteObject(font);
    ReleaseDC(nullptr, refDC);

    int textW = textRc.right - textRc.left;
    int textH = textRc.bottom - textRc.top;
    int w = std::max(textW + 24, 160);
    int h = textH + 16;

    // Position just above the cursor
    POINT cursor;
    GetCursorPos(&cursor);
    int x = cursor.x - w / 2;
    int y = cursor.y - h - 12;

    // Clamp to primary monitor so it doesn't go off-screen
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(mon, &mi)) {
        x = std::clamp(x, (int)mi.rcWork.left, (int)(mi.rcWork.right  - w));
        y = std::clamp(y, (int)mi.rcWork.top,  (int)(mi.rcWork.bottom - h));
    }

    auto* ts        = new ToastState;
    ts->message     = message;
    ts->durationMs  = durationMs;

    ts->hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        kToastClass, L"",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, GetModuleHandleW(nullptr), ts);

    if (ts->hwnd) {
        ShowWindow(ts->hwnd, SW_SHOWNA);
        // Trigger initial paint (UpdateLayeredWindow needs a WM_PAINT)
        InvalidateRect(ts->hwnd, nullptr, FALSE);
        UpdateWindow(ts->hwnd);
    } else {
        delete ts;
    }
}
