#include "JsonViewer.h"

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
// Value parser - infers JSON type from a text buffer
// ---------------------------------------------------------------------------
static nlohmann::ordered_json ParseValue(const char* buf) {
    std::string s = buf;
    while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    if (s.empty()) return std::string{};
    if (s == "true")  return true;
    if (s == "false") return false;
    if (s == "null")  return nullptr;
    // JSON object/array - try to parse
    if (s.front() == '{' || s.front() == '[') {
        try { return nlohmann::ordered_json::parse(s); } catch (...) {}
    }
    // Number
    if (isdigit((unsigned char)s.front()) || s.front() == '-') {
        try {
            size_t pos;
            if (s.find_first_of(".eE") != std::string::npos) {
                double d = std::stod(s, &pos);
                if (pos == s.size()) return d;
            } else {
                long long ll = std::stoll(s, &pos);
                if (pos == s.size()) return ll;
            }
        } catch (...) {}
    }
    // Quoted string → strip quotes
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        try {
            auto j = nlohmann::ordered_json::parse(s);
            if (j.is_string()) return j;
        } catch (...) {}
        return s.substr(1, s.size() - 2);
    }
    return s;  // plain string
}

// Convert our dot-bracket path notation to a JSON Pointer (RFC 6901) string
// e.g. "user.name" → "/user/name",  "items[0].v" → "/items/0/v"
static std::string ToJsonPointer(const std::string& path) {
    if (path.empty()) return "";
    std::string jp, seg;
    auto flush = [&]() { if (!seg.empty()) { jp += "/" + seg; seg.clear(); } };
    for (char c : path) {
        if (c == '[' || c == ']' || c == '.') flush();
        else seg += c;
    }
    flush();
    return jp;
}

// Return the path of the parent node
// "user.name" → "user",  "items[0]" → "items",  "name" → ""
static std::string GetParentPath(const std::string& path) {
    size_t lastDot = std::string::npos, lastBrk = std::string::npos;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '.') lastDot = i;
        if (path[i] == '[') lastBrk = i;
    }
    size_t pos = std::string::npos;
    if (lastDot != std::string::npos && lastBrk != std::string::npos)
        pos = std::max(lastDot, lastBrk);
    else if (lastDot != std::string::npos) pos = lastDot;
    else if (lastBrk != std::string::npos) pos = lastBrk;
    return pos == std::string::npos ? "" : path.substr(0, pos);
}

// Rename a key in an ordered_json object while preserving insertion order
static void RenameObjectKey(nlohmann::ordered_json& obj,
                            const std::string& oldKey, const std::string& newKey) {
    if (!obj.is_object() || oldKey == newKey) return;
    nlohmann::ordered_json rebuilt;
    for (auto& [k, v] : obj.items())
        rebuilt[k == oldKey ? newKey : k] = v;
    obj = std::move(rebuilt);
}

// ---------------------------------------------------------------------------
// Recents path
// ---------------------------------------------------------------------------
static std::wstring GetRecentsPath() {
    wchar_t appdata[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) return {};
    return std::wstring(appdata) + L"\\json_viewer_recents.txt";
}

// ---------------------------------------------------------------------------
// Win32 window
// ---------------------------------------------------------------------------
static constexpr wchar_t kWndClass[] = L"JsonViewerWindow";

LRESULT CALLBACK JsonViewerApp::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    auto* app = reinterpret_cast<JsonViewerApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

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

bool JsonViewerApp::CreateAppWindow(HINSTANCE hInstance) {
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
        kWndClass, L"JSON Editor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 750,
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
bool JsonViewerApp::CreateD3D() {
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

void JsonViewerApp::DestroyD3D() {
    DestroyRenderTarget();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
    if (m_context)   { m_context->Release();   m_context   = nullptr; }
    if (m_device)    { m_device->Release();     m_device    = nullptr; }
}

void JsonViewerApp::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_device->CreateRenderTargetView(back, nullptr, &m_renderTarget);
        back->Release();
    }
}

void JsonViewerApp::DestroyRenderTarget() {
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }
}

void JsonViewerApp::ResizeSwapChain(UINT w, UINT h) {
    DestroyRenderTarget();
    m_swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

// ---------------------------------------------------------------------------
// Theme (matches sqlite editor / clipboardpp)
// ---------------------------------------------------------------------------
void JsonViewerApp::ApplyTheme() {
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
bool JsonViewerApp::Init(HINSTANCE hInstance, const wchar_t* initialFile) {
    m_hInstance = hInstance;

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

    // Monospace font for raw JSON panel (index 1)
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\consola.ttf") != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 13.0f, &fc);

    ApplyTheme();
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    LoadRecents();

    if (initialFile && initialFile[0])
        OpenFile(initialFile);

    return true;
}

int JsonViewerApp::Run() {
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

void JsonViewerApp::Shutdown() {
    CloseFile();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyD3D();
    DestroyWindow(m_hwnd);
    UnregisterClassW(kWndClass, m_hInstance);
}

// ---------------------------------------------------------------------------
// Recents
// ---------------------------------------------------------------------------
void JsonViewerApp::LoadRecents() {
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

void JsonViewerApp::SaveRecents() {
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

void JsonViewerApp::AddToRecents(const std::wstring& path) {
    auto it = std::find(m_recents.begin(), m_recents.end(), path);
    if (it != m_recents.end()) m_recents.erase(it);
    m_recents.insert(m_recents.begin(), path);
    if ((int)m_recents.size() > kMaxRecents)
        m_recents.resize(kMaxRecents);
    SaveRecents();
}

// ---------------------------------------------------------------------------
// File handling
// ---------------------------------------------------------------------------
static int CountNodesRecursive(const nlohmann::ordered_json& j) {
    if (j.is_object()) {
        int n = 0;
        for (auto& [k, v] : j.items()) n += CountNodesRecursive(v);
        return n;
    }
    if (j.is_array()) {
        int n = 0;
        for (auto& v : j) n += CountNodesRecursive(v);
        return n;
    }
    return 1;
}

bool JsonViewerApp::OpenFile(const std::wstring& path) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) {
        m_parseError = "Cannot open file.";
        m_jsonLoaded = false;
        return false;
    }

    fseek(f, 0, SEEK_END);
    m_fileSize = static_cast<size_t>(ftell(f));
    fseek(f, 0, SEEK_SET);
    m_rawJson.resize(m_fileSize);
    fread(m_rawJson.data(), 1, m_fileSize, f);
    fclose(f);

    try {
        m_json       = nlohmann::ordered_json::parse(m_rawJson);
        m_parseError.clear();
        m_jsonLoaded = true;
        m_nodeCount  = CountNodesRecursive(m_json);
    } catch (const nlohmann::json::parse_error& e) {
        m_parseError = e.what();
        m_jsonLoaded = false;
        m_nodeCount  = 0;
    }

    m_filePath = path;
    AddToRecents(path);

    m_isDirty   = false;
    m_rawDirty  = false;
    m_editPath.clear();
    m_editFocus = false;
    m_pendingDelete = false;
    m_pendingDeleteParent = nullptr;

    UpdateTitleBar();

    m_statusMsg = m_jsonLoaded
        ? std::to_string(m_nodeCount) + " values"
        : "Parse error";

    return m_jsonLoaded;
}

void JsonViewerApp::CloseFile() {
    m_json       = nlohmann::ordered_json{};
    m_jsonLoaded = false;
    m_filePath.clear();
    m_rawJson.clear();
    m_parseError.clear();
    m_fileSize   = 0;
    m_nodeCount  = 0;
    m_statusMsg.clear();
    m_selKey.clear();    m_selPath.clear();
    m_selType.clear();   m_selValue.clear();
    m_isDirty   = false; m_rawDirty = false;
    m_editPath.clear();  m_editFocus = false;
    m_pendingDelete = false; m_pendingDeleteParent = nullptr;
    m_addTarget = nullptr;   m_addModalPending = false;
    UpdateTitleBar();
}

void JsonViewerApp::ReloadFile() {
    if (m_filePath.empty()) return;
    const std::wstring p = m_filePath;
    CloseFile();
    OpenFile(p);
}

bool JsonViewerApp::SaveFile() {
    if (m_filePath.empty() || !m_jsonLoaded) return false;
    if (m_rawDirty) m_rawJson = m_json.dump(2);
    FILE* f = nullptr;
    _wfopen_s(&f, m_filePath.c_str(), L"wb");
    if (!f) { m_statusMsg = "Error: could not open file for writing."; return false; }
    fwrite(m_rawJson.data(), 1, m_rawJson.size(), f);
    fclose(f);
    m_isDirty  = false;
    m_rawDirty = false;
    m_statusMsg = "Saved.";
    UpdateTitleBar();
    return true;
}

void JsonViewerApp::UpdateTitleBar() {
    if (m_filePath.empty()) {
        SetWindowTextW(m_hwnd, L"JSON Editor");
        return;
    }
    std::string fname = WideToUtf8(m_filePath);
    const auto slash = fname.find_last_of("\\/");
    const std::string title = "JSON Editor  \xe2\x80\x94  " +
        (slash != std::string::npos ? fname.substr(slash + 1) : fname) +
        (m_isDirty ? " *" : "");
    SetWindowTextA(m_hwnd, title.c_str());
}

void JsonViewerApp::CommitEdit(nlohmann::ordered_json& node, const std::string& path) {
    // Preserve the original type for strings and booleans; infer for others
    if (node.is_string()) {
        node = std::string(m_editBuf);
    } else if (node.is_boolean()) {
        std::string s(m_editBuf);
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        node = (s == "true" || s == "1" || s == "yes");
    } else {
        node = ParseValue(m_editBuf);
    }
    m_isDirty  = true;
    m_rawDirty = true;
    UpdateTitleBar();
    // Refresh detail panel if this node is currently selected
    if (m_selPath == path) {
        if (node.is_string())      { m_selType = "string";  m_selValue = node.get<std::string>(); }
        else if (node.is_boolean()){ m_selType = "boolean"; m_selValue = node.get<bool>() ? "true" : "false"; }
        else if (node.is_null())   { m_selType = "null";    m_selValue = "null"; }
        else                       { m_selType = "number";  m_selValue = node.dump(); }
        ++m_selGeneration;
    }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void JsonViewerApp::Render() {
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = m_fontScale;
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

    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_O)) {
        auto p = OpenFileDialog();
        if (!p.empty()) OpenFile(p);
    }
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S))
        SaveFile();
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_Equal))
        m_fontScale = std::min(2.5f, m_fontScale + 0.1f);
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_Minus))
        m_fontScale = std::max(0.7f, m_fontScale - 0.1f);
    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_0))
        m_fontScale = 1.0f;
    if (ImGui::IsKeyPressed(ImGuiKey_F5) && !m_filePath.empty())
        m_needsReload = true;
    if (m_needsReload) {
        ReloadFile();
        m_needsReload = false;
    }

    DrawToolbar();

    const float kStatusH = 22.0f;
    const float availH   = sh - ImGui::GetCursorPosY() - kStatusH;
    const bool  hasDetail = m_jsonLoaded;
    const float splW    = hasDetail ? 4.0f : 0.0f;
    const float detailW = hasDetail ? sw * m_detailPanelRatio : 0.0f;
    const float leftW   = sw - detailW - splW;

    // Left column: tree panel + optional raw panel below
    ImGui::BeginChild("##leftcol", {leftW, availH}, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);
    if (m_showRaw) {
        const float treeH = std::max(40.0f, availH - m_rawPanelH - 4.0f);
        DrawTreePanel(treeH);

        ImGui::PushStyleColor(ImGuiCol_Button,        {0.15f, 0.15f, 0.16f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.149f, 0.475f, 1.0f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.149f, 0.475f, 1.0f, 0.8f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
        ImGui::Button("##hresize", {ImGui::GetContentRegionAvail().x, 4.0f});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemActive()) {
            m_rawPanelH -= io.MouseDelta.y;
            m_rawPanelH  = std::clamp(m_rawPanelH, 60.0f, availH * 0.8f);
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

        DrawRawPanel(m_rawPanelH);
    } else {
        DrawTreePanel(availH);
    }
    ImGui::EndChild();

    // Vertical splitter + detail panel
    if (hasDetail) {
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.15f, 0.15f, 0.16f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.149f, 0.475f, 1.0f, 0.5f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.149f, 0.475f, 1.0f, 0.8f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
        ImGui::Button("##vsplit", {splW, availH});
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemActive() && sw > 0.0f) {
            m_detailPanelRatio -= io.MouseDelta.x / sw;
            m_detailPanelRatio  = std::clamp(m_detailPanelRatio, 0.10f, 0.70f);
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::SameLine(0, 0);
        DrawDetailPanel(detailW, availH);
    }

    ImGui::SetCursorPosY(sh - kStatusH);
    DrawStatusBar();

    // Clear one-shot expand/collapse flags after the tree panel used them
    m_forceExpandOnce   = false;
    m_forceCollapseOnce = false;

    DrawAddModal();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------
void JsonViewerApp::DrawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            auto p = OpenFileDialog();
            if (!p.empty()) OpenFile(p);
        }
        if (ImGui::MenuItem("Save", "Ctrl+S", false, m_jsonLoaded && m_isDirty))
            SaveFile();

        const bool hasRecents = !m_recents.empty();
        if (ImGui::BeginMenu("Recent Files", hasRecents)) {
            for (int i = 0; i < (int)m_recents.size(); ++i) {
                const auto& r = m_recents[i];
                std::string display = WideToUtf8(r);
                const auto slash = display.find_last_of("\\/");
                if (slash != std::string::npos) display = display.substr(slash + 1);

                ImGui::PushID(i);
                const bool exists = (GetFileAttributesW(r.c_str()) != INVALID_FILE_ATTRIBUTES);
                if (!exists)
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (ImGui::MenuItem(display.c_str(), nullptr, false, exists))
                    OpenFile(r);
                if (!exists) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(WideToUtf8(r).c_str());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Recent Files")) { m_recents.clear(); SaveRecents(); }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Reload", "F5", false, !m_filePath.empty()))
            m_needsReload = true;
        if (ImGui::MenuItem("Close", nullptr, false, m_jsonLoaded))
            CloseFile();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Expand All",   nullptr, false, m_jsonLoaded))
            m_forceExpandOnce   = true;
        if (ImGui::MenuItem("Collapse All", nullptr, false, m_jsonLoaded))
            m_forceCollapseOnce = true;
        ImGui::Separator();
        ImGui::MenuItem("Raw JSON Panel", nullptr, &m_showRaw);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

// ---------------------------------------------------------------------------
// Toolbar (search + expand/collapse + raw toggle)
// ---------------------------------------------------------------------------
void JsonViewerApp::DrawToolbar() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.100f, 0.100f, 0.100f, 1.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0f, 6.0f});
    ImGui::BeginChild("##toolbar", {0.0f, 34.0f}, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    ImGui::SetNextItemWidth(260.0f);
    const bool changed = ImGui::InputTextWithHint("##search",
        "Search keys and values...", m_searchBuf, sizeof(m_searchBuf));
    if (changed) {
        m_searchLower = m_searchBuf;
        std::transform(m_searchLower.begin(), m_searchLower.end(),
                       m_searchLower.begin(), ::tolower);
    }
    if (m_searchBuf[0]) {
        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton("x")) { m_searchBuf[0] = '\0'; m_searchLower.clear(); }
    }

    ImGui::SameLine(0, 14);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 14);

    ImGui::BeginDisabled(!m_jsonLoaded);
    if (ImGui::SmallButton("Expand All"))   m_forceExpandOnce   = true;
    ImGui::SameLine(0, 6);
    if (ImGui::SmallButton("Collapse All")) m_forceCollapseOnce = true;
    ImGui::EndDisabled();

    ImGui::SameLine(0, 14);
    ImGui::TextDisabled("|");
    ImGui::SameLine(0, 14);
    if (ImGui::SmallButton("A-"))
        m_fontScale = std::max(0.7f, m_fontScale - 0.1f);
    ImGui::SameLine(0, 5);
    char scaleLabel[8];
    snprintf(scaleLabel, sizeof(scaleLabel), "%d%%", (int)(m_fontScale * 100.0f + 0.5f));
    ImGui::TextDisabled("%s", scaleLabel);
    ImGui::SameLine(0, 5);
    if (ImGui::SmallButton("A+"))
        m_fontScale = std::min(2.5f, m_fontScale + 0.1f);

    // "Raw" toggle right-aligned
    const float rawBtnW = 46.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - rawBtnW - 8.0f, 0);
    if (m_showRaw)
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::SmallButton("Raw")) m_showRaw = !m_showRaw;
    if (m_showRaw) ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Tree panel
// ---------------------------------------------------------------------------
void JsonViewerApp::DrawTreePanel(float availH) {
    // Process pending structural change before rendering the tree
    if (m_pendingDelete && m_pendingDeleteParent) {
        if (m_pendingDeleteIsArr)
            m_pendingDeleteParent->erase(
                m_pendingDeleteParent->begin() + static_cast<ptrdiff_t>(m_pendingDeleteIdx));
        else
            m_pendingDeleteParent->erase(m_pendingDeleteKey);
        m_isDirty   = true;
        m_rawDirty  = true;
        m_nodeCount = CountNodesRecursive(m_json);
        m_pendingDelete = false;
        m_pendingDeleteParent = nullptr;
        UpdateTitleBar();
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.118f, 0.118f, 0.118f, 1.0f});
    ImGui::BeginChild("##tree", {0.0f, availH}, ImGuiChildFlags_None);

    if (!m_jsonLoaded) {
        // Centered placeholder text
        const float textH = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        ImGui::SetCursorPosY(std::max(8.0f, (availH - textH) * 0.5f));
        const char* line1 = m_parseError.empty()
            ? "Drop a JSON file here, or use File > Open"
            : "Parse error:";
        const char* line2 = m_parseError.empty() ? nullptr : m_parseError.c_str();

        auto centerText = [&](const char* t, ImVec4 col) {
            const float tw = ImGui::CalcTextSize(t).x;
            ImGui::SetCursorPosX(
                std::max(8.0f, (ImGui::GetContentRegionAvail().x - tw) * 0.5f));
            ImGui::TextColored(col, "%s", t);
        };

        const ImVec4 dimCol = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        const ImVec4 errCol = {0.878f, 0.424f, 0.459f, 1.0f};
        centerText(line1, m_parseError.empty() ? dimCol : errCol);
        if (line2) centerText(line2, errCol);
        ImGui::Dummy({0, 0});
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 16.0f);
        m_nodeCounter = 0;
        DrawJsonNode("", m_json, 0, "", nullptr, 0);
        ImGui::PopStyleVar();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// JSON tree node (recursive)
// ---------------------------------------------------------------------------
static constexpr ImVec4 kColorKey    = {0.671f, 0.698f, 0.749f, 1.0f}; // blue-gray
static constexpr ImVec4 kColorString = {0.596f, 0.765f, 0.474f, 1.0f}; // green
static constexpr ImVec4 kColorNumber = {0.820f, 0.604f, 0.400f, 1.0f}; // orange
static constexpr ImVec4 kColorBool   = {0.337f, 0.714f, 0.761f, 1.0f}; // cyan
static constexpr ImVec4 kColorNull   = {0.776f, 0.471f, 0.867f, 1.0f}; // purple
static constexpr ImVec4 kColorMatch  = {0.95f,  0.82f,  0.30f,  1.0f}; // yellow highlight
static constexpr size_t kChildLimit  = 500;

void JsonViewerApp::DrawJsonNode(const std::string& key, nlohmann::ordered_json& node,
                                 int depth, const std::string& path,
                                 nlohmann::ordered_json* parent, size_t arrIdx) {
    ImGui::PushID(m_nodeCounter++);

    const bool hasSearch = !m_searchLower.empty();

    const bool km = [&]() -> bool {
        if (!hasSearch || key.empty()) return false;
        std::string lk = key;
        std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
        return lk.find(m_searchLower) != std::string::npos;
    }();

    if (node.is_object() || node.is_array()) {
        const bool isObj = node.is_object();

        std::string label;
        if (!key.empty())
            label = key + (isObj
                ? "  {" + std::to_string(node.size()) + "}"
                : "  [" + std::to_string(node.size()) + "]");
        else
            label = isObj
                ? "{" + std::to_string(node.size()) + " keys}"
                : "[" + std::to_string(node.size()) + " items]";

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (depth == 0) flags |= ImGuiTreeNodeFlags_DefaultOpen;

        if (hasSearch && ContainsMatch(node, m_searchLower))
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (m_forceExpandOnce)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (m_forceCollapseOnce && depth > 0)
            ImGui::SetNextItemOpen(false, ImGuiCond_Always);

        if (km) ImGui::PushStyleColor(ImGuiCol_Text, kColorMatch);
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
        if (km) ImGui::PopStyleColor();

        if (ImGui::IsItemClicked()) {
            m_selKey   = key;
            m_selPath  = path;
            m_selType  = isObj
                ? "object  {" + std::to_string(node.size()) + " keys}"
                : "array  [" + std::to_string(node.size()) + " items]";
            m_selValue = node.dump(2);
            ++m_selGeneration;
        }

        // Context menu: add / delete
        if (ImGui::BeginPopupContextItem("##ctxcont")) {
            if (isObj && ImGui::MenuItem("Add property...")) {
                m_addTarget      = &node;
                m_addTargetIsArr = false;
                m_addModalPending = true;
                memset(m_addKeyBuf, 0, sizeof(m_addKeyBuf));
                memset(m_addValBuf, 0, sizeof(m_addValBuf));
            }
            if (!isObj && ImGui::MenuItem("Add item...")) {
                m_addTarget      = &node;
                m_addTargetIsArr = true;
                m_addModalPending = true;
                memset(m_addKeyBuf, 0, sizeof(m_addKeyBuf));
                memset(m_addValBuf, 0, sizeof(m_addValBuf));
            }
            if (parent) {
                ImGui::Separator();
                if (ImGui::MenuItem("Delete")) {
                    m_pendingDelete       = true;
                    m_pendingDeleteParent = parent;
                    m_pendingDeleteKey    = key;
                    m_pendingDeleteIdx    = arrIdx;
                    m_pendingDeleteIsArr  = parent->is_array();
                    if (m_selPath == path ||
                        m_selPath.find(path + ".") == 0 ||
                        m_selPath.find(path + "[") == 0) {
                        m_selKey.clear(); m_selPath.clear();
                        m_selType.clear(); m_selValue.clear();
                    }
                }
            }
            ImGui::EndPopup();
        }

        if (open) {
            if (isObj) {
                size_t count = 0;
                for (auto& [k, v] : node.items()) {
                    if (count++ >= kChildLimit) {
                        ImGui::TextDisabled("  ... %zu more keys",
                            node.size() - kChildLimit);
                        break;
                    }
                    DrawJsonNode(k, v, depth + 1,
                        path.empty() ? k : path + "." + k,
                        &node, 0);
                }
            } else {
                const size_t limit = std::min(node.size(), kChildLimit);
                for (size_t i = 0; i < limit; ++i) {
                    const std::string idx = std::to_string(i);
                    DrawJsonNode("[" + idx + "]", node[i], depth + 1,
                        path + "[" + idx + "]",
                        &node, i);
                }
                if (node.size() > kChildLimit)
                    ImGui::TextDisabled("  ... %zu more items",
                        node.size() - kChildLimit);
            }
            ImGui::TreePop();
        }
    } else {
        // Leaf node
        std::string valueStr;
        ImVec4 valueColor;

        if (node.is_string()) {
            valueStr   = "\"" + node.get<std::string>() + "\"";
            valueColor = kColorString;
        } else if (node.is_boolean()) {
            valueStr   = node.get<bool>() ? "true" : "false";
            valueColor = kColorBool;
        } else if (node.is_null()) {
            valueStr   = "null";
            valueColor = kColorNull;
        } else {
            valueStr   = node.dump();
            valueColor = kColorNumber;
        }

        const bool vm = [&]() -> bool {
            if (!hasSearch) return false;
            std::string lv = node.is_string() ? node.get<std::string>() : valueStr;
            std::transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
            return lv.find(m_searchLower) != std::string::npos;
        }();

        const bool editing = (m_editPath == path);

        const ImGuiTreeNodeFlags leafFlags =
            ImGuiTreeNodeFlags_Leaf |
            ImGuiTreeNodeFlags_NoTreePushOnOpen |
            ImGuiTreeNodeFlags_SpanAvailWidth |
            ((km || vm || editing) ? ImGuiTreeNodeFlags_Selected : 0);

        if (km || vm)
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{0.149f, 0.475f, 1.0f, 0.18f});
        else if (editing)
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{0.149f, 0.475f, 1.0f, 0.30f});

        ImGui::TreeNodeEx("##leaf", leafFlags);

        if (km || vm || editing) ImGui::PopStyleColor();

        // Click detection: single → select, double → enter edit mode
        if (!editing && ImGui::IsItemClicked()) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                m_editPath = path;
                if (node.is_string())
                    strncpy_s(m_editBuf, sizeof(m_editBuf),
                        node.get<std::string>().c_str(), _TRUNCATE);
                else
                    strncpy_s(m_editBuf, sizeof(m_editBuf),
                        node.dump().c_str(), _TRUNCATE);
                m_editFocus = true;
            } else {
                m_selKey  = key;
                m_selPath = path;
                if (node.is_string())      { m_selType = "string";  m_selValue = node.get<std::string>(); }
                else if (node.is_boolean()){ m_selType = "boolean"; m_selValue = node.get<bool>() ? "true" : "false"; }
                else if (node.is_null())   { m_selType = "null";    m_selValue = "null"; }
                else                       { m_selType = "number";  m_selValue = node.dump(); }
                ++m_selGeneration;
            }
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem("##ctx")) {
            if (ImGui::MenuItem("Edit value")) {
                m_editPath = path;
                if (node.is_string())
                    strncpy_s(m_editBuf, sizeof(m_editBuf),
                        node.get<std::string>().c_str(), _TRUNCATE);
                else
                    strncpy_s(m_editBuf, sizeof(m_editBuf),
                        node.dump().c_str(), _TRUNCATE);
                m_editFocus = true;
            }
            ImGui::Separator();
            if (!key.empty() && ImGui::MenuItem("Copy key"))
                ImGui::SetClipboardText(key.c_str());
            if (ImGui::MenuItem("Copy value"))
                ImGui::SetClipboardText(valueStr.c_str());
            if (parent) {
                ImGui::Separator();
                if (ImGui::MenuItem("Delete")) {
                    m_pendingDelete       = true;
                    m_pendingDeleteParent = parent;
                    m_pendingDeleteKey    = key;
                    m_pendingDeleteIdx    = arrIdx;
                    m_pendingDeleteIsArr  = parent->is_array();
                    if (m_selPath == path) {
                        m_selKey.clear(); m_selPath.clear();
                        m_selType.clear(); m_selValue.clear();
                    }
                    if (m_editPath == path) m_editPath.clear();
                }
            }
            ImGui::EndPopup();
        }

        // Key label
        ImGui::SameLine(0, 4);
        if (!key.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, km ? kColorMatch : kColorKey);
            ImGui::TextUnformatted(key.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0);
            ImGui::TextDisabled(" :  ");
            ImGui::SameLine(0, 0);
        }

        // Value: inline InputText when editing, static text otherwise
        if (editing) {
            if (m_editFocus) { ImGui::SetKeyboardFocusHere(); m_editFocus = false; }
            ImGui::SetNextItemWidth(
                std::max(80.0f, ImGui::GetContentRegionAvail().x - 4.0f));
            const bool entered = ImGui::InputText("##iedit", m_editBuf, sizeof(m_editBuf),
                ImGuiInputTextFlags_EnterReturnsTrue);
            const bool esc   = ImGui::IsKeyPressed(ImGuiKey_Escape);
            const bool deact = ImGui::IsItemDeactivated();
            if (entered || (deact && !esc))
                CommitEdit(node, path);
            if (esc || deact)
                m_editPath.clear();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, vm ? kColorMatch : valueColor);
            ImGui::TextUnformatted(valueStr.c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// ContainsMatch - lsearch must be pre-lowercased by the caller
// ---------------------------------------------------------------------------
bool JsonViewerApp::ContainsMatch(const nlohmann::ordered_json& node,
                                  const std::string& lsearch) {
    if (lsearch.empty()) return false;

    if (node.is_object()) {
        for (auto& [k, v] : node.items()) {
            std::string lk = k;
            std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
            if (lk.find(lsearch) != std::string::npos) return true;
            if (ContainsMatch(v, lsearch)) return true;
        }
    } else if (node.is_array()) {
        for (auto& v : node)
            if (ContainsMatch(v, lsearch)) return true;
    } else if (node.is_string()) {
        std::string lv = node.get<std::string>();
        std::transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
        if (lv.find(lsearch) != std::string::npos) return true;
    } else if (!node.is_null()) {
        std::string lv = node.dump();
        std::transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
        if (lv.find(lsearch) != std::string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Raw JSON panel
// ---------------------------------------------------------------------------
void JsonViewerApp::DrawRawPanel(float height) {
    if (m_rawDirty) { m_rawJson = m_json.dump(2); m_rawDirty = false; }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.090f, 0.090f, 0.095f, 1.0f});
    ImGui::BeginChild("##raw", {0.0f, height});

    ImGui::PushStyleColor(ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::Text("  RAW JSON");
    ImGui::PopStyleColor();
    ImGui::Separator();

    const float inputH = height - ImGui::GetCursorPosY() - 6.0f;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.090f, 0.090f, 0.095f, 1.0f});

    // Use monospace font if loaded (index 1)
    const ImGuiIO& io = ImGui::GetIO();
    ImFont* monoFont = (io.Fonts->Fonts.Size > 1) ? io.Fonts->Fonts[1] : nullptr;
    if (monoFont) ImGui::PushFont(monoFont);

    ImGui::InputTextMultiline("##rawtext",
        const_cast<char*>(m_rawJson.c_str()),
        m_rawJson.size() + 1,
        {-1.0f, inputH},
        ImGuiInputTextFlags_ReadOnly);

    if (monoFont) ImGui::PopFont();
    ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Detail panel - shows and edits the selected node's key and value
// ---------------------------------------------------------------------------
void JsonViewerApp::DrawDetailPanel(float panelW, float availH) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.090f, 0.090f, 0.095f, 1.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f, 8.0f});
    ImGui::BeginChild("##detail", {panelW, availH}, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    // Repopulate editable buffers whenever the selection changes
    if (m_selGeneration != m_detailLastGen) {
        strncpy_s(m_detailKeyBuf, sizeof(m_detailKeyBuf), m_selKey.c_str(), _TRUNCATE);
        strncpy_s(m_detailValBuf, sizeof(m_detailValBuf), m_selValue.c_str(), _TRUNCATE);
        m_detailLastGen = m_selGeneration;
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted("DETAILS");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    if (m_selType.empty()) {
        ImGui::TextDisabled("Click a node to inspect it.");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    const bool isContainer = m_selType.rfind("object", 0) == 0 ||
                             m_selType.rfind("array",  0) == 0;
    // Keys are editable for plain object children (not array indices, not root)
    const bool keyEditable = !m_selKey.empty() && !m_selPath.empty() &&
                             m_selKey.front() != '[';

    // ── KEY ──────────────────────────────────────────────────────────────────
    ImGui::TextDisabled("KEY");
    ImGui::SetNextItemWidth(-1.0f);
    if (keyEditable) {
        ImGui::InputText("##dkey", m_detailKeyBuf, sizeof(m_detailKeyBuf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const std::string newKey(m_detailKeyBuf);
            if (!newKey.empty() && newKey != m_selKey) {
                const std::string parentPath = GetParentPath(m_selPath);
                try {
                    auto& parentNode = parentPath.empty()
                        ? m_json
                        : m_json.at(nlohmann::json::json_pointer(ToJsonPointer(parentPath)));
                    if (parentNode.is_object()) {
                        RenameObjectKey(parentNode, m_selKey, newKey);
                        m_selPath = parentPath.empty() ? newKey : parentPath + "." + newKey;
                        m_selKey  = newKey;
                        m_isDirty = true; m_rawDirty = true;
                        UpdateTitleBar();
                        ++m_selGeneration;
                        m_detailLastGen = m_selGeneration;
                        strncpy_s(m_detailKeyBuf, sizeof(m_detailKeyBuf), newKey.c_str(), _TRUNCATE);
                    }
                } catch (...) {}
            }
        }
    } else {
        const std::string displayKey = m_selKey.empty() ? "(root)" : m_selKey;
        ImGui::InputText("##dkey_ro", const_cast<char*>(displayKey.c_str()),
            displayKey.size() + 1, ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::Spacing();

    // ── PATH (read-only) ─────────────────────────────────────────────────────
    if (!m_selPath.empty()) {
        ImGui::TextDisabled("PATH");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##dpath", const_cast<char*>(m_selPath.c_str()),
            m_selPath.size() + 1, ImGuiInputTextFlags_ReadOnly);
        ImGui::Spacing();
    }

    // ── TYPE (read-only) ─────────────────────────────────────────────────────
    ImGui::TextDisabled("TYPE");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##dtype", const_cast<char*>(m_selType.c_str()),
        m_selType.size() + 1, ImGuiInputTextFlags_ReadOnly);
    ImGui::Spacing();

    // ── VALUE ────────────────────────────────────────────────────────────────
    ImGui::TextDisabled(!isContainer ? "VALUE  (Enter new lines freely; click Apply to save)"
                                     : "VALUE");

    const float applyH = !isContainer ? (ImGui::GetFrameHeight() + 6.0f) : 0.0f;
    const float valH   = std::max(40.0f, ImGui::GetContentRegionAvail().y - applyH - 2.0f);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.090f, 0.090f, 0.095f, 1.0f});
    const ImGuiIO& fio = ImGui::GetIO();
    ImFont* mono = (fio.Fonts->Fonts.Size > 1) ? fio.Fonts->Fonts[1] : nullptr;
    if (mono) ImGui::PushFont(mono);

    char valId[32];
    snprintf(valId, sizeof(valId), "##dval%d", m_selGeneration);

    if (!isContainer) {
        // Scalar value - editable
        ImGui::InputTextMultiline(valId, m_detailValBuf, sizeof(m_detailValBuf),
            {-1.0f, valH});
        const bool valDeact = ImGui::IsItemDeactivatedAfterEdit();
        if (mono) ImGui::PopFont();
        ImGui::PopStyleColor();

        const bool applyClicked = ImGui::Button("Apply", {-1.0f, 0.0f});
        if (applyClicked || valDeact) {
            try {
                auto& target = m_selPath.empty()
                    ? m_json
                    : m_json.at(nlohmann::json::json_pointer(ToJsonPointer(m_selPath)));

                if (target.is_string()) {
                    target = std::string(m_detailValBuf);
                } else if (target.is_boolean()) {
                    std::string s(m_detailValBuf);
                    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                    target = (s == "true" || s == "1" || s == "yes");
                } else {
                    target = ParseValue(m_detailValBuf);
                }
                m_isDirty = true; m_rawDirty = true;
                UpdateTitleBar();

                if (target.is_string())       { m_selType = "string";  m_selValue = target.get<std::string>(); }
                else if (target.is_boolean()) { m_selType = "boolean"; m_selValue = target.get<bool>() ? "true" : "false"; }
                else if (target.is_null())    { m_selType = "null";    m_selValue = "null"; }
                else                          { m_selType = "number";  m_selValue = target.dump(); }

                strncpy_s(m_detailValBuf, sizeof(m_detailValBuf), m_selValue.c_str(), _TRUNCATE);
                ++m_selGeneration;
                m_detailLastGen = m_selGeneration;

                if (m_editPath == m_selPath) m_editPath.clear();
            } catch (...) {}
        }
    } else {
        // Container - read-only display
        ImGui::InputTextMultiline(valId,
            const_cast<char*>(m_selValue.c_str()), m_selValue.size() + 1,
            {-1.0f, valH}, ImGuiInputTextFlags_ReadOnly);
        if (mono) ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------
void JsonViewerApp::DrawStatusBar() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.075f, 0.075f, 0.075f, 1.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.0f, 4.0f});
    ImGui::BeginChild("##status", {0.f, 22.0f}, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    if (!m_filePath.empty()) {
        std::string fname = WideToUtf8(m_filePath);
        const auto slash = fname.find_last_of("\\/");
        if (slash != std::string::npos) fname = fname.substr(slash + 1);

        ImGui::TextDisabled("%s", fname.c_str());
        ImGui::SameLine(0, 10.0f);
        ImGui::TextDisabled("|");
        ImGui::SameLine(0, 10.0f);

        if (m_fileSize >= 1024 * 1024)
            ImGui::TextDisabled("%.2f MB", m_fileSize / (1024.0 * 1024.0));
        else if (m_fileSize >= 1024)
            ImGui::TextDisabled("%.1f KB", m_fileSize / 1024.0);
        else
            ImGui::TextDisabled("%zu bytes", m_fileSize);

        if (m_jsonLoaded) {
            ImGui::SameLine(0, 10.0f);
            ImGui::TextDisabled("|");
            ImGui::SameLine(0, 10.0f);
            ImGui::TextDisabled("%d values", m_nodeCount);
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
// Add property / add item modal
// ---------------------------------------------------------------------------
void JsonViewerApp::DrawAddModal() {
    if (m_addModalPending) {
        ImGui::OpenPopup("##addmodal");
        m_addModalPending = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({380.0f, 0.0f});

    if (!ImGui::BeginPopupModal("##addmodal", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        return;

    ImGui::Spacing();
    ImGui::TextUnformatted(m_addTargetIsArr ? "Add Array Item" : "Add Property");
    ImGui::Separator();
    ImGui::Spacing();

    bool focusVal = false;
    if (!m_addTargetIsArr) {
        ImGui::TextDisabled("Key");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##akey", m_addKeyBuf, sizeof(m_addKeyBuf),
                ImGuiInputTextFlags_EnterReturnsTrue))
            focusVal = true;   // Tab from key → value
        ImGui::Spacing();
    }

    ImGui::TextDisabled("Value  (string, 42, true, false, null)");
    ImGui::SetNextItemWidth(-1.0f);
    if (focusVal) ImGui::SetKeyboardFocusHere();
    const bool valEnter = ImGui::InputText("##aval", m_addValBuf, sizeof(m_addValBuf),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Spacing();

    const bool canOk = m_addTargetIsArr || (m_addKeyBuf[0] != '\0');

    ImGui::BeginDisabled(!canOk);
    const bool ok = ImGui::Button("Add", {90.0f, 0}) || valEnter;
    ImGui::EndDisabled();
    ImGui::SameLine(0, 8);
    const bool cancel = ImGui::Button("Cancel", {90.0f, 0}) ||
                        ImGui::IsKeyPressed(ImGuiKey_Escape);

    if (ok && canOk && m_addTarget) {
        nlohmann::ordered_json newVal = ParseValue(m_addValBuf);
        if (m_addTargetIsArr) {
            m_addTarget->push_back(std::move(newVal));
        } else {
            (*m_addTarget)[m_addKeyBuf] = std::move(newVal);
        }
        m_isDirty   = true;
        m_rawDirty  = true;
        m_nodeCount = CountNodesRecursive(m_json);
        UpdateTitleBar();
        m_addTarget = nullptr;
        ImGui::CloseCurrentPopup();
    }
    if (cancel) {
        m_addTarget = nullptr;
        ImGui::CloseCurrentPopup();
    }

    ImGui::Spacing();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// File dialog + drag-and-drop
// ---------------------------------------------------------------------------
std::wstring JsonViewerApp::OpenFileDialog() {
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hwnd;
    ofn.lpstrFilter = L"JSON files\0*.json\0All files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = L"Open JSON File";
    if (!GetOpenFileNameW(&ofn)) return {};
    return buf;
}

void JsonViewerApp::CheckDroppedFile(HDROP hDrop) {
    wchar_t buf[MAX_PATH]{};
    if (DragQueryFileW(hDrop, 0, buf, MAX_PATH))
        OpenFile(buf);
    DragFinish(hDrop);
}
