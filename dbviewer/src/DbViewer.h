#pragma once
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <optional>

struct sqlite3;

struct TableInfo {
    std::string name;
    int64_t     rowCount{-1};
};

struct ColumnInfo {
    std::string name;
    std::string type;
    bool        notNull{};
    bool        isPk{};
};

struct CellEdit {
    int         row{-1};
    int         col{-1};
    std::string original;
    char        buf[1024]{};
};

struct QueryResult {
    std::vector<std::string>              cols;
    std::vector<std::vector<std::string>> rows;
    std::string                           error;
    int                                   affectedRows{};
};

class DbViewerApp {
public:
    bool Init(HINSTANCE hInstance, const wchar_t* initialFile = nullptr);
    int  Run();
    void Shutdown();

    HWND GetHwnd() const { return m_hwnd; }

private:
    // Window + D3D11 -------------------------------------------------------
    bool CreateAppWindow(HINSTANCE hInstance);
    bool CreateD3D();
    void DestroyD3D();
    void CreateRenderTarget();
    void DestroyRenderTarget();
    void ResizeSwapChain(UINT w, UINT h);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    // ImGui render loop ----------------------------------------------------
    void Render();

    // UI panels ------------------------------------------------------------
    void DrawMenuBar();
    void DrawLeftPanel(float panelW);
    void DrawMainArea(float x, float availW, float availH);
    void DrawTableGrid(float availH);
    void DrawImagePreview(float panelW, float availH);
    void DrawSqlPanel(float height);
    void DrawStatusBar();
    void DrawOpenConfirmModal();

    // DB helpers -----------------------------------------------------------
    bool OpenDb(const std::wstring& path);
    void CloseDb();
    void RefreshTableList();
    void SelectTable(const std::string& name);
    void LoadTableData(int offset, int limit);
    void ExecuteQuery(const char* sql);
    void DeleteSelectedRow();
    void CommitEdit();
    std::string PkWhereClause(int rowIdx) const;
    std::string EscapeForSql(const std::string& v, bool numeric) const;

    // Blob / image preview -------------------------------------------------
    void LoadBlobPreview(int rowIdx);
    void ReleasePreview();
    ID3D11ShaderResourceView* DecodeBytesToSrv(const std::vector<uint8_t>& bytes);

    // Recents --------------------------------------------------------------
    void LoadRecents();
    void SaveRecents();
    void AddToRecents(const std::wstring& path);

    // File dialog ----------------------------------------------------------
    std::wstring OpenFileDialog();
    void CheckDroppedFile(HDROP hDrop);

    // Theming --------------------------------------------------------------
    void ApplyTheme();

    // Win32 + D3D ----------------------------------------------------------
    HWND                    m_hwnd{};
    HINSTANCE               m_hInstance{};
    IDXGISwapChain*         m_swapChain{};
    ID3D11Device*           m_device{};
    ID3D11DeviceContext*    m_context{};
    ID3D11RenderTargetView* m_renderTarget{};
    bool                    m_swapChainOccluded{};
    UINT                    m_resizeW{};
    UINT                    m_resizeH{};

    // SQLite ---------------------------------------------------------------
    sqlite3*               m_db{};
    std::wstring           m_dbPath;
    std::vector<TableInfo> m_tables;
    std::string            m_selectedTable;

    // Table data -----------------------------------------------------------
    std::vector<ColumnInfo>               m_cols;
    std::vector<std::vector<std::string>> m_rows;
    std::vector<int>                      m_blobCols;  // column indices declared BLOB
    int                                   m_totalRows{};
    int                                   m_dataOffset{};
    static constexpr int kPageSize = 500;
    int                                   m_sortCol{-1};
    bool                                  m_sortAsc{true};
    int                                   m_selectedRow{-1};
    int                                   m_lastPreviewRow{-2};  // -2 = uninitialized
    std::optional<CellEdit>               m_edit;

    // SQL editor -----------------------------------------------------------
    char        m_sqlBuf[8192]{};
    QueryResult m_queryResult;
    bool        m_showSqlPanel{true};
    float       m_sqlPanelH{160.0f};
    bool        m_sqlDirty{};

    // Image preview --------------------------------------------------------
    ID3D11ShaderResourceView* m_previewSrv{};
    int                       m_previewW{};
    int                       m_previewH{};
    size_t                    m_previewBlobSize{};
    std::string               m_previewMsg;
    float                     m_previewPanelW{300.0f};

    // Recents --------------------------------------------------------------
    static constexpr int kMaxRecents = 10;
    std::vector<std::wstring> m_recents;

    // UI state -------------------------------------------------------------
    float        m_leftPanelW{200.0f};
    char         m_tableFilter[128]{};
    bool         m_openDbConfirm{};
    std::wstring m_pendingOpenPath;
    std::string  m_statusMsg;
    bool         m_needsRefresh{};
};
