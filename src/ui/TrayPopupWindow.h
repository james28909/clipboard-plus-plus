#pragma once

#include "Appearance.h"

#include <d3d11.h>
#include <windows.h>

struct ImGuiContext;

class TrayPopupWindow {
public:
    bool Create(HINSTANCE hInstance,
                ID3D11Device* device,
                ID3D11DeviceContext* context);
    void Destroy();

    void ShowAtCursor();
    void Hide();
    void Render();
    void ApplyAppearance(const AppearanceSettings& settings);

    bool IsVisible() const { return m_visible; }
    HWND GetHwnd() const { return m_hwnd; }

private:
    bool CreateSwapChain();
    void ResizeSwapChainToClient();
    void DestroySwapChain();
    void CreateRenderTarget();
    void DestroyRenderTarget();
    void ApplyWindowChrome();
    void PositionNearCursor();
    void CloseWhenClickedOutside();
    void DrawMenu();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND                    m_hwnd{};
    HINSTANCE               m_hInstance{};
    ID3D11Device*           m_device{};
    ID3D11DeviceContext*    m_context{};
    IDXGISwapChain*         m_swapChain{};
    ID3D11RenderTargetView* m_renderTarget{};
    ImGuiContext*           m_imguiCtx{};

    AppearanceSettings m_appearance{};
    bool m_visible{false};
    int  m_width{230};
    int  m_height{214};
};
