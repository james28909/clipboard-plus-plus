#pragma once
#include "../clipboard/ClipboardItem.h"
#include <windows.h>
#include <d3d11.h>
#include <vector>

struct ImGuiContext;

class PopupWindow {
public:
    // Must be called after the main D3D11 device is created.
    bool Create(HINSTANCE hInstance,
                ID3D11Device* device,
                ID3D11DeviceContext* context);
    void Destroy();

    void Show();
    void Hide();
    bool IsVisible() const { return m_visible; }

    // Called each frame from Application — renders the popup if visible.
    void Render();

    // Configurable
    float m_opacity{0.95f};
    int   m_width{440};
    int   m_height{540};
    int   m_queueDelayMs{50};

private:
    // ── D3D / window setup ────────────────────────────────────────────────────
    bool CreateSwapChain();
    void DestroySwapChain();
    void CreateRenderTarget();
    void DestroyRenderTarget();
    void PositionAtCursor();
    void ApplyOpacity();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    // ── ImGui UI ──────────────────────────────────────────────────────────────
    void DrawFilterStrip();
    void DrawItemList();
    bool ItemPassesFilter(const ClipboardItem& item) const;

    // ── Paste ─────────────────────────────────────────────────────────────────
    void PasteItem(const ClipboardItem& item);
    void PasteQueue();
    static void WriteToClipboard(const ClipboardItem& item);
    void RestoreFocusAndPaste();

    // ── Win32 + D3D11 ─────────────────────────────────────────────────────────
    HWND                    m_hwnd{};
    HINSTANCE               m_hInstance{};
    ID3D11Device*           m_device{};       // borrowed — not owned
    ID3D11DeviceContext*    m_context{};      // borrowed — not owned
    IDXGISwapChain*         m_swapChain{};
    ID3D11RenderTargetView* m_renderTarget{};

    // ── ImGui ─────────────────────────────────────────────────────────────────
    ImGuiContext* m_imguiCtx{};

    // ── State ─────────────────────────────────────────────────────────────────
    bool  m_visible{false};
    bool  m_justOpened{false};
    bool  m_queueMode{false};
    int   m_filterMode{0};      // 0=All 1=Text 2=Image 3=URL
    char  m_searchBuf[256]{};
    HWND  m_prevForeground{};
    std::vector<size_t> m_queue;
};
