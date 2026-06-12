#include "DbViewer.h"
#include "../../third_party/sqlite/sqlite3.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), -1, nullptr, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), -1, w.data(), n);
    return w;
}

// ---------------------------------------------------------------------------
// Recents path
// ---------------------------------------------------------------------------
static std::wstring GetRecentsPath() {
    wchar_t appdata[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) return {};
    return std::wstring(appdata) + L"\\sqlite_editor_recents.txt";
}

// ---------------------------------------------------------------------------
// Win32 window
// ---------------------------------------------------------------------------
static constexpr wchar_t kWndClass[] = L"SqliteEditorWindow";

LRESULT CALLBACK DbViewerApp::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    auto* app = reinterpret_cast<DbViewerApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_SIZE:
        if (app && wParam != SIZE_MINIMIZED) {
            app->m_resizeW = LOWORD(lParam);
            app->m_resizeH = HIWORD(lParam);
        }
        return 0;
    case WM_DROPFILES:
        if (app) app->CheckDroppedFile(reinterpret_cast<HDROP>(wParam));
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool DbViewerApp::CreateAppWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm       = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                           GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                           LR_DEFAULTCOLOR);
    if (!wc.hIcon)   wc.hIcon   = LoadIconW(nullptr, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        kWndClass, L"SQLite Editor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        nullptr, nullptr, hInstance, nullptr);

    if (!m_hwnd) return false;
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    if (HICON h = LoadIconW(hInstance, MAKEINTRESOURCEW(1)))
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(h));
    if (HICON h = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                       GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                       LR_DEFAULTCOLOR))
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(h));

    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);
    return true;
}

// ---------------------------------------------------------------------------
// D3D11
// ---------------------------------------------------------------------------
bool DbViewerApp::CreateD3D() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount        = 2;
    sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow       = m_hwnd;
    sd.SampleDesc.Count   = 1;
    sd.Windowed           = TRUE;
    sd.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL flOut;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1,
        D3D11_SDK_VERSION, &sd, &m_swapChain, &m_device, &flOut, &m_context);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

void DbViewerApp::DestroyD3D() {
    DestroyRenderTarget();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_context)   { m_context->Release();   m_context   = nullptr; }
    if (m_device)    { m_device->Release();     m_device    = nullptr; }
}

void DbViewerApp::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_device->CreateRenderTargetView(back, nullptr, &m_renderTarget);
        back->Release();
    }
}

void DbViewerApp::DestroyRenderTarget() {
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }
}

void DbViewerApp::ResizeSwapChain(UINT w, UINT h) {
    DestroyRenderTarget();
    m_swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
void DbViewerApp::ApplyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding     = 6.0f;
    s.ChildRounding      = 4.0f;
    s.FrameRounding      = 3.0f;
    s.PopupRounding      = 5.0f;
    s.ScrollbarRounding  = 7.0f;
    s.GrabRounding       = 3.0f;
    s.TabRounding        = 4.0f;
    s.FramePadding       = {6.0f, 3.0f};
    s.ItemSpacing        = {8.0f, 4.0f};
    s.ScrollbarSize      = 10.0f;
    s.WindowBorderSize   = 0.0f;
    s.FrameBorderSize    = 0.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = {0.118f, 0.118f, 0.118f, 1.0f};
    c[ImGuiCol_ChildBg]              = {0.118f, 0.118f, 0.118f, 1.0f};
    c[ImGuiCol_PopupBg]              = {0.145f, 0.145f, 0.149f, 1.0f};
    c[ImGuiCol_Border]               = {0.220f, 0.220f, 0.240f, 0.6f};
    c[ImGuiCol_Text]                 = {0.863f, 0.863f, 0.863f, 1.0f};
    c[ImGuiCol_TextDisabled]         = {0.520f, 0.520f, 0.520f, 1.0f};
    c[ImGuiCol_Header]               = {0.149f, 0.475f, 1.0f, 0.35f};
    c[ImGuiCol_HeaderHovered]        = {0.149f, 0.475f, 1.0f, 0.55f};
    c[ImGuiCol_HeaderActive]         = {0.149f, 0.475f, 1.0f, 0.75f};
    c[ImGuiCol_Button]               = {0.184f, 0.196f, 0.220f, 1.0f};
    c[ImGuiCol_ButtonHovered]        = {0.294f, 0.561f, 1.0f, 0.70f};
    c[ImGuiCol_ButtonActive]         = {0.149f, 0.475f, 1.0f, 1.0f};
    c[ImGuiCol_FrameBg]              = {0.145f, 0.145f, 0.149f, 1.0f};
    c[ImGuiCol_FrameBgHovered]       = {0.184f, 0.196f, 0.220f, 1.0f};
    c[ImGuiCol_FrameBgActive]        = {0.149f, 0.475f, 1.0f, 0.40f};
    c[ImGuiCol_TitleBg]              = {0.075f, 0.075f, 0.075f, 1.0f};
    c[ImGuiCol_TitleBgActive]        = {0.075f, 0.075f, 0.075f, 1.0f};
    c[ImGuiCol_MenuBarBg]            = {0.100f, 0.100f, 0.100f, 1.0f};
    c[ImGuiCol_Tab]                  = {0.145f, 0.145f, 0.149f, 1.0f};
    c[ImGuiCol_TabHovered]           = {0.294f, 0.561f, 1.0f, 0.70f};
    c[ImGuiCol_TabActive]            = {0.149f, 0.475f, 1.0f, 1.0f};
    c[ImGuiCol_TabUnfocused]         = {0.145f, 0.145f, 0.149f, 1.0f};
    c[ImGuiCol_TabUnfocusedActive]   = {0.184f, 0.196f, 0.220f, 1.0f};
    c[ImGuiCol_ScrollbarBg]          = {0.145f, 0.145f, 0.149f, 0.45f};
    c[ImGuiCol_ScrollbarGrab]        = {0.310f, 0.360f, 0.460f, 1.0f};
    c[ImGuiCol_ScrollbarGrabHovered] = {0.294f, 0.561f, 1.0f, 1.0f};
    c[ImGuiCol_ScrollbarGrabActive]  = {0.149f, 0.475f, 1.0f, 1.0f};
    c[ImGuiCol_CheckMark]            = {0.149f, 0.475f, 1.0f, 1.0f};
    c[ImGuiCol_SliderGrab]           = {0.149f, 0.475f, 1.0f, 0.8f};
    c[ImGuiCol_SliderGrabActive]     = {0.149f, 0.475f, 1.0f, 1.0f};
    c[ImGuiCol_Separator]            = {0.220f, 0.220f, 0.240f, 0.6f};
    c[ImGuiCol_SeparatorHovered]     = {0.294f, 0.561f, 1.0f, 0.7f};
    c[ImGuiCol_SeparatorActive]      = {0.149f, 0.475f, 1.0f, 1.0f};
    c[ImGuiCol_ResizeGrip]           = {0.149f, 0.475f, 1.0f, 0.20f};
    c[ImGuiCol_ResizeGripHovered]    = {0.149f, 0.475f, 1.0f, 0.67f};
    c[ImGuiCol_ResizeGripActive]     = {0.149f, 0.475f, 1.0f, 0.95f};
    c[ImGuiCol_TableHeaderBg]        = {0.090f, 0.090f, 0.095f, 1.0f};
    c[ImGuiCol_TableBorderLight]     = {0.190f, 0.190f, 0.200f, 1.0f};
    c[ImGuiCol_TableBorderStrong]    = {0.250f, 0.250f, 0.270f, 1.0f};
    c[ImGuiCol_TableRowBg]           = {0.000f, 0.000f, 0.000f, 0.0f};
    c[ImGuiCol_TableRowBgAlt]        = {1.000f, 1.000f, 1.000f, 0.025f};
    c[ImGuiCol_InputTextCursor]      = {0.863f, 0.863f, 0.863f, 1.0f};
}

// ---------------------------------------------------------------------------
// Init + Run
// ---------------------------------------------------------------------------
bool DbViewerApp::Init(HINSTANCE hInstance, const wchar_t* initialFile) {
    m_hInstance = hInstance;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!CreateAppWindow(hInstance)) return false;
    if (!CreateD3D()) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename  = nullptr;

    ImFontConfig fc{};
    fc.OversampleH = 2;
    fc.OversampleV = 2;
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\segoeui.ttf") != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f, &fc);
    else
        io.Fonts->AddFontDefault();

    ApplyTheme();
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    LoadRecents();

    if (initialFile && initialFile[0])
        OpenDb(initialFile);

    return true;
}

int DbViewerApp::Run() {
    MSG msg{};
    while (true) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) goto done;
        }

        if (m_swapChainOccluded) {
            HRESULT hr = m_swapChain->Present(0, DXGI_PRESENT_TEST);
            if (hr == DXGI_STATUS_OCCLUDED) { Sleep(8); continue; }
            m_swapChainOccluded = false;
        }

        if (m_resizeW && m_resizeH) {
            ResizeSwapChain(m_resizeW, m_resizeH);
            m_resizeW = m_resizeH = 0;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        Render();

        ImGui::Render();
        const float clearColor[4] = {0.118f, 0.118f, 0.118f, 1.0f};
        m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
        m_context->ClearRenderTargetView(m_renderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = m_swapChain->Present(1, 0);
        m_swapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }
done:
    Shutdown();
    return static_cast<int>(msg.wParam);
}

void DbViewerApp::Shutdown() {
    ReleasePreview();
    CloseDb();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyD3D();
    DestroyWindow(m_hwnd);
    UnregisterClassW(kWndClass, m_hInstance);
    CoUninitialize();
}

// ---------------------------------------------------------------------------
// Recents
// ---------------------------------------------------------------------------
void DbViewerApp::LoadRecents() {
    m_recents.clear();
    const std::wstring path = GetRecentsPath();
    if (path.empty()) return;

    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return;

    std::string content;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        content.append(buf, n);
    fclose(f);

    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line) && (int)m_recents.size() < kMaxRecents) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (!line.empty())
            m_recents.push_back(Utf8ToWide(line));
    }
}

void DbViewerApp::SaveRecents() {
    const std::wstring path = GetRecentsPath();
    if (path.empty()) return;

    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"wb");
    if (!f) return;

    for (const auto& r : m_recents) {
        std::string utf8 = WideToUtf8(r) + '\n';
        fwrite(utf8.data(), 1, utf8.size(), f);
    }
    fclose(f);
}

void DbViewerApp::AddToRecents(const std::wstring& path) {
    auto it = std::find(m_recents.begin(), m_recents.end(), path);
    if (it != m_recents.end()) m_recents.erase(it);

    m_recents.insert(m_recents.begin(), path);
    if ((int)m_recents.size() > kMaxRecents)
        m_recents.resize(kMaxRecents);

    SaveRecents();
}

// ---------------------------------------------------------------------------
// DB helpers
// ---------------------------------------------------------------------------
bool DbViewerApp::OpenDb(const std::wstring& path) {
    CloseDb();
    const std::string utf8 = WideToUtf8(path);
    if (sqlite3_open_v2(utf8.c_str(), &m_db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) {
        if (sqlite3_open_v2(utf8.c_str(), &m_db,
                SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) {
            m_statusMsg = "Failed to open: " + utf8;
            m_db = nullptr;
            return false;
        }
        m_statusMsg = "Opened read-only: " + utf8;
    } else {
        m_statusMsg = "Opened: " + utf8;
    }
    m_dbPath = path;
    AddToRecents(path);
    RefreshTableList();

    std::wstring title = L"SQLite Editor \x2014 " + path;
    SetWindowTextW(m_hwnd, title.c_str());
    return true;
}

void DbViewerApp::CloseDb() {
    m_selectedTable.clear();
    m_tables.clear();
    m_cols.clear();
    m_rows.clear();
    m_blobCols.clear();
    m_totalRows      = 0;
    m_lastPreviewRow = -2;
    m_edit.reset();
    ReleasePreview();
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
    m_dbPath.clear();
    SetWindowTextW(m_hwnd, L"SQLite Editor");
}

void DbViewerApp::RefreshTableList() {
    m_tables.clear();
    if (!m_db) return;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db,
            "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;",
            -1, &stmt, nullptr) != SQLITE_OK) return;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (!n) continue;
        TableInfo ti;
        ti.name = n;
        std::string countSql = "SELECT COUNT(*) FROM \"" + ti.name + "\";";
        sqlite3_stmt* cs = nullptr;
        if (sqlite3_prepare_v2(m_db, countSql.c_str(), -1, &cs, nullptr) == SQLITE_OK) {
            if (sqlite3_step(cs) == SQLITE_ROW)
                ti.rowCount = sqlite3_column_int64(cs, 0);
            sqlite3_finalize(cs);
        }
        m_tables.push_back(std::move(ti));
    }
    sqlite3_finalize(stmt);
}

void DbViewerApp::SelectTable(const std::string& name) {
    m_selectedTable  = name;
    m_cols.clear();
    m_rows.clear();
    m_blobCols.clear();
    m_totalRows      = 0;
    m_dataOffset     = 0;
    m_sortCol        = -1;
    m_sortAsc        = true;
    m_selectedRow    = -1;
    m_lastPreviewRow = -2;
    m_edit.reset();
    ReleasePreview();

    if (!m_db || name.empty()) return;

    std::string pragmaSql = "PRAGMA table_info(\"" + name + "\");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, pragmaSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ColumnInfo ci;
            auto col = [&](int i) -> const char* {
                const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                return p ? p : "";
            };
            ci.name    = col(1);
            ci.type    = col(2);
            ci.notNull = sqlite3_column_int(stmt, 3) != 0;
            ci.isPk    = sqlite3_column_int(stmt, 5) != 0;

            // Detect BLOB columns by declared type
            std::string ltype = ci.type;
            std::transform(ltype.begin(), ltype.end(), ltype.begin(), ::tolower);
            if (ltype.find("blob") != std::string::npos)
                m_blobCols.push_back(static_cast<int>(m_cols.size()));

            m_cols.push_back(std::move(ci));
        }
        sqlite3_finalize(stmt);
    }

    std::string countSql = "SELECT COUNT(*) FROM \"" + name + "\";";
    stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, countSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            m_totalRows = static_cast<int>(sqlite3_column_int64(stmt, 0));
        sqlite3_finalize(stmt);
    }

    LoadTableData(0, kPageSize);
}

void DbViewerApp::LoadTableData(int offset, int limit) {
    if (!m_db || m_selectedTable.empty()) return;

    m_rows.clear();
    m_dataOffset = offset;

    std::string orderBy;
    if (m_sortCol >= 0 && m_sortCol < (int)m_cols.size())
        orderBy = " ORDER BY \"" + m_cols[m_sortCol].name + "\" " + (m_sortAsc ? "ASC" : "DESC");

    std::string sql = "SELECT * FROM \"" + m_selectedTable + "\"" + orderBy
                    + " LIMIT " + std::to_string(limit)
                    + " OFFSET " + std::to_string(offset) + ";";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::vector<std::string> row;
        row.reserve(m_cols.size());
        const int nc = sqlite3_column_count(stmt);
        for (int i = 0; i < nc; ++i) {
            const int ct = sqlite3_column_type(stmt, i);
            if (ct == SQLITE_BLOB) {
                char placeholder[64];
                snprintf(placeholder, sizeof(placeholder),
                         "[BLOB: %d bytes]", sqlite3_column_bytes(stmt, i));
                row.push_back(placeholder);
            } else if (ct == SQLITE_NULL) {
                row.push_back("NULL");
            } else {
                const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                row.push_back(v ? v : "");
            }
        }
        m_rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
}

void DbViewerApp::ExecuteQuery(const char* sql) {
    m_queryResult = {};
    if (!m_db || !sql || !sql[0]) return;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        m_queryResult.error = sqlite3_errmsg(m_db);
        return;
    }

    const int nc = sqlite3_column_count(stmt);
    for (int i = 0; i < nc; ++i)
        m_queryResult.cols.push_back(
            sqlite3_column_name(stmt, i) ? sqlite3_column_name(stmt, i) : "");

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        std::vector<std::string> row;
        for (int i = 0; i < nc; ++i) {
            if (sqlite3_column_type(stmt, i) == SQLITE_BLOB) {
                char ph[64];
                snprintf(ph, sizeof(ph), "[BLOB: %d bytes]", sqlite3_column_bytes(stmt, i));
                row.push_back(ph);
            } else if (sqlite3_column_type(stmt, i) == SQLITE_NULL) {
                row.push_back("NULL");
            } else {
                const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                row.push_back(v ? v : "");
            }
        }
        m_queryResult.rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
        m_queryResult.error = sqlite3_errmsg(m_db);
    else {
        m_queryResult.affectedRows = sqlite3_changes(m_db);
        m_needsRefresh = true;
    }
}

std::string DbViewerApp::EscapeForSql(const std::string& v, bool numeric) const {
    if (numeric) return v;
    std::string out = "'";
    for (char ch : v) {
        if (ch == '\'') out += "''";
        else out += ch;
    }
    out += "'";
    return out;
}

std::string DbViewerApp::PkWhereClause(int rowIdx) const {
    if (rowIdx < 0 || rowIdx >= (int)m_rows.size()) return {};
    const auto& row = m_rows[rowIdx];
    std::string clause;
    for (int i = 0; i < (int)m_cols.size(); ++i) {
        if (!m_cols[i].isPk) continue;
        if (!clause.empty()) clause += " AND ";
        const bool num = (m_cols[i].type == "INTEGER" || m_cols[i].type == "REAL");
        clause += "\"" + m_cols[i].name + "\"=" + EscapeForSql(row[i], num);
    }
    if (clause.empty()) {
        // Fallback: all non-BLOB columns
        for (int i = 0; i < (int)m_cols.size() && i < (int)row.size(); ++i) {
            std::string ltype = m_cols[i].type;
            std::transform(ltype.begin(), ltype.end(), ltype.begin(), ::tolower);
            if (ltype.find("blob") != std::string::npos) continue;
            if (!clause.empty()) clause += " AND ";
            clause += "\"" + m_cols[i].name + "\"=" + EscapeForSql(row[i], false);
        }
    }
    return clause;
}

void DbViewerApp::DeleteSelectedRow() {
    if (m_selectedRow < 0 || m_selectedTable.empty()) return;
    const std::string where = PkWhereClause(m_selectedRow);
    if (where.empty()) return;
    const std::string sql = "DELETE FROM \"" + m_selectedTable + "\" WHERE " + where + ";";
    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg) == SQLITE_OK) {
        m_statusMsg = "Row deleted.";
        SelectTable(m_selectedTable);
    } else {
        m_statusMsg = errMsg ? errMsg : "Delete failed.";
        sqlite3_free(errMsg);
    }
}

void DbViewerApp::CommitEdit() {
    if (!m_edit || m_selectedTable.empty()) return;
    const int row = m_edit->row, col = m_edit->col;
    if (row < 0 || col < 0 || row >= (int)m_rows.size() || col >= (int)m_cols.size()) return;

    const std::string where = PkWhereClause(row);
    if (where.empty()) { m_edit.reset(); return; }

    const std::string newVal(m_edit->buf);
    const bool num = (m_cols[col].type == "INTEGER" || m_cols[col].type == "REAL");
    const std::string sql = "UPDATE \"" + m_selectedTable + "\" SET \""
        + m_cols[col].name + "\"=" + EscapeForSql(newVal, num)
        + " WHERE " + where + ";";

    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg) == SQLITE_OK) {
        m_rows[row][col] = newVal;
        m_statusMsg = "Cell updated.";
    } else {
        m_statusMsg = errMsg ? errMsg : "Update failed.";
        sqlite3_free(errMsg);
    }
    m_edit.reset();
}

// ---------------------------------------------------------------------------
// Blob / image preview
// ---------------------------------------------------------------------------
void DbViewerApp::ReleasePreview() {
    if (m_previewSrv) { m_previewSrv->Release(); m_previewSrv = nullptr; }
    m_previewW       = 0;
    m_previewH       = 0;
    m_previewBlobSize = 0;
    m_previewMsg.clear();
}

void DbViewerApp::LoadBlobPreview(int rowIdx) {
    ReleasePreview();
    if (!m_db || m_blobCols.empty() || rowIdx < 0 || rowIdx >= (int)m_rows.size()) return;

    const int blobCol = m_blobCols[0];
    if (blobCol >= (int)m_cols.size()) return;

    const std::string where = PkWhereClause(rowIdx);
    std::string sql;
    if (!where.empty())
        sql = "SELECT \"" + m_cols[blobCol].name + "\" FROM \"" + m_selectedTable
            + "\" WHERE " + where + " LIMIT 1;";
    else
        sql = "SELECT \"" + m_cols[blobCol].name + "\" FROM \"" + m_selectedTable
            + "\" LIMIT 1 OFFSET " + std::to_string(rowIdx) + ";";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void*  blob   = sqlite3_column_blob(stmt, 0);
        const int    nbytes = sqlite3_column_bytes(stmt, 0);
        m_previewBlobSize   = static_cast<size_t>(nbytes);

        if (blob && nbytes > 0) {
            const auto* ptr = static_cast<const uint8_t*>(blob);
            std::vector<uint8_t> bytes(ptr, ptr + nbytes);
            m_previewSrv = DecodeBytesToSrv(bytes);
            if (!m_previewSrv)
                m_previewMsg = "Not a decodable image";
        } else {
            m_previewMsg = "NULL";
        }
    } else {
        m_previewMsg = "No data";
    }
    sqlite3_finalize(stmt);
}

ID3D11ShaderResourceView* DbViewerApp::DecodeBytesToSrv(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return nullptr;

    IWICImagingFactory* fac = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&fac)))) return nullptr;

    auto tryDecode = [&](const uint8_t* data, DWORD size) -> IWICBitmapDecoder* {
        IWICStream* st = nullptr;
        fac->CreateStream(&st);
        st->InitializeFromMemory(const_cast<BYTE*>(data), size);
        IWICBitmapDecoder* dec = nullptr;
        HRESULT hr = fac->CreateDecoderFromStream(st, nullptr,
            WICDecodeMetadataCacheOnDemand, &dec);
        st->Release();
        return SUCCEEDED(hr) ? dec : nullptr;
    };

    IWICBitmapDecoder* decoder = tryDecode(bytes.data(), static_cast<DWORD>(bytes.size()));

    // If direct decode failed, try wrapping raw DIB in a BITMAPFILEHEADER
    std::vector<uint8_t> dibWrapped;
    if (!decoder && bytes.size() >= 40) {
        const DWORD biSize = *reinterpret_cast<const DWORD*>(bytes.data());
        if (biSize == 40 || biSize == 108 || biSize == 124 || biSize == 12) {
            const auto* bih = reinterpret_cast<const BITMAPINFOHEADER*>(bytes.data());
            DWORD palSize = 0;
            if (bih->biClrUsed > 0)       palSize = bih->biClrUsed * 4;
            else if (bih->biBitCount <= 8) palSize = (1u << bih->biBitCount) * 4;

            BITMAPFILEHEADER bfh{};
            bfh.bfType    = 0x4D42;
            bfh.bfSize    = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + bytes.size());
            bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + bih->biSize + palSize;

            dibWrapped.resize(sizeof(BITMAPFILEHEADER) + bytes.size());
            memcpy(dibWrapped.data(), &bfh, sizeof(bfh));
            memcpy(dibWrapped.data() + sizeof(bfh), bytes.data(), bytes.size());

            decoder = tryDecode(dibWrapped.data(), static_cast<DWORD>(dibWrapped.size()));
        }
    }

    if (!decoder) { fac->Release(); return nullptr; }

    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        decoder->Release(); fac->Release(); return nullptr;
    }

    IWICFormatConverter* converter = nullptr;
    fac->CreateFormatConverter(&converter);
    converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    m_previewW = static_cast<int>(w);
    m_previewH = static_cast<int>(h);

    std::vector<uint8_t> pixels(w * h * 4);
    converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data());

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width            = w;
    desc.Height           = h;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem     = pixels.data();
    initData.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    m_device->CreateTexture2D(&desc, &initData, &tex);

    ID3D11ShaderResourceView* srv = nullptr;
    if (tex) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                    = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels       = 1;
        m_device->CreateShaderResourceView(tex, &srvDesc, &srv);
        tex->Release();
    }

    converter->Release();
    frame->Release();
    decoder->Release();
    fac->Release();
    return srv;
}

// ---------------------------------------------------------------------------
// File dialog / drag-drop
// ---------------------------------------------------------------------------
std::wstring DbViewerApp::OpenFileDialog() {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hwnd;
    ofn.lpstrFilter = L"SQLite databases\0*.db;*.sqlite;*.sqlite3;*.s3db\0All files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrTitle  = L"Open SQLite Database";
    return GetOpenFileNameW(&ofn) ? std::wstring(path) : std::wstring{};
}

void DbViewerApp::CheckDroppedFile(HDROP hDrop) {
    wchar_t path[MAX_PATH]{};
    if (DragQueryFileW(hDrop, 0, path, MAX_PATH))
        OpenDb(path);
    DragFinish(hDrop);
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------
void DbViewerApp::Render() {
    const ImGuiIO& io = ImGui::GetIO();
    const float sw = io.DisplaySize.x;
    const float sh = io.DisplaySize.y;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({sw, sh});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("##host", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    DrawMenuBar();

    if (ImGui::IsKeyPressed(ImGuiKey_F5) && m_db)
        SelectTable(m_selectedTable);
    if (m_needsRefresh) {
        RefreshTableList();
        if (!m_selectedTable.empty()) SelectTable(m_selectedTable);
        m_needsRefresh = false;
    }

    // Detect row selection change → trigger blob preview
    if (m_selectedRow != m_lastPreviewRow) {
        m_lastPreviewRow = m_selectedRow;
        if (!m_blobCols.empty())
            LoadBlobPreview(m_selectedRow);
    }

    const float kStatusH = 22.0f;
    const float availH   = sh - ImGui::GetCursorPosY() - kStatusH;

    DrawLeftPanel(m_leftPanelW);
    ImGui::SameLine(0, 0);

    // Vertical splitter (left panel / main area)
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.15f, 0.15f, 0.16f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.149f, 0.475f, 1.0f, 0.5f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.149f, 0.475f, 1.0f, 0.8f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    ImGui::Button("##vsplitter", {4.0f, availH});
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemActive()) {
        m_leftPanelW += io.MouseDelta.x;
        m_leftPanelW = std::clamp(m_leftPanelW, 120.0f, sw * 0.4f);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ImGui::SameLine(0, 0);

    DrawMainArea(m_leftPanelW + 4.0f, sw - m_leftPanelW - 4.0f, availH);

    ImGui::SetCursorPosY(sh - kStatusH);
    DrawStatusBar();

    DrawOpenConfirmModal();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------
void DbViewerApp::DrawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            auto p = OpenFileDialog();
            if (!p.empty()) OpenDb(p);
        }

        const bool hasRecents = !m_recents.empty();
        if (ImGui::BeginMenu("Recent Files", hasRecents)) {
            for (int i = 0; i < (int)m_recents.size(); ++i) {
                const auto& r = m_recents[i];
                // Show filename only as label, full path as tooltip
                std::string display = WideToUtf8(r);
                const auto slash = display.find_last_of("\\/");
                if (slash != std::string::npos) display = display.substr(slash + 1);

                char itemId[64];
                snprintf(itemId, sizeof(itemId), "%s##recent%d", display.c_str(), i);
                if (ImGui::MenuItem(itemId)) {
                    if (GetFileAttributesW(r.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        OpenDb(r);
                    } else {
                        m_statusMsg = "File not found: " + display;
                        m_recents.erase(m_recents.begin() + i);
                        SaveRecents();
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", WideToUtf8(r).c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Recent Files")) {
                m_recents.clear();
                SaveRecents();
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Close", nullptr, false, m_db != nullptr))
            CloseDb();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            PostQuitMessage(0);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Refresh", "F5", false, m_db != nullptr))
            SelectTable(m_selectedTable);
        ImGui::MenuItem("SQL Editor", "Ctrl+`", &m_showSqlPanel);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Row")) {
        if (ImGui::MenuItem("Delete selected row", "Del",
                false, m_selectedRow >= 0 && m_db != nullptr))
            DeleteSelectedRow();
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();

    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
        auto p = OpenFileDialog();
        if (!p.empty()) OpenDb(p);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_GraveAccent))
        m_showSqlPanel = !m_showSqlPanel;
}

// ---------------------------------------------------------------------------
// Left panel — table list
// ---------------------------------------------------------------------------
void DbViewerApp::DrawLeftPanel(float panelW) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.090f, 0.090f, 0.095f, 1.0f});
    ImGui::BeginChild("##left", {panelW, 0.f}, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::Text("  TABLES");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 2});
    ImGui::SetNextItemWidth(panelW - 12.0f);
    ImGui::InputTextWithHint("##tfilter", "Filter...", m_tableFilter, sizeof(m_tableFilter));
    ImGui::PopStyleVar();
    ImGui::Spacing();

    if (!m_db) {
        ImGui::SetCursorPosX(8.0f);
        ImGui::TextDisabled("No database open.");
        ImGui::Spacing();
        ImGui::SetCursorPosX(8.0f);
        if (ImGui::SmallButton("Open file...")) {
            auto p = OpenFileDialog();
            if (!p.empty()) OpenDb(p);
        }
    } else {
        ImGui::BeginChild("##tablelist", {0.f, 0.f});
        const std::string filter(m_tableFilter);
        for (const auto& ti : m_tables) {
            if (!filter.empty()) {
                std::string ln = ti.name, lf = filter;
                auto lc = [](unsigned char ch) { return (char)std::tolower(ch); };
                std::transform(ln.begin(), ln.end(), ln.begin(), lc);
                std::transform(lf.begin(), lf.end(), lf.begin(), lc);
                if (ln.find(lf) == std::string::npos) continue;
            }

            const bool sel = (ti.name == m_selectedTable);
            ImGui::PushID(ti.name.c_str());

            if (sel) ImGui::PushStyleColor(ImGuiCol_Header,
                         ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            const bool clicked = ImGui::Selectable(
                ("  " + ti.name).c_str(), sel,
                ImGuiSelectableFlags_AllowDoubleClick);
            if (sel) ImGui::PopStyleColor();

            if (clicked) SelectTable(ti.name);

            if (ti.rowCount >= 0) {
                char badge[24];
                snprintf(badge, sizeof(badge), "%lld", (long long)ti.rowCount);
                const float bw = ImGui::CalcTextSize(badge).x + 6.0f;
                ImGui::SameLine(panelW - bw - 8.0f);
                ImGui::TextDisabled("%s", badge);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Main area
// ---------------------------------------------------------------------------
void DbViewerApp::DrawMainArea(float /*x*/, float availW, float availH) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::BeginChild("##main", {availW, availH}, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);

    if (!m_db) {
        const float cx = availW * 0.5f, cy = availH * 0.4f;
        ImGui::SetCursorPos({cx - 148.0f, cy - 20.0f});
        ImGui::TextDisabled("Drop a .db file here or use File > Open");
        ImGui::SetCursorPos({cx - 60.0f, cy + 12.0f});
        if (ImGui::Button("Open file...", {120.0f, 28.0f})) {
            auto p = OpenFileDialog();
            if (!p.empty()) OpenDb(p);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    if (m_selectedTable.empty()) {
        ImGui::SetCursorPos({20.0f, 20.0f});
        ImGui::TextDisabled("Select a table from the left panel.");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    // Header strip
    ImGui::Spacing();
    ImGui::SetCursorPosX(8.0f);
    ImGui::Text("%s", m_selectedTable.c_str());
    ImGui::SameLine(0, 10.0f);
    ImGui::TextDisabled("(%d rows, %zu cols)", m_totalRows, m_cols.size());
    if (!m_blobCols.empty()) {
        ImGui::SameLine(0, 10.0f);
        ImGui::TextDisabled("[has BLOB]");
    }
    ImGui::SameLine(0, 10.0f);
    if (m_sortCol >= 0) {
        ImGui::TextDisabled("sorted by %s %s",
            m_cols[m_sortCol].name.c_str(), m_sortAsc ? "\xe2\x86\x91" : "\xe2\x86\x93");
        ImGui::SameLine(0, 6.0f);
        if (ImGui::SmallButton("clear sort")) {
            m_sortCol = -1; m_sortAsc = true;
            LoadTableData(m_dataOffset, kPageSize);
        }
    }

    if (m_selectedRow >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_edit)
        DeleteSelectedRow();
    if (m_edit && ImGui::IsKeyPressed(ImGuiKey_Escape))
        m_edit.reset();

    const float sqlH    = m_showSqlPanel ? (m_sqlPanelH + 6.0f) : 0.0f;
    const float contentH = availH - ImGui::GetCursorPosY() - sqlH - 6.0f;
    const bool  hasBlob  = !m_blobCols.empty();

    if (hasBlob) {
        // Side-by-side: [grid] | [splitter] | [preview]
        const float gridW = std::max(100.0f, availW - m_previewPanelW - 4.0f);

        ImGui::BeginChild("##gridwrap", {gridW, contentH}, ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar);
        DrawTableGrid(contentH);
        ImGui::EndChild();

        ImGui::SameLine(0, 0);

        // Vertical splitter between grid and preview
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.10f, 0.10f, 0.11f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.149f, 0.475f, 1.0f, 0.4f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.149f, 0.475f, 1.0f, 0.7f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
        ImGui::Button("##prevsplit", {4.0f, contentH});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemActive()) {
            m_previewPanelW -= ImGui::GetIO().MouseDelta.x;
            m_previewPanelW = std::clamp(m_previewPanelW, 100.0f, availW * 0.6f);
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        ImGui::SameLine(0, 0);
        DrawImagePreview(m_previewPanelW, contentH);
    } else {
        DrawTableGrid(contentH);
    }

    if (m_showSqlPanel) {
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.10f, 0.10f, 0.11f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.149f, 0.475f, 1.0f, 0.4f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.149f, 0.475f, 1.0f, 0.7f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
        ImGui::Button("##hsplitter", {availW, 4.0f});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemActive()) {
            m_sqlPanelH -= ImGui::GetIO().MouseDelta.y;
            m_sqlPanelH = std::clamp(m_sqlPanelH, 60.0f, availH * 0.6f);
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

        DrawSqlPanel(m_sqlPanelH);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Image preview panel
// ---------------------------------------------------------------------------
void DbViewerApp::DrawImagePreview(float panelW, float availH) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.090f, 0.090f, 0.095f, 1.0f});
    // Explicit padding so all content has consistent 8px margins without
    // manual SetCursorPosX calls, and contentW is unambiguous.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0f, 8.0f});
    ImGui::BeginChild("##preview", {panelW, availH});
    ImGui::PopStyleVar();

    // Content width — always use this, never panelW, for sizing and centering.
    // panelW is the outer window size; the usable area is smaller by 2×padding.
    const float cw = ImGui::GetContentRegionAvail().x;

    // Header: show the actual BLOB column name
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (!m_blobCols.empty())
        ImGui::Text("BLOB  /  %s", m_cols[m_blobCols[0]].name.c_str());
    else
        ImGui::TextUnformatted("BLOB PREVIEW");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    if (m_selectedRow < 0) {
        ImGui::TextDisabled("Select a row to preview.");
    } else if (m_previewSrv) {
        // Reserve footer: dimensions line + size line + spacing + save button
        constexpr float kFooterH = 78.0f;
        const float headerY = ImGui::GetCursorPosY();
        const float remainH = availH - headerY - kFooterH;
        const float maxH    = std::max(10.0f, remainH);

        float imgW = static_cast<float>(m_previewW);
        float imgH = static_cast<float>(m_previewH);
        const float scale = std::min({1.0f, cw / imgW, maxH / imgH});
        imgW *= scale;
        imgH *= scale;

        // Center within the content area — cw is the correct boundary
        const float cx = std::max(0.0f, (cw - imgW) * 0.5f);
        const float cy = headerY + std::max(0.0f, (remainH - imgH) * 0.5f);
        ImGui::SetCursorPos({cx, cy});
        ImGui::Image(reinterpret_cast<ImTextureID>(m_previewSrv), {imgW, imgH});

        // Advance past image area; Dummy commits the content boundary
        ImGui::SetCursorPosY(headerY + remainH);
        ImGui::Dummy({cw, 0.0f});

        // Metadata footer (WindowPadding gives the 8px left margin automatically)
        ImGui::Text("%d \xc3\x97 %d px", m_previewW, m_previewH);
        if (m_previewBlobSize >= 1024 * 1024)
            ImGui::TextDisabled("%.2f MB", m_previewBlobSize / (1024.0 * 1024.0));
        else if (m_previewBlobSize >= 1024)
            ImGui::TextDisabled("%.1f KB", m_previewBlobSize / 1024.0);
        else
            ImGui::TextDisabled("%zu bytes", m_previewBlobSize);

        ImGui::Spacing();
        if (ImGui::Button("Save image...", {cw, 0.0f})) {
            wchar_t savePath[MAX_PATH]{};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = m_hwnd;
            ofn.lpstrFilter = L"PNG image\0*.png\0BMP image\0*.bmp\0All files\0*.*\0";
            ofn.lpstrFile   = savePath;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_OVERWRITEPROMPT;
            ofn.lpstrTitle  = L"Save Blob as Image";
            ofn.lpstrDefExt = L"png";
            if (GetSaveFileNameW(&ofn)) {
                // Re-fetch blob and write to file
                if (!m_blobCols.empty()) {
                    const int bc = m_blobCols[0];
                    const std::string where = PkWhereClause(m_selectedRow);
                    std::string sql = "SELECT \"" + m_cols[bc].name + "\" FROM \""
                        + m_selectedTable + "\" WHERE " + where + " LIMIT 1;";
                    sqlite3_stmt* st = nullptr;
                    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &st, nullptr) == SQLITE_OK
                        && sqlite3_step(st) == SQLITE_ROW) {
                        const void* blob  = sqlite3_column_blob(st, 0);
                        const int   nbytes = sqlite3_column_bytes(st, 0);
                        if (blob && nbytes > 0) {
                            FILE* fp = nullptr;
                            _wfopen_s(&fp, savePath, L"wb");
                            if (fp) {
                                fwrite(blob, 1, nbytes, fp);
                                fclose(fp);
                                m_statusMsg = "Saved.";
                            }
                        }
                    }
                    if (st) sqlite3_finalize(st);
                }
            }
        }
    } else {
        // Row selected but BLOB is not a decodable image
        ImGui::TextDisabled("%s", m_previewMsg.empty()
            ? "Not a decodable image." : m_previewMsg.c_str());
        if (m_previewBlobSize > 0) {
            ImGui::Spacing();
            if (m_previewBlobSize >= 1024 * 1024)
                ImGui::TextDisabled("%.2f MB", m_previewBlobSize / (1024.0 * 1024.0));
            else if (m_previewBlobSize >= 1024)
                ImGui::TextDisabled("%.1f KB", m_previewBlobSize / 1024.0);
            else
                ImGui::TextDisabled("%zu bytes", m_previewBlobSize);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Table data grid
// ---------------------------------------------------------------------------
void DbViewerApp::DrawTableGrid(float availH) {
    if (m_cols.empty()) return;

    ImGuiTableFlags flags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_Hideable  | ImGuiTableFlags_Sortable    |
        ImGuiTableFlags_ScrollX   | ImGuiTableFlags_ScrollY     |
        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_RowBg     | ImGuiTableFlags_SizingFixedFit;

    const int ncols = static_cast<int>(m_cols.size()) + 1;
    if (!ImGui::BeginTable("##grid", ncols, flags, {0.f, availH})) return;

    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("#",
        ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 40.0f);
    for (int i = 0; i < (int)m_cols.size(); ++i) {
        std::string hdr = m_cols[i].name;
        if (!m_cols[i].type.empty()) hdr += "\n" + m_cols[i].type;
        ImGui::TableSetupColumn(hdr.c_str(),
            ImGuiTableColumnFlags_WidthFixed, 120.0f);
    }
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
        if (ss->SpecsDirty && ss->SpecsCount > 0) {
            const int idx = static_cast<int>(ss->Specs[0].ColumnIndex) - 1;
            const bool asc = (ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
            if (idx >= 0 && (idx != m_sortCol || asc != m_sortAsc)) {
                m_sortCol = idx;
                m_sortAsc = asc;
                LoadTableData(0, kPageSize);
                m_dataOffset = 0;
            }
            ss->SpecsDirty = false;
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_rows.size()));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const auto& row = m_rows[r];
            ImGui::TableNextRow();

            const bool sel = (r == m_selectedRow);

            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::Text("%d", m_dataOffset + r + 1);
            ImGui::PopStyleColor();

            for (int c = 0; c < (int)m_cols.size(); ++c) {
                ImGui::TableSetColumnIndex(c + 1);
                ImGui::PushID(r * 10000 + c);

                if (m_edit && m_edit->row == r && m_edit->col == c) {
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##edit", m_edit->buf, sizeof(m_edit->buf),
                            ImGuiInputTextFlags_EnterReturnsTrue))
                        CommitEdit();
                    if (ImGui::IsItemDeactivatedAfterEdit()) CommitEdit();
                } else {
                    const std::string& val = (c < (int)row.size()) ? row[c] : "";
                    const bool isBlob  = (!val.empty() && val[0] == '[' &&
                                          val.find("[BLOB:") == 0);
                    const bool isNull  = (val == "NULL");
                    const bool dimmed  = isBlob || isNull;

                    if (sel)   ImGui::PushStyleColor(ImGuiCol_Header,
                                   ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
                    if (dimmed) ImGui::PushStyleColor(ImGuiCol_Text,
                                   ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

                    if (ImGui::Selectable(val.c_str(), sel,
                            ImGuiSelectableFlags_SpanAllColumns |
                            ImGuiSelectableFlags_AllowDoubleClick,
                            {0.f, 0.f})) {
                        m_selectedRow = r;
                        if (!isBlob && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            m_edit = CellEdit{};
                            m_edit->row = r;
                            m_edit->col = c;
                            m_edit->original = val;
                            strncpy(m_edit->buf, isNull ? "" : val.c_str(),
                                    sizeof(m_edit->buf) - 1);
                        }
                    }

                    if (dimmed) ImGui::PopStyleColor();
                    if (sel)    ImGui::PopStyleColor();

                    if (ImGui::BeginPopupContextItem("##cellctx")) {
                        m_selectedRow = r;
                        if (!isBlob && ImGui::MenuItem("Copy cell value"))
                            ImGui::SetClipboardText(val.c_str());
                        if (!isBlob && ImGui::MenuItem("Edit cell")) {
                            m_edit = CellEdit{};
                            m_edit->row = r; m_edit->col = c;
                            m_edit->original = val;
                            strncpy(m_edit->buf, isNull ? "" : val.c_str(),
                                    sizeof(m_edit->buf) - 1);
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Delete row")) DeleteSelectedRow();
                        ImGui::EndPopup();
                    }
                }
                ImGui::PopID();
            }
        }
    }
    clipper.End();

    if (m_dataOffset + (int)m_rows.size() < m_totalRows) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::SmallButton("Load more..."))
            LoadTableData(0, m_dataOffset + static_cast<int>(m_rows.size()) + kPageSize);
    }

    ImGui::EndTable();
}

// ---------------------------------------------------------------------------
// SQL editor panel
// ---------------------------------------------------------------------------
void DbViewerApp::DrawSqlPanel(float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.090f, 0.090f, 0.095f, 1.0f});
    if (!ImGui::BeginChild("##sqlpanel", {0.f, height})) {
        ImGui::EndChild(); ImGui::PopStyleColor(); return;
    }

    ImGui::Spacing();
    ImGui::SetCursorPosX(8.0f);
    ImGui::TextDisabled("SQL   F9 to run");

    const float btnH  = 24.0f;
    const float editH = height - ImGui::GetCursorPosY() - btnH - 10.0f;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.10f, 0.10f, 0.11f, 1.0f});
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("##sql", m_sqlBuf, sizeof(m_sqlBuf),
        {-1.0f, editH > 20.0f ? editH : 60.0f},
        ImGuiInputTextFlags_AllowTabInput);
    ImGui::PopStyleColor();

    if (ImGui::IsKeyPressed(ImGuiKey_F9)) ExecuteQuery(m_sqlBuf);

    ImGui::SetCursorPosX(8.0f);
    if (ImGui::Button("Run  (F9)", {100.0f, btnH})) ExecuteQuery(m_sqlBuf);
    ImGui::SameLine(0, 8.0f);
    if (ImGui::Button("Clear", {60.0f, btnH})) m_sqlBuf[0] = '\0';

    if (!m_queryResult.error.empty()) {
        ImGui::SameLine(0, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, {0.9f, 0.3f, 0.3f, 1.0f});
        ImGui::TextWrapped("%s", m_queryResult.error.c_str());
        ImGui::PopStyleColor();
    } else if (!m_queryResult.cols.empty()) {
        ImGui::SameLine(0, 10.0f);
        ImGui::TextDisabled("%d rows", (int)m_queryResult.rows.size());
    } else if (m_queryResult.affectedRows > 0) {
        ImGui::SameLine(0, 10.0f);
        ImGui::TextDisabled("%d rows affected", m_queryResult.affectedRows);
    }

    if (!m_queryResult.cols.empty()) {
        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX
            | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter
            | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_SizingFixedFit;
        const int nc = static_cast<int>(m_queryResult.cols.size());
        if (ImGui::BeginTable("##qresult", nc, flags, {-1.0f, 0.0f})) {
            for (const auto& col : m_queryResult.cols)
                ImGui::TableSetupColumn(col.c_str(), 0, 120.0f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(m_queryResult.rows.size()));
            while (clip.Step()) {
                for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                    ImGui::TableNextRow();
                    for (int c = 0; c < nc; ++c) {
                        ImGui::TableSetColumnIndex(c);
                        const std::string& v = (c < (int)m_queryResult.rows[r].size())
                            ? m_queryResult.rows[r][c] : "";
                        if (v == "NULL") ImGui::TextDisabled("NULL");
                        else             ImGui::TextUnformatted(v.c_str());
                    }
                }
            }
            clip.End();
            ImGui::EndTable();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------
void DbViewerApp::DrawStatusBar() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.075f, 0.075f, 0.075f, 1.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0f, 4.0f});
    ImGui::BeginChild("##status", {0.f, 22.0f}, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    if (m_db) {
        std::string fname = WideToUtf8(m_dbPath);
        const auto slash = fname.find_last_of("\\/");
        if (slash != std::string::npos) fname = fname.substr(slash + 1);
        ImGui::TextDisabled("%s", fname.c_str());
        ImGui::SameLine(0, 10.0f);
        ImGui::TextDisabled("|");
        ImGui::SameLine(0, 10.0f);
        ImGui::TextDisabled("%d table%s", (int)m_tables.size(),
            m_tables.size() == 1 ? "" : "s");
        if (!m_selectedTable.empty()) {
            ImGui::SameLine(0, 10.0f);
            ImGui::TextDisabled("|");
            ImGui::SameLine(0, 10.0f);
            ImGui::TextDisabled("%d rows  %zu cols", m_totalRows, m_cols.size());
        }
    }

    if (!m_statusMsg.empty()) {
        ImGui::SameLine(0, 20.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, {0.4f, 0.8f, 0.4f, 1.0f});
        ImGui::Text("%s", m_statusMsg.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Confirm modal
// ---------------------------------------------------------------------------
void DbViewerApp::DrawOpenConfirmModal() {
    if (m_openDbConfirm) ImGui::OpenPopup("Open database?");

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
        ImGuiCond_Always, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Open database?", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Close the current database and open a new one?");
        ImGui::Spacing();
        if (ImGui::Button("Open", {100.0f, 0.0f})) {
            m_openDbConfirm = false;
            OpenDb(m_pendingOpenPath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {100.0f, 0.0f})) {
            m_openDbConfirm = false;
            m_pendingOpenPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    m_openDbConfirm = false;
}
