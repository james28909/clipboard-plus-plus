#pragma once
#include <string>
#include <windows.h>

// Lightweight, self-contained GDI-based notification toast.
// ShowToast() is thread-safe and can be called from any thread.
// The window is WS_EX_NOACTIVATE + WS_EX_TOPMOST and auto-dismisses after
// durationMs milliseconds (default 2000).
namespace ToastWindow {
    void Show(const std::wstring& message, DWORD durationMs = 2000);
}
