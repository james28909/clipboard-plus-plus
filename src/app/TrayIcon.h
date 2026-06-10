#pragma once
#include <windows.h>
#include <shellapi.h>

class TrayIcon {
public:
    TrayIcon(HWND hwnd, HINSTANCE hInstance);
    ~TrayIcon();

    bool Create();
    void Destroy();
    void HandleMessage(WPARAM wParam, LPARAM lParam);
    void SetIncognito(bool on);
    bool IsIncognito() const { return m_incognito; }

private:
    void UpdateIcon();

    HWND      m_hwnd{};
    HINSTANCE m_hInstance{};
    NOTIFYICONDATAW m_nid{};
    bool      m_created{false};
    bool      m_incognito{false};
};
