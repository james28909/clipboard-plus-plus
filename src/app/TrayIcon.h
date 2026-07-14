#pragma once
#include <windows.h>
#include <shellapi.h>
#include "../ui/Appearance.h"
#include <string>

class TrayIcon {
public:
    TrayIcon(HWND hwnd, HINSTANCE hInstance);
    ~TrayIcon();

    bool Create();
    void Destroy();
    void HandleMessage(WPARAM wParam, LPARAM lParam);
    void SetIncognito(bool on);
    void ApplyTheme(const AppearanceSettings& ap);

    // Renders the themed clipboard icon at 16/32/48 px and writes an .ico file.
    static bool WriteThemeIco(const AppearanceSettings& ap, const std::wstring& outPath);
    // Returns a short hash string of the icon-related theme colors for change detection.
    static std::string ThemeIconHash(const AppearanceSettings& ap);

private:
    void UpdateIcon();
    HICON BuildHIcon(int sz, const AppearanceSettings& ap);

    HWND      m_hwnd{};
    HINSTANCE m_hInstance{};
    NOTIFYICONDATAW m_nid{};
    bool      m_created{false};
    HICON     m_themedIcon{nullptr};
};
