#pragma once
#include "../clipboard/ClipboardItem.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ImageStore.h"
#include "Appearance.h"
#include "PopupSelectionModel.h"
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <vector>

struct ImGuiContext;
struct AppearanceSettings;
struct RegexTransformDefinition;
struct PasteTemplateDefinition;
enum class StructuredFormat;

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
    void PasteNamedSlot(int64_t slotId, HWND targetWindow = nullptr);
    void PasteVisibleSlot(int slot);
    void PasteSelectedItems();
    void ClearSelectedItems();
    void RequestSearchFocus();
    void LaunchWebSearch();
    void LaunchClipboardWebSearch();
    void ActivateKeyboardCapture();
    void ReleaseSearchCapture();
    void NoteExternalMouseDown(POINT screenPoint);
    bool GetAppendNewlineAfterPaste() const { return m_appendNewlineAfterPaste; }
    void SetAppendNewlineAfterPaste(bool value) { m_appendNewlineAfterPaste = value; }
    bool HasMultipleSelectedItems() const { return m_itemSelection.HasMultiple(); }
    ClipboardHistory::MoveTarget GetPasteMoveTarget() const { return m_pasteMoveTarget; }
    void SetPasteMoveTarget(ClipboardHistory::MoveTarget target) { m_pasteMoveTarget = target; }
    void SetTheme(ThemeId theme) { m_appearance.theme = theme; }

    // Configurable
    float m_opacity{0.95f};
    float m_outlineStrength{0.65f};
    int   m_width{440};
    int   m_height{540};
    int   m_multiPasteDelayMs{50};
    bool  m_focusTestMode{false}; // test: let popup activate then restore focus to prev foreground

private:
    // -- D3D / window setup ----------------------------------------------------
    bool CreateSwapChain();
    void ResizeSwapChainToClient();
    void DestroySwapChain();
    void CreateRenderTarget();
    void DestroyRenderTarget();
    void PositionAtCursor();
    void ApplyOpacity();
    void ApplyDwmFrameSettings();
    void ApplyWindowRegion();
    void InvalidateWindowRegion();
    void ToggleMaximized();
    void StartPasteTargetTracking();
    void StopPasteTargetTracking();
    bool IsValidPasteTarget(HWND hwnd) const;
    void NotePasteTarget(HWND hwnd, const char* reason);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK ForegroundWinEventProc(HWINEVENTHOOK hook, DWORD event,
                                                HWND hwnd, LONG objectId,
                                                LONG childId, DWORD eventThread,
                                                DWORD eventTime);

    // -- ImGui UI --------------------------------------------------------------
    void DrawFilterStrip();
    void DrawSearchBar();
    void DrawItemList();
    void DrawAndroidPanel();
    void DrawImageBrowser();
    void DrawNamedSlots();
    bool ItemPassesFilter(const ClipboardItem& item) const;
    void InvalidateVisibleHistoryCache() const;
    void EnsureVisibleHistoryCache() const;
    std::vector<size_t> BuildVisibleHistoryIndices(bool pinnedOnly) const;
    std::vector<uint64_t> BuildVisibleItemIds() const;
    bool IsItemSelected(uint64_t itemId) const;
    std::vector<uint64_t> ContextSelectionFor(uint64_t itemId) const;
    void ClearItemSelection();
    void SelectOnlyItem(uint64_t itemId);
    void ToggleItemSelection(uint64_t itemId);
    void SelectRangeTo(uint64_t itemId);
    void DrawItemDragDrop(uint64_t itemId);
    bool DrawItemContextMenu(const ClipboardItem& item);
    void DrawTitleBar();
    void ClearThumbCache();

    // -- Paste -----------------------------------------------------------------
    void PasteItemKeepOpen(const ClipboardItem& item);
    void PasteItemWithTransformKeepOpen(
        const ClipboardItem& item, const RegexTransformDefinition& transform);
    bool PasteSelectionWithTemplateKeepOpen(
        const std::vector<uint64_t>& itemIds,
        const PasteTemplateDefinition& pasteTemplate);
    bool PasteItemFormattedKeepOpen(const ClipboardItem& item,
                                    StructuredFormat format);
    void PasteItemAsFormatKeepOpen(const ClipboardItem& item,
                                   const ClipboardFormatRecord& format);
    void PasteSelectedItemsInOrder();
    void WriteToClipboard(const ClipboardItem& item, HWND targetWindow = nullptr) const;
    bool WriteFormatToClipboard(const ClipboardFormatRecord& format) const;
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
    bool  m_clipboardDropdownOpen{false};
    bool  m_openDeleteConfirm{false};
    std::string m_pendingDeleteProfileId;
    std::string m_pendingDeleteProfileName;
    bool  m_openProfileContextMenu{false};
    float m_contextMenuX{0.0f};
    float m_contextMenuY{0.0f};
    std::string m_contextMenuProfileId;
    std::string m_contextMenuProfileName;
    bool  m_keyboardCapture{false};
    bool  m_maximized{false};
    RECT  m_restoreRect{};
    bool  m_regionCacheValid{false};
    int   m_lastRegionWidth{-1};
    int   m_lastRegionHeight{-1};
    int   m_lastRegionRadius{-1};
    bool  m_lastRegionMaximized{false};
    bool  m_androidPanelOpen{false};
    bool  m_namedSlotsPanelOpen{false};
    bool  m_appendNewlineAfterPaste{false};
    ClipboardHistory::MoveTarget m_pasteMoveTarget{ClipboardHistory::MoveTarget::None};
    AppearanceSettings m_appearance{};
    int   m_filterMode{0};      // 0=All 1=Text 2=Image 3=URL
    std::string m_activeCustomFilterId;
    char  m_searchBuf[256]{};
    char  m_clipboardName[128]{};
    char  m_searchDebug[256]{};
    bool  m_lastSearchActive{false};
    bool  m_lastSearchInputActive{false};
    bool  m_lastAnyItemActive{false};
    bool  m_lastWantTextInput{false};
    size_t m_lastSearchLen{};
    HWND  m_prevForeground{};
    HWND  m_activePasteTarget{};
    HWINEVENTHOOK m_foregroundHook{};
    std::string m_lastClipboardId;
    std::vector<uint64_t> m_dragIds;
    PopupSelectionModel m_itemSelection;
    char m_androidEndpointBuf[256]{};
    bool m_androidEndpointEditing{false};
    bool m_lastAndroidPanelOpen{false};
    std::string m_androidSyncStatus;
    mutable bool m_visibleHistoryCacheValid{false};
    mutable const ClipboardHistory* m_visibleHistoryCacheHistory{};
    mutable uint64_t m_visibleHistoryCacheVersion{};
    mutable int m_visibleHistoryCacheFilterMode{-1};
    mutable std::string m_visibleHistoryCacheCustomFilterId;
    mutable std::string m_visibleHistoryCacheSearch;
    mutable std::vector<size_t> m_visiblePinnedIndices;
    mutable std::vector<size_t> m_visibleRegularIndices;
    mutable std::vector<uint64_t> m_visibleItemIds;

    // -- Image browser ---------------------------------------------------------
    struct ThumbEntry {
        ID3D11ShaderResourceView* srv{};
        int w{};
        int h{};
    };
    std::unordered_map<std::string, ThumbEntry> m_thumbCache;
    std::vector<ImageRecord> m_cachedImageList;
    bool m_imageListDirty{true};
    int  m_imgSort{0};         // 0=newest  1=oldest  2=largest  3=smallest
    int  m_imgDateFilter{0};   // 0=all  1=today  2=week  3=month
    int  m_imgSizeFilter{0};   // 0=any  1=>100KB  2=>500KB  3=>1MB
    std::string m_imgCtxMenuId;
    std::string m_imgSelectionAnchorId;
    std::vector<std::string> m_selectedImageIds;
};
