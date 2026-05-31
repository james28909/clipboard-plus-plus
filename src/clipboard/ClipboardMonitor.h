#pragma once
#include <windows.h>
#include <functional>
#include "ClipboardItem.h"

class ClipboardMonitor {
public:
    using ItemCallback = std::function<void(ClipboardItem)>;

    ClipboardMonitor();
    ~ClipboardMonitor();

    // Creates a message-only window and registers for WM_CLIPBOARDUPDATE.
    // onItem is called on the main thread whenever a new item is captured.
    bool Start(HINSTANCE hInstance, ItemCallback onItem);
    void Stop();

    bool IsRunning() const { return m_hwnd != nullptr; }

    // Call this before writing to the clipboard yourself to suppress echo updates.
    void SuppressNextUpdate();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnClipboardUpdate();
    ClipboardItem ReadClipboard() const;
    static std::string GetForegroundProcessName();
    static std::string WideToUtf8(const wchar_t* w, int len = -1);

    HWND         m_hwnd{};
    HINSTANCE    m_hInstance{};
    ItemCallback m_callback;
    ULONGLONG    m_ignoreUntilTick{};
    DWORD        m_lastSeq{};   // suppress duplicate WM_CLIPBOARDUPDATE
};
