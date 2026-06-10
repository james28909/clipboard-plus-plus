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
            Application::Get()->OpenSettingsWindow();
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        PostMessageW(m_hwnd, WM_SHOWTRAYPOPUP, 0, 0);
        break;
    }
}

void TrayIcon::SetIncognito(bool on) {
    m_incognito = on;
    UpdateIcon();
}

void TrayIcon::UpdateIcon() {
    // TODO (Milestone 8): swap to incognito icon when m_incognito is true
    // For now, keep the placeholder icon
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}
