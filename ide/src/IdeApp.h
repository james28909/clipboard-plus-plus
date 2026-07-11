#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <shellapi.h>
#include <windows.h>

#include <string>
#include <vector>

struct IdeLaunchOptions {
    std::wstring filePath;
    std::wstring mode;
    bool returnToClipboard{false};
    bool waitMode{false};
};

enum class LanguageMode {
    Text,
    PowerShell,
    Batch,
    Json,
    Markdown,
    Cpp,
};

struct DocumentState {
    std::wstring path;
    std::string name{"Untitled"};
    std::string text;
    LanguageMode language{LanguageMode::Text};
    bool dirty{false};
    size_t cursor{0};
    std::string search;
};

class ClipboardIdeApp {
public:
    bool Init(HINSTANCE hInstance, const IdeLaunchOptions& options);
    int Run();
    void Shutdown();

private:
    bool CreateAppWindow(HINSTANCE hInstance);
    bool CreateD3D();
    void DestroyD3D();
    void CreateRenderTarget();
    void DestroyRenderTarget();
    void ResizeSwapChain(UINT width, UINT height);
    void RenderFrame();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ScintillaWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void ApplyTheme();
    void Render();
    void DrawTitleBar();
    void DrawActivityBar(float height);
    void DrawSidebar(float width, float height);
    void DrawTabs(float height);
    void DrawCommandBar();
    void DrawEditor(float width, float height);
    void DrawScintillaEditor(float width, float height);
    void DrawEditorSurface(float width, float height);
    void DrawStatusBar(float height);
    void DrawFindPanel();
    void DrawSettingsPanel();
    bool CreateScintillaEditor();
    void DestroyScintillaEditor();
    void ApplyScintillaTheme();
    void SetScintillaText(const std::string& text);
    std::string GetScintillaText() const;
    void StyleScintillaDocument();
    void UpdateScintillaLanguage();
    void PositionScintilla(ImVec2 screenPos, ImVec2 size);

    void HandleEditorInput(const std::vector<std::string>& lines);
    void DrawHighlightedLine(ImDrawList* drawList, ImVec2 pos, const std::string& line,
                             LanguageMode mode, float clipRight);
    void DrawCursor(ImDrawList* drawList, ImVec2 origin, const std::vector<std::string>& lines,
                    float charW, float lineH);

    bool OpenFile(const std::wstring& path);
    bool SaveFile();
    bool SaveFileAs();
    void NewDocument();
    void LoadLaunchDocument(const IdeLaunchOptions& options);
    void UpdateTitle();
    void SetStatus(std::string status);
    void CopyDocumentToClipboard();
    void CheckDroppedFile(HDROP drop);

    std::wstring OpenFileDialog();
    std::wstring SaveFileDialog();
    std::vector<std::string> SplitLines() const;
    size_t OffsetForLineColumn(const std::vector<std::string>& lines, int line, int column) const;
    void LineColumnForOffset(const std::vector<std::string>& lines, size_t offset,
                             int& line, int& column) const;

    HWND                    m_hwnd{};
    HINSTANCE               m_hInstance{};
    IDXGISwapChain*         m_swapChain{};
    ID3D11Device*           m_device{};
    ID3D11DeviceContext*    m_context{};
    ID3D11RenderTargetView* m_renderTarget{};
    bool                    m_swapChainOccluded{};
    UINT                    m_resizeW{};
    UINT                    m_resizeH{};
    HMODULE                 m_scintillaModule{};
    HWND                    m_scintillaHwnd{};
    WNDPROC                 m_scintillaWndProc{};
    bool                    m_scintillaReady{};
    bool                    m_scintillaTextDirty{};
    LanguageMode            m_scintillaStyledMode{LanguageMode::Text};
    RECT                    m_restoreRect{};
    bool                    m_maximized{};
    bool                    m_liveResize{};
    bool                    m_renderingFrame{};
    int                     m_chromeDragHit{};
    POINT                   m_chromeDragStart{};
    RECT                    m_chromeDragRect{};

    DocumentState m_doc;
    IdeLaunchOptions m_launchOptions;
    std::vector<std::wstring> m_recentFiles;
    std::string m_status{"Ready"};
    bool m_running{true};
    bool m_showCommandPalette{false};
    bool m_showFind{false};
    bool m_showSettings{false};
    bool m_editorFocused{false};
    bool m_showExplorer{true};
    bool m_showMinimap{true};
    bool m_wordWrap{false};
    bool m_insertSpaces{true};
    int  m_tabSize{4};
    float m_fontScale{1.0f};
    char m_commandBuf[256]{};
    char m_findBuf[256]{};
};
