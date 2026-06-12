#pragma once
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include "json.hpp"

class JsonViewerApp {
public:
    bool Init(HINSTANCE hInstance, const wchar_t* initialFile = nullptr);
    int  Run();
    void Shutdown();
    HWND GetHwnd() const { return m_hwnd; }

private:
    // Window + D3D11 ----------------------------------------------------------
    bool CreateAppWindow(HINSTANCE hInstance);
    bool CreateD3D();
    void DestroyD3D();
    void CreateRenderTarget();
    void DestroyRenderTarget();
    void ResizeSwapChain(UINT w, UINT h);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    // Render loop -------------------------------------------------------------
    void Render();

    // UI panels ---------------------------------------------------------------
    void DrawMenuBar();
    void DrawToolbar();
    void DrawTreePanel(float availH);
    void DrawRawPanel(float height);
    void DrawStatusBar();

    // JSON tree ---------------------------------------------------------------
    void DrawJsonNode(const std::string& key,
                      const nlohmann::ordered_json& node, int depth);
    bool ContainsMatch(const nlohmann::ordered_json& node,
                       const std::string& lsearch);

    // File handling -----------------------------------------------------------
    bool OpenFile(const std::wstring& path);
    void CloseFile();
    void ReloadFile();
    std::wstring OpenFileDialog();
    void CheckDroppedFile(HDROP hDrop);

    // Recents -----------------------------------------------------------------
    void LoadRecents();
    void SaveRecents();
    void AddToRecents(const std::wstring& path);

    // Theming -----------------------------------------------------------------
    void ApplyTheme();

    // Win32 + D3D -------------------------------------------------------------
    HWND                    m_hwnd{};
    HINSTANCE               m_hInstance{};
    IDXGISwapChain*         m_swapChain{};
    ID3D11Device*           m_device{};
    ID3D11DeviceContext*    m_context{};
    ID3D11RenderTargetView* m_renderTarget{};
    bool                    m_swapChainOccluded{};
    UINT                    m_resizeW{};
    UINT                    m_resizeH{};

    // JSON state --------------------------------------------------------------
    nlohmann::ordered_json m_json;
    bool             m_jsonLoaded{};
    std::wstring     m_filePath;
    std::string      m_rawJson;
    std::string      m_parseError;
    size_t           m_fileSize{};
    int              m_nodeCount{};
    int              m_nodeCounter{};  // unique ID counter, reset each frame

    // UI state ----------------------------------------------------------------
    char        m_searchBuf[256]{};
    std::string m_searchLower;
    bool        m_showRaw{};
    float       m_rawPanelH{200.0f};
    bool        m_forceExpandOnce{};
    bool        m_forceCollapseOnce{};
    bool        m_needsReload{};

    // Recents -----------------------------------------------------------------
    static constexpr int kMaxRecents = 10;
    std::vector<std::wstring> m_recents;

    // Status ------------------------------------------------------------------
    std::string m_statusMsg;
};
