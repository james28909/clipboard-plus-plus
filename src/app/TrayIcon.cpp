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
    m_nid.hIcon = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                    LR_DEFAULTCOLOR);
    if (!m_nid.hIcon)
        m_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
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
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

HICON TrayIcon::BuildHIcon(int sz, const AppearanceSettings& ap) {
    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = sz;
    bi.bV5Height      = -sz;
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    void* bits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hbm = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi),
                                   DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hbm) return nullptr;

    auto* px = reinterpret_cast<DWORD*>(bits);
    memset(px, 0, sz * sz * 4);

    auto setpx = [&](int x, int y, const ImVec4& c) {
        if (x < 0 || y < 0 || x >= sz || y >= sz) return;
        int r = (int)(c.x * 255.0f + 0.5f);
        int g = (int)(c.y * 255.0f + 0.5f);
        int b = (int)(c.z * 255.0f + 0.5f);
        int a = (int)(c.w * 255.0f + 0.5f);
        px[y * sz + x] = ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
    };

    auto fillRect = [&](int x0, int y0, int x1, int y1, const ImVec4& c) {
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                setpx(x, y, c);
    };

    // Gradient fill for board (vertical blend top→bottom)
    auto fillGrad = [&](int x0, int y0, int x1, int y1,
                        const ImVec4& top, const ImVec4& bot) {
        int h = y1 - y0;
        if (h <= 0) return;
        for (int y = y0; y < y1; y++) {
            float t = (float)(y - y0) / (float)h;
            ImVec4 c{top.x + (bot.x - top.x) * t,
                     top.y + (bot.y - top.y) * t,
                     top.z + (bot.z - top.z) * t, 1.0f};
            for (int x = x0; x < x1; x++)
                setpx(x, y, c);
        }
    };

    // All coordinates designed at 32px, scaled to sz
    float sc = sz / 32.0f;
    auto R  = [&](float v) { return (int)(v * sc + 0.5f); };
    auto R1 = [&](float v) { return std::max(1, (int)(v * sc + 0.5f)); };

    // Board
    fillGrad(R(3), R(5), R(29), R(31), ap.iconBoardTop, ap.iconBoardBottom);

    // Clip (metallic gray, stays fixed)
    ImVec4 clipCol{0.78f, 0.78f, 0.78f, 1.0f};
    fillRect(R(11), R(1), R(21), R(8), clipCol);

    // Paper
    fillRect(R(5), R(8), R(27), R(31), ap.iconPaper);

    // Margin line (vertical, red) — 2px wide at sz>=24
    int mlw = sz >= 24 ? 2 : 1;
    for (int y = R(9); y < R(30); y++)
        for (int dx = 0; dx < mlw; dx++)
            setpx(R(8) + dx, y, ap.iconMarginLine);

    // 4 ruled lines (horizontal)
    for (int i = 0; i < 4; i++) {
        int y = R(11) + i * R1(5);
        for (int x = R(10); x < R(26); x++)
            setpx(x, y, ap.iconRuledLines);
    }

    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmColor = hbm;
    ii.hbmMask  = CreateBitmap(sz, sz, 1, 1, nullptr);
    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(ii.hbmMask);
    DeleteObject(hbm);
    return hIcon;
}

void TrayIcon::ApplyTheme(const AppearanceSettings& ap) {
    HICON newIcon = BuildHIcon(GetSystemMetrics(SM_CXSMICON), ap);
    if (!newIcon) return;

    if (m_themedIcon) DestroyIcon(m_themedIcon);
    m_themedIcon = newIcon;

    if (m_created) {
        m_nid.hIcon = m_themedIcon;
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    }
}
