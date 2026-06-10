#pragma once

#include "Appearance.h"

#include <d3d11.h>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

struct ImGuiContext;

class DebugWindow {
public:
    bool Create(HINSTANCE hInstance,
                ID3D11Device* device,
                ID3D11DeviceContext* context);
    void Destroy();

    void Show();
    void Hide();
    void Toggle();
    void Render();
    void ApplyAppearance(const AppearanceSettings& settings);

    bool IsVisible() const { return m_visible; }
    void AddLine(const std::string& line);
    void Clear();

private:
    bool CreateSwapChain();
    void ResizeSwapChainToClient();
    void DestroySwapChain();
    void CreateRenderTarget();
    void DestroyRenderTarget();
    void PositionInitial();
    void DrawContents();

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
    bool m_followTail{true};
    bool m_positioned{false};
    bool m_textDirty{true};
    int  m_width{760};
    int  m_height{460};

    std::mutex m_mutex;
    std::string m_text;
    std::vector<char> m_textBuffer;
};
