#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include "ClipboardItem.h"

class ImageStore;

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

    // Must be set before Start() for images to be stored in the DB.
    void SetImageStore(ImageStore* store) { m_imageStore = store; }

    // Callback that returns the active clipboard profile ID at capture time.
    using ProfileIdGetter = std::function<std::string()>;
    void SetProfileIdGetter(ProfileIdGetter fn) { m_profileIdGetter = std::move(fn); }

    // Call this before writing to the clipboard yourself to suppress echo updates.
    void SuppressNextUpdate();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnClipboardUpdate();
    ClipboardItem ReadClipboard() const;
    static bool IsImageAvailable();
    void ReadImageFormats(ClipboardItem& item) const;
    static std::string GetForegroundProcessName();

    HWND         m_hwnd{};
    HINSTANCE    m_hInstance{};
    ItemCallback m_callback;
    ImageStore*    m_imageStore{};
    ProfileIdGetter m_profileIdGetter;
    ULONGLONG    m_ignoreUntilTick{};
    DWORD        m_lastSeq{};   // suppress duplicate WM_CLIPBOARDUPDATE
};
