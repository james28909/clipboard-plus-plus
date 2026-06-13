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
    void DrawJsonNode(const std::string& key, nlohmann::ordered_json& node,
                      int depth, const std::string& path = "",
                      nlohmann::ordered_json* parent = nullptr, size_t arrIdx = 0);
    void DrawDetailPanel(float width, float availH);
    void DrawAddModal();
    bool ContainsMatch(const nlohmann::ordered_json& node,
                       const std::string& lsearch);

    // File handling -----------------------------------------------------------
    bool OpenFile(const std::wstring& path);
    void CloseFile();
    void ReloadFile();
    bool SaveFile();
    std::wstring OpenFileDialog();
    void CheckDroppedFile(HDROP hDrop);

    // Recents -----------------------------------------------------------------
    void LoadRecents();
    void SaveRecents();
    void AddToRecents(const std::wstring& path);

    // Helpers -----------------------------------------------------------------
    void ApplyTheme();
    void UpdateTitleBar();
    void CommitEdit(nlohmann::ordered_json& node, const std::string& path);

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
    float       m_detailPanelRatio{0.40f}; // fraction of window width for detail panel
    float       m_fontScale{1.5f};
    bool        m_forceExpandOnce{};
    bool        m_forceCollapseOnce{};
    bool        m_needsReload{};

    // Selection / detail panel ------------------------------------------------
    std::string m_selKey;
    std::string m_selPath;
    std::string m_selType;
    std::string m_selValue;
    int         m_selGeneration{};
    // Editable buffers for the detail panel (repopulated on selection change)
    char        m_detailKeyBuf[512]{};
    char        m_detailValBuf[16384]{};
    int         m_detailLastGen{-1};

    // Editing -----------------------------------------------------------------
    bool        m_isDirty{};
    bool        m_rawDirty{};
    std::string m_editPath;
    char        m_editBuf[4096]{};
    bool        m_editFocus{};
    // Deferred structural changes (deletion happens before the next tree render)
    bool                    m_pendingDelete{};
    nlohmann::ordered_json* m_pendingDeleteParent{};
    std::string             m_pendingDeleteKey;
    size_t                  m_pendingDeleteIdx{};
    bool                    m_pendingDeleteIsArr{};
    // Add node modal
    bool                    m_addModalPending{};
    nlohmann::ordered_json* m_addTarget{};
    bool                    m_addTargetIsArr{};
    char                    m_addKeyBuf[256]{};
    char                    m_addValBuf[1024]{};

    // Recents -----------------------------------------------------------------
    static constexpr int kMaxRecents = 10;
    std::vector<std::wstring> m_recents;

    // Status ------------------------------------------------------------------
    std::string m_statusMsg;
};
