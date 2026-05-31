#pragma once
#include "../clipboard/ClipboardItem.h"
#include <windows.h>
#include <vector>

class PopupWindow {
public:
    // Called each frame — renders nothing when not visible
    void Draw();

    void Show();
    void Hide();
    bool IsVisible() const { return m_visible; }

    // Configurable — will be driven by config/theme in later milestones
    float m_opacity{0.95f};
    float m_width{440.0f};
    float m_height{540.0f};
    int   m_queueDelayMs{50};

private:
    // ── Paste helpers ─────────────────────────────────────────────────────────
    void PasteItem(const ClipboardItem& item);
    void PasteQueue();
    static void WriteToClipboard(const ClipboardItem& item);
    void RestoreFocusAndPaste();

    // ── Rendering helpers ─────────────────────────────────────────────────────
    void DrawFilterStrip();
    void DrawItemList();
    bool ItemPassesFilter(const ClipboardItem& item) const;

    // ── State ─────────────────────────────────────────────────────────────────
    bool  m_visible{false};
    bool  m_justOpened{false};
    bool  m_queueMode{false};
    bool  m_alphaSet{false};    // transparency applied to viewport yet?
    int   m_filterMode{0};      // 0=All 1=Text 2=Image 3=URL
    float m_spawnX{}, m_spawnY{};
    char  m_searchBuf[256]{};
    HWND  m_prevForeground{};
    std::vector<size_t> m_queue; // history indices in selection order
};
