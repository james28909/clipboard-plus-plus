#pragma once

#include "Appearance.h"
#include "../app/ConfigStore.h"

#include <d3d11.h>
#include <windows.h>

#include <filesystem>
#include <string>

struct ImGuiContext;

class TextEditorWindow {
public:
    bool Create(HINSTANCE hInstance,
                ID3D11Device* device,
                ID3D11DeviceContext* context);
    void Destroy();

    void Show();
    void Hide();
    void Render();
    void ApplyAppearance(const AppearanceSettings& settings);
    void ApplySettings(const EditorSettings& settings);

    bool IsVisible() const { return m_visible; }
    HWND GetHwnd() const { return m_hwnd; }
    SIZE GetCurrentSize() const;

private:
    bool CreateSwapChain();
    void ResizeSwapChainToClient();
    void DestroySwapChain();
    void CreateRenderTarget();
    void DestroyRenderTarget();
    void ApplyWindowChrome();
    void ApplyWindowRegion();
    void InvalidateWindowRegion();
    void PositionInitial();
    void ToggleMaximized();
    void DrawTitleBar();
    void DrawToolbar();
    void DrawEditor();
    void DrawStatusBar();
    void ClearDocument();
    void LoadClipboardText();
    void CopyTextToClipboard();
    bool SaveAs();
    bool SaveToPath(const std::filesystem::path& path);
    void OpenSaveDialog();
    void ConfirmClose();

    static int TextResizeCallback(struct ImGuiInputTextCallbackData* data);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND                    m_hwnd{};
    HINSTANCE               m_hInstance{};
    ID3D11Device*           m_device{};
    ID3D11DeviceContext*    m_context{};
    IDXGISwapChain*         m_swapChain{};
    ID3D11RenderTargetView* m_renderTarget{};
    ImGuiContext*           m_imguiCtx{};

    AppearanceSettings m_appearance{};
    EditorSettings m_settings{};
    bool m_visible{false};
    bool m_focusEditorOnOpen{false};
    bool m_dirty{false};
    bool m_pendingClose{false};
    bool m_maximized{false};
    bool m_regionCacheValid{false};
    int  m_lastRegionWidth{-1};
    int  m_lastRegionHeight{-1};
    int  m_lastRegionRadius{-1};
    RECT m_restoreRect{};
    int  m_width{760};
    int  m_height{520};
    std::string m_text;
    std::filesystem::path m_path;
    std::string m_status;
};
