#pragma once
#include <windows.h>
#include <shellapi.h>

// Context menu command IDs
enum TrayMenuCmd : UINT {
    TRAY_CMD_OPEN      = 1001,
    TRAY_CMD_POPUP     = 1002,
    TRAY_CMD_INCOGNITO = 1003,
    TRAY_CMD_ABOUT     = 1004,
    TRAY_CMD_EXIT      = 1005,
};

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
    void ShowContextMenu();
    void UpdateIcon();

    HWND      m_hwnd{};
    HINSTANCE m_hInstance{};
    NOTIFYICONDATAW m_nid{};
    bool      m_created{false};
    bool      m_incognito{false};
};
