#include "PopupWindow.h"
#include "../app/Application.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ContentDetector.h"
#include <imgui.h>
#include <imgui_internal.h>   // ImGuiWindow, GetCurrentWindow()
#include <algorithm>
#include <cstring>

static constexpr char kPopupWinId[] = "##cpppopup";

// ── Show / Hide ───────────────────────────────────────────────────────────────

void PopupWindow::Show() {
    // Capture the window that was focused before we steal it
    m_prevForeground = GetForegroundWindow();

    // Calculate spawn position at cursor, clamped to monitor work area
    POINT pt{};
    GetCursorPos(&pt);
    m_spawnX = static_cast<float>(pt.x);
    m_spawnY = static_cast<float>(pt.y);

    HMONITOR    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(mon, &mi)) {
        float rRight  = static_cast<float>(mi.rcWork.right);
        float rBottom = static_cast<float>(mi.rcWork.bottom);
        float rLeft   = static_cast<float>(mi.rcWork.left);
        float rTop    = static_cast<float>(mi.rcWork.top);
        if (m_spawnX + m_width  > rRight)  m_spawnX = rRight  - m_width;
        if (m_spawnY + m_height > rBottom) m_spawnY = rBottom - m_height;
        if (m_spawnX < rLeft) m_spawnX = rLeft;
        if (m_spawnY < rTop)  m_spawnY = rTop;
    }

    m_visible     = true;
    m_justOpened  = true;
    m_alphaSet    = false;
    m_queueMode   = false;
    m_queue.clear();
    std::memset(m_searchBuf, 0, sizeof(m_searchBuf));
}

void PopupWindow::Hide() {
    m_visible   = false;
    m_queueMode = false;
    m_queue.clear();
    m_alphaSet  = false;
}

// ── Main draw ─────────────────────────────────────────────────────────────────

void PopupWindow::Draw() {
    if (!m_visible) return;

    // Position at spawn point on first frame only
    if (m_justOpened) {
        ImGui::SetNextWindowPos({m_spawnX, m_spawnY}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({m_width, m_height}, ImGuiCond_Always);
    }

    // Force into its own always-on-top OS window via the viewport system
    ImGuiWindowClass wc{};
    wc.ViewportFlagsOverrideSet =
        ImGuiViewportFlags_TopMost        |
        ImGuiViewportFlags_NoAutoMerge    |
        ImGuiViewportFlags_NoDecoration   |
        ImGuiViewportFlags_NoTaskBarIcon;
    wc.ParentViewportId = 0; // detach from main window
    ImGui::SetNextWindowClass(&wc);
    ImGui::SetNextWindowBgAlpha(m_opacity);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    bool open = true;
    if (!ImGui::Begin(kPopupWinId, &open, flags)) {
        ImGui::End();
        return;
    }

    // Apply OS-level transparency once the viewport's platform window exists
    if (!m_alphaSet) {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        if (win && win->Viewport && ImGui::GetPlatformIO().Platform_SetWindowAlpha) {
            ImGui::GetPlatformIO().Platform_SetWindowAlpha(win->Viewport, m_opacity);
            m_alphaSet = true;
        }
    }

    m_justOpened = false;

    // Close on Escape
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        Hide(); ImGui::End(); return;
    }

    if (!open) { Hide(); ImGui::End(); return; }

    // ── Search bar ────────────────────────────────────────────────────────────
    float winW = ImGui::GetWindowWidth();
    ImGui::SetNextItemWidth(winW - 16.0f);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##search", "  Search...",
                              m_searchBuf, sizeof(m_searchBuf));

    ImGui::Spacing();
    DrawFilterStrip();
    ImGui::Separator();
    DrawItemList();

    ImGui::End();
}

// ── Filter strip ──────────────────────────────────────────────────────────────

void PopupWindow::DrawFilterStrip() {
    struct FilterBtn { const char* label; int mode; };
    static constexpr FilterBtn kFilters[] = {
        {"All", 0}, {"Text", 1}, {"Image", 2}, {"URL", 3}
    };

    for (const auto& f : kFilters) {
        bool active = (m_filterMode == f.mode);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(f.label)) m_filterMode = f.mode;
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    // Queue mode toggle
    bool qActive = m_queueMode;
    if (qActive)
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::SmallButton("Queue")) {
        m_queueMode = !m_queueMode;
        m_queue.clear();
    }
    if (qActive) ImGui::PopStyleColor();
    ImGui::SameLine();

    // Queue paste button — only visible when items are queued
    if (m_queueMode && !m_queue.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::SmallButton("Paste All")) PasteQueue();
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    // Gear → open settings
    if (ImGui::SmallButton(" @ ")) {
        Application::Get()->ShowMainWindow();
        Hide();
    }
    ImGui::Spacing();
}

// ── Item list ─────────────────────────────────────────────────────────────────

bool PopupWindow::ItemPassesFilter(const ClipboardItem& item) const {
    switch (m_filterMode) {
    case 1: return item.IsText();
    case 2: return item.IsImage();
    case 3: return (item.tags & TAG_URL) != 0;
    default: return true;
    }
}

void PopupWindow::DrawItemList() {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    const std::string query(m_searchBuf);
    const bool searching = !query.empty();

    // Lower-case query once for reuse
    std::string lquery = query;
    std::transform(lquery.begin(), lquery.end(), lquery.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });

    ImGui::BeginChild("##popupitems", {0.0f, 0.0f}, ImGuiChildFlags_None);

    size_t slot = 0;
    bool   anyVisible = false;

    for (size_t i = 0; i < hist->Size() && slot < 35; ++i) {
        const ClipboardItem* item = hist->Get(i);
        if (!item || !ItemPassesFilter(*item)) continue;

        // Search filter
        if (searching) {
            std::string lt = item->text;
            std::transform(lt.begin(), lt.end(), lt.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });
            if (lt.find(lquery) == std::string::npos) continue;
        }

        anyVisible = true;

        // Slot key label: 1-9 then a-z
        char key[2]{};
        key[0] = slot < 9 ? (char)('1' + (int)slot)
                           : (char)('a' + (int)(slot - 9));
        slot++;

        // Queue position badge
        int qpos = -1;
        for (size_t q = 0; q < m_queue.size(); ++q)
            if (m_queue[q] == i) { qpos = (int)q + 1; break; }

        // Secret items get a red tint
        const bool isSecret = (item->tags & TAG_SECRET) != 0;
        if (isSecret)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.42f, 0.42f, 1.0f));

        // Build display string
        char label[320]{};
        std::string preview = item->Preview(64);
        if (qpos >= 0)
            snprintf(label, sizeof(label), " %s [%d]  %s##r%zu",
                     key, qpos, preview.c_str(), i);
        else
            snprintf(label, sizeof(label), " %s   %s##r%zu",
                     key, preview.c_str(), i);

        bool rowSelected = (qpos >= 0);
        if (ImGui::Selectable(label, rowSelected,
                               ImGuiSelectableFlags_SpanAllColumns))
        {
            if (m_queueMode) {
                if (qpos >= 0) {
                    // Remove from queue
                    m_queue.erase(std::remove(m_queue.begin(), m_queue.end(), i),
                                   m_queue.end());
                } else {
                    m_queue.push_back(i);
                }
            } else {
                // Standard mode: paste immediately
                if (isSecret) ImGui::PopStyleColor();
                PasteItem(*item);
                ImGui::EndChild();
                ImGui::End();
                return;
            }
        }

        if (isSecret) ImGui::PopStyleColor();
    }

    if (!anyVisible)
        ImGui::TextDisabled("  No items match.");

    ImGui::EndChild();
}

// ── Paste ─────────────────────────────────────────────────────────────────────

void PopupWindow::PasteItem(const ClipboardItem& item) {
    WriteToClipboard(item);
    Hide();
    RestoreFocusAndPaste();
}

void PopupWindow::PasteQueue() {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    // Take a snapshot of indices before Hide() clears the queue
    std::vector<size_t> indices = m_queue;
    Hide();

    for (size_t qi = 0; qi < indices.size(); ++qi) {
        const ClipboardItem* item = hist->Get(indices[qi]);
        if (!item) continue;
        WriteToClipboard(*item);
        RestoreFocusAndPaste();
        if (qi + 1 < indices.size() && m_queueDelayMs > 0)
            Sleep(static_cast<DWORD>(m_queueDelayMs));
    }
}

void PopupWindow::WriteToClipboard(const ClipboardItem& item) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();

    if (item.type == ContentType::Image && !item.imageData.empty()) {
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, item.imageData.size());
        if (hMem) {
            void* p = GlobalLock(hMem);
            std::memcpy(p, item.imageData.data(), item.imageData.size());
            GlobalUnlock(hMem);
            SetClipboardData(CF_DIB, hMem);
        }
    } else {
        int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                        item.text.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE,
                                        static_cast<SIZE_T>(wlen) * sizeof(wchar_t));
            if (hMem) {
                auto* p = static_cast<wchar_t*>(GlobalLock(hMem));
                MultiByteToWideChar(CP_UTF8, 0, item.text.c_str(), -1, p, wlen);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
    }

    CloseClipboard();
}

void PopupWindow::RestoreFocusAndPaste() {
    if (m_prevForeground && IsWindow(m_prevForeground)) {
        SetForegroundWindow(m_prevForeground);
        Sleep(25); // let the OS complete focus transfer
    }

    INPUT in[4]{};
    in[0].type     = INPUT_KEYBOARD;
    in[0].ki.wVk   = VK_CONTROL;
    in[1].type     = INPUT_KEYBOARD;
    in[1].ki.wVk   = 'V';
    in[2]          = in[1];
    in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3]          = in[0];
    in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}
