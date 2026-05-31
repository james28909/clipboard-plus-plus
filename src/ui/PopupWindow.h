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

    void Show(bool focusSearch = false);
    void Hide();
    bool IsVisible() const { return m_visible; }
    bool IsSearchActive() const { return m_searchActive || m_focusSearchOnOpen; }
    HWND GetHwnd()   const { return m_hwnd; }

    // Called each frame from Application — renders the popup if visible.
    void Render();

    // Paste a specific item without opening the popup (used by hotkey direct-paste).
    // targetWindow is the HWND that should receive the Ctrl+V after we write to clipboard.
    void PasteDirect(const ClipboardItem& item, HWND targetWindow);
    void PasteVisibleSlot(int slot);
    void RequestSearchFocus();

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
    std::vector<size_t> BuildVisibleHistoryIndices() const;

    // ── Paste ─────────────────────────────────────────────────────────────────
    void PasteItemKeepOpen(const ClipboardItem& item);
    void PasteQueue();
    void WriteToClipboard(const ClipboardItem& item) const;
    HWND ResolvePasteTarget() const;
    bool WaitForForeground(HWND target, DWORD timeoutMs) const;
    void RestoreFocusAndPaste(HWND preferredTarget = nullptr);

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
    bool  m_focusSearchOnOpen{false};
    bool  m_searchActive{false};
    bool  m_queueMode{false};
    bool  m_appendNewlineAfterPaste{false};
    int   m_filterMode{0};      // 0=All 1=Text 2=Image 3=URL
    char  m_searchBuf[256]{};
    HWND  m_prevForeground{};
    std::vector<size_t> m_queue;
};
