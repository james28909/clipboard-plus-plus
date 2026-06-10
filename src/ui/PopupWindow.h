#pragma once
#include "../clipboard/ClipboardItem.h"
#include "../clipboard/ClipboardHistory.h"
#include "Appearance.h"
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <vector>

struct ImGuiContext;
struct AppearanceSettings;

class PopupWindow {
public:
    // Must be called after the main D3D11 device is created.
    bool Create(HINSTANCE hInstance,
                ID3D11Device* device,
                ID3D11DeviceContext* context);
    void Destroy();

    void Show(bool focusSearch = false);
    void Hide();
    void OpenSettingsWindow();
    bool IsVisible() const { return m_visible; }
    bool IsSearchActive() const { return m_searchActive || m_searchCapture || m_focusSearchOnOpen; }
    bool IsTextEntryActive() const { return IsSearchActive() || m_dialogTextCapture; }
    bool IsKeyboardCaptureActive() const { return m_keyboardCapture || IsTextEntryActive(); }
    HWND GetHwnd()   const { return m_hwnd; }
    SIZE GetCurrentSize() const;

    // Called each frame from Application - renders the popup if visible.
    void Render();
    void ApplyAppearance(const AppearanceSettings& settings);

    void PasteHistorySlot(int slot, HWND targetWindow);
    void PastePinnedSlot(int slot, HWND targetWindow);
    void PasteVisibleSlot(int slot);
    void RequestSearchFocus();
    void LaunchWebSearch();
    void LaunchClipboardWebSearch();
    void ActivateKeyboardCapture();
    void ReleaseSearchCapture();
    void NoteExternalMouseDown(POINT screenPoint);
    bool GetAppendNewlineAfterPaste() const { return m_appendNewlineAfterPaste; }
    void SetAppendNewlineAfterPaste(bool value) { m_appendNewlineAfterPaste = value; }
    ClipboardHistory::MoveTarget GetPasteMoveTarget() const { return m_pasteMoveTarget; }
    void SetPasteMoveTarget(ClipboardHistory::MoveTarget target) { m_pasteMoveTarget = target; }
    void SetTheme(ThemeId theme) { m_appearance.theme = theme; }

    // Configurable
    float m_opacity{0.95f};
    float m_outlineStrength{0.65f};
    int   m_width{440};
    int   m_height{540};
    int   m_queueDelayMs{50};

private:
    // -- D3D / window setup ----------------------------------------------------
    bool CreateSwapChain();
    void ResizeSwapChainToClient();
    void DestroySwapChain();
    void CreateRenderTarget();
    void DestroyRenderTarget();
    void PositionAtCursor();
    void ApplyOpacity();
    void ApplyWindowCorners();
    void ToggleMaximized();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    // -- ImGui UI --------------------------------------------------------------
    void DrawFilterStrip();
    void DrawSearchBar();
    void DrawItemList();
    bool ItemPassesFilter(const ClipboardItem& item) const;
    std::vector<size_t> BuildVisibleHistoryIndices(bool pinnedOnly) const;
    void DrawItemDragDrop(uint64_t itemId, int qpos);
    bool DrawItemContextMenu(const ClipboardItem& item, int qpos);
    void DrawTitleBar();

    // -- Paste -----------------------------------------------------------------
    void PasteItemKeepOpen(const ClipboardItem& item);
    void PasteQueue();
    void WriteToClipboard(const ClipboardItem& item) const;
    HWND ResolvePasteTarget() const;
    bool WaitForForeground(HWND target, DWORD timeoutMs) const;
    void RestoreFocusAndPaste(HWND preferredTarget = nullptr);
    bool LaunchWebSearchForText(const std::string& query, bool hideOnSuccess);

    // -- Win32 + D3D11 ---------------------------------------------------------
    HWND                    m_hwnd{};
    HINSTANCE               m_hInstance{};
    ID3D11Device*           m_device{};       // borrowed - not owned
    ID3D11DeviceContext*    m_context{};      // borrowed - not owned
    IDXGISwapChain*         m_swapChain{};
    ID3D11RenderTargetView* m_renderTarget{};

    // -- ImGui -----------------------------------------------------------------
    ImGuiContext* m_imguiCtx{};

    // -- State -----------------------------------------------------------------
    bool  m_visible{false};
    bool  m_justOpened{false};
    bool  m_focusSearchOnOpen{false};
    bool  m_searchActive{false};
    bool  m_searchCapture{false};
    bool  m_dialogTextCapture{false};
    bool  m_keyboardCapture{false};
    bool  m_maximized{false};
    RECT  m_restoreRect{};
    bool  m_queueMode{false};
    bool  m_appendNewlineAfterPaste{false};
    ClipboardHistory::MoveTarget m_pasteMoveTarget{ClipboardHistory::MoveTarget::None};
    AppearanceSettings m_appearance{};
    int   m_filterMode{0};      // 0=All 1=Text 2=Image 3=URL
    char  m_searchBuf[256]{};
    char  m_clipboardName[128]{};
    char  m_searchDebug[256]{};
    bool  m_lastSearchActive{false};
    bool  m_lastSearchInputActive{false};
    bool  m_lastAnyItemActive{false};
    bool  m_lastWantTextInput{false};
    size_t m_lastSearchLen{};
    HWND  m_prevForeground{};
    std::string m_lastClipboardId;
    std::vector<uint64_t> m_queue;
    std::vector<uint64_t> m_dragIds;
};
