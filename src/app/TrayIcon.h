#pragma once
#include <windows.h>
#include <shellapi.h>
#include "../ui/Appearance.h"

class TrayIcon {
public:
    TrayIcon(HWND hwnd, HINSTANCE hInstance);
    ~TrayIcon();

    bool Create();
    void Destroy();
    void HandleMessage(WPARAM wParam, LPARAM lParam);
    void SetIncognito(bool on);
    bool IsIncognito() const { return m_incognito; }
    void ApplyTheme(const AppearanceSettings& ap);

private:
    void UpdateIcon();
    HICON BuildHIcon(int sz, const AppearanceSettings& ap);

    HWND      m_hwnd{};
    HINSTANCE m_hInstance{};
    NOTIFYICONDATAW m_nid{};
    bool      m_created{false};
    bool      m_incognito{false};
    HICON     m_themedIcon{nullptr};
};
