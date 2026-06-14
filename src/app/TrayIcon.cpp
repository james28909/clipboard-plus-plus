#include "TrayIcon.h"
#include "Application.h"
#include <cstdio>
#include <vector>

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

// Shared drawing routine — renders the clipboard icon into a top-down BGRA pixel buffer.
static void DrawClipboardIcon(int sz, const AppearanceSettings& ap, DWORD* px) {
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

    float sc = sz / 32.0f;
    auto R  = [&](float v) { return (int)(v * sc + 0.5f); };
    auto R1 = [&](float v) { return std::max(1, (int)(v * sc + 0.5f)); };

    fillGrad(R(3), R(5), R(29), R(31), ap.iconBoardTop, ap.iconBoardBottom);

    ImVec4 clipCol{0.78f, 0.78f, 0.78f, 1.0f};
    fillRect(R(11), R(1), R(21), R(8), clipCol);

    fillRect(R(5), R(8), R(27), R(31), ap.iconPaper);

    int mlw = sz >= 24 ? 2 : 1;
    for (int y = R(9); y < R(30); y++)
        for (int dx = 0; dx < mlw; dx++)
            setpx(R(8) + dx, y, ap.iconMarginLine);

    for (int i = 0; i < 4; i++) {
        int y = R(11) + i * R1(5);
        for (int x = R(10); x < R(26); x++)
            setpx(x, y, ap.iconRuledLines);
    }
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
    DrawClipboardIcon(sz, ap, px);

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

std::string TrayIcon::ThemeIconHash(const AppearanceSettings& ap) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "%03d%03d%03d_%03d%03d%03d_%03d%03d%03d_%03d%03d%03d_%03d%03d%03d",
        (int)(ap.iconBoardTop.x    * 255 + 0.5f), (int)(ap.iconBoardTop.y    * 255 + 0.5f), (int)(ap.iconBoardTop.z    * 255 + 0.5f),
        (int)(ap.iconBoardBottom.x * 255 + 0.5f), (int)(ap.iconBoardBottom.y * 255 + 0.5f), (int)(ap.iconBoardBottom.z * 255 + 0.5f),
        (int)(ap.iconPaper.x       * 255 + 0.5f), (int)(ap.iconPaper.y       * 255 + 0.5f), (int)(ap.iconPaper.z       * 255 + 0.5f),
        (int)(ap.iconMarginLine.x  * 255 + 0.5f), (int)(ap.iconMarginLine.y  * 255 + 0.5f), (int)(ap.iconMarginLine.z  * 255 + 0.5f),
        (int)(ap.iconRuledLines.x  * 255 + 0.5f), (int)(ap.iconRuledLines.y  * 255 + 0.5f), (int)(ap.iconRuledLines.z  * 255 + 0.5f));
    return buf;
}

bool TrayIcon::WriteThemeIco(const AppearanceSettings& ap, const std::wstring& outPath) {
    // ICO structures (packed, no padding)
#pragma pack(push, 1)
    struct IcoDir   { WORD reserved, type, count; };
    struct IcoDirEntry {
        BYTE width, height, colorCount, reserved;
        WORD planes, bitCount;
        DWORD bytesInRes, imageOffset;
    };
#pragma pack(pop)

    static const int kSizes[] = {16, 24, 32, 48, 64, 128, 256};
    static const int kCount   = 7;

    // Render each size into a top-down pixel buffer
    std::vector<std::vector<DWORD>> frames(kCount);
    for (int i = 0; i < kCount; ++i) {
        int sz = kSizes[i];
        BITMAPV5HEADER bi = {};
        bi.bV5Size        = sizeof(bi);
        bi.bV5Width       = sz;
        bi.bV5Height      = -sz; // top-down
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
        if (!hbm) return false;

        auto* px = reinterpret_cast<DWORD*>(bits);
        memset(px, 0, sz * sz * 4);
        DrawClipboardIcon(sz, ap, px);
        frames[i].assign(px, px + sz * sz);
        DeleteObject(hbm);
    }

    // Build the ICO file in memory.
    // Each entry image = BITMAPINFOHEADER (40 bytes) + pixel rows (bottom-up) + 1bpp AND mask.
    // biHeight is set to sz*2 per ICO spec (combined color + mask height).
    std::vector<std::vector<char>> images(kCount);
    for (int i = 0; i < kCount; ++i) {
        int sz        = kSizes[i];
        int andStride = ((sz + 31) / 32) * 4; // DWORD-aligned 1bpp rows
        int imgBytes  = 40 + sz * sz * 4 + sz * andStride;
        images[i].resize(imgBytes, '\0');
        char* p = images[i].data();

        auto* bih         = reinterpret_cast<BITMAPINFOHEADER*>(p);
        bih->biSize       = sizeof(BITMAPINFOHEADER);
        bih->biWidth      = sz;
        bih->biHeight     = sz * 2; // ICO convention: 2× actual height
        bih->biPlanes     = 1;
        bih->biBitCount   = 32;
        bih->biCompression = BI_RGB;
        p += sizeof(BITMAPINFOHEADER);

        // Pixel rows — ICO expects bottom-up, renderer produced top-down → reverse rows
        for (int row = sz - 1; row >= 0; --row) {
            memcpy(p, &frames[i][row * sz], sz * 4);
            p += sz * 4;
        }

        // AND mask — all zeros (alpha channel controls transparency for 32-bit icons)
        memset(p, 0, sz * andStride);
    }

    // Compute total file size and entry offsets
    size_t offset = sizeof(IcoDir) + kCount * sizeof(IcoDirEntry);
    std::vector<DWORD> entryOffsets(kCount);
    for (int i = 0; i < kCount; ++i) {
        entryOffsets[i] = static_cast<DWORD>(offset);
        offset += images[i].size();
    }

    std::vector<char> icoFile(offset);
    auto* dir         = reinterpret_cast<IcoDir*>(icoFile.data());
    dir->reserved     = 0; dir->type = 1; dir->count = kCount;
    auto* entries = reinterpret_cast<IcoDirEntry*>(icoFile.data() + sizeof(IcoDir));
    for (int i = 0; i < kCount; ++i) {
        entries[i].width       = (kSizes[i] >= 256) ? 0 : static_cast<BYTE>(kSizes[i]);
        entries[i].height      = (kSizes[i] >= 256) ? 0 : static_cast<BYTE>(kSizes[i]);
        entries[i].colorCount  = 0;
        entries[i].reserved    = 0;
        entries[i].planes      = 1;
        entries[i].bitCount    = 32;
        entries[i].bytesInRes  = static_cast<DWORD>(images[i].size());
        entries[i].imageOffset = entryOffsets[i];
        memcpy(icoFile.data() + entryOffsets[i], images[i].data(), images[i].size());
    }

    FILE* fp = _wfopen(outPath.c_str(), L"wb");
    if (!fp) return false;
    bool ok = fwrite(icoFile.data(), 1, icoFile.size(), fp) == icoFile.size();
    fclose(fp);
    return ok;
}
