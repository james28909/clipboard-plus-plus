#pragma once
#include <windows.h>
#include <atomic>
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

    // Disabling capture keeps the Windows clipboard listener alive, but consumes
    // updates without reading or storing their contents. Re-enabling starts with
    // the current clipboard sequence so incognito content is never captured late.
    void SetCaptureEnabled(bool enabled);
    bool IsCaptureEnabled() const { return m_captureEnabled; }

    // Must be set before Start() for images to be stored in the DB.
    void SetImageStore(ImageStore* store) { m_imageStore = store; }

    // Callback that returns the active clipboard profile ID at capture time.
    using ProfileIdGetter = std::function<std::string()>;
    void SetProfileIdGetter(ProfileIdGetter fn) { m_profileIdGetter = std::move(fn); }

    // Suppress the next clipboard sequence change. This is reserved for writes
    // initiated by another process (for example, a synthetic Ctrl+C).
    void SuppressNextUpdate();

    // Bracket Clipboard++'s own clipboard writes. The final sequence number is
    // ignored exactly, avoiding both delayed self-capture and a broad time window
    // that could swallow an unrelated user copy.
    void BeginSelfWrite();
    // Call after EmptyClipboard while the clipboard is open. A private token
    // lets later delayed-format notifications remain attributable to this write.
    void MarkSelfWrite();
    void EndSelfWrite();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnClipboardUpdate();
    bool ClipboardHasCurrentSelfWriteToken() const;
    ClipboardItem ReadClipboard() const;
    static bool IsImageAvailable();
    void ReadImageFormats(ClipboardItem& item) const;
    static std::string GetForegroundProcessName();

    HWND         m_hwnd{};
    HINSTANCE    m_hInstance{};
    ItemCallback m_callback;
    ImageStore*    m_imageStore{};
    ProfileIdGetter m_profileIdGetter;
    std::atomic_bool  m_suppressNextArmed{false};
    std::atomic<DWORD> m_suppressNextBaseline{};
    std::atomic<DWORD> m_suppressedSelfSeq{};
    std::atomic<DWORD> m_selfWriteStartSeq{};
    std::atomic_int    m_selfWriteDepth{};
    std::atomic<uint64_t> m_selfWriteToken{};
    DWORD        m_lastSeq{};   // suppress duplicate WM_CLIPBOARDUPDATE
    bool         m_captureEnabled{true};
    // Image-capture debounce: Win11 fires WM_CLIPBOARDUPDATE twice per screenshot
    uint64_t     m_lastImgHash{};
    int          m_lastImgW{};
    int          m_lastImgH{};
    ULONGLONG    m_lastImgTickMs{};
};
