#include "TrayIcon.h"
#include "Application.h"

TrayIcon::TrayIcon(HWND hwnd, HINSTANCE hInstance)
    : m_hwnd(hwnd), m_hInstance(hInstance)
{}

TrayIcon::~TrayIcon() {
    Destroy();
}

bool TrayIcon::Create() {
    m_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd             = m_hwnd;
    m_nid.uID              = 1;
    m_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon            = LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION placeholder
    wcscpy_s(m_nid.szTip, L"Clipboard++");

    if (!Shell_NotifyIconW(NIM_ADD, &m_nid))
        return false;

    m_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &m_nid);

    m_created = true;
    return true;
}

void TrayIcon::Destroy() {
    if (m_created) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_created = false;
    }
}

void TrayIcon::HandleMessage(WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(lParam)) {
    case WM_LBUTTONDBLCLK:
        if (Application::Get())
            Application::Get()->ShowMainWindow();
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowContextMenu();
        break;
    }
}

void TrayIcon::SetIncognito(bool on) {
    m_incognito = on;
    UpdateIcon();
}

void TrayIcon::ShowContextMenu() {
    HMENU hMenu = CreatePopupMenu();

    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_OPEN,  L"Open Clipboard++");
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_POPUP, L"Show Popup");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu,
        MF_STRING | (m_incognito ? MF_CHECKED : 0),
        TRAY_CMD_INCOGNITO, L"Incognito Mode");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_ABOUT, L"About");
    AppendMenuW(hMenu, MF_STRING, TRAY_CMD_EXIT,  L"Exit");

    // Make the first item the default (bold, activated on double-click)
    SetMenuDefaultItem(hMenu, TRAY_CMD_OPEN, FALSE);

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd); // required by TrackPopupMenu contract

    UINT cmd = static_cast<UINT>(
        TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                       pt.x, pt.y, 0, m_hwnd, nullptr));
    DestroyMenu(hMenu);

    Application* app = Application::Get();
    switch (cmd) {
    case TRAY_CMD_OPEN:
        if (app) app->ShowMainWindow();
        break;
    case TRAY_CMD_POPUP:
        // TODO (Milestone 3): show popup window
        break;
    case TRAY_CMD_INCOGNITO:
        SetIncognito(!m_incognito);
        break;
    case TRAY_CMD_ABOUT:
        MessageBoxW(m_hwnd,
            L"Clipboard++ v0.1\n\nA lean, modern clipboard manager for Windows.",
            L"About Clipboard++", MB_OK | MB_ICONINFORMATION);
        break;
    case TRAY_CMD_EXIT:
        PostMessageW(m_hwnd, WM_DESTROY, 0, 0);
        break;
    }
}

void TrayIcon::UpdateIcon() {
    // TODO (Milestone 8): swap to incognito icon when m_incognito is true
    // For now, keep the placeholder icon
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}
