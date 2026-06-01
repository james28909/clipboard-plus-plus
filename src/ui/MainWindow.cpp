#include "MainWindow.h"
#include "../app/Application.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ContentDetector.h"
#include "Appearance.h"
#include "PopupWindow.h"
#include <imgui.h>
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

// ── Section nav ───────────────────────────────────────────────────────────────

enum Section {
    SEC_GENERAL    = 0,
    SEC_HOTKEYS    = 1,
    SEC_APPEARANCE = 2,
    SEC_HISTORY    = 3,
    SEC_PRIVACY    = 4,
    SEC_DEVELOPER  = 5,
    SEC_ABOUT      = 6,
    SEC_COUNT
};

static const char* kSectionLabels[SEC_COUNT] = {
    "General", "Hotkeys", "Appearance", "History",
    "Privacy", "Developer", "About",
};

static int s_activeSection = SEC_GENERAL;

static bool PickFontFile(char* path, DWORD pathSize) {
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = Application::Get() ? Application::Get()->GetHwnd() : nullptr;
    ofn.lpstrFile = path;
    ofn.nMaxFile = pathSize;
    ofn.lpstrFilter = "Font Files (*.ttf;*.otf)\0*.ttf;*.otf\0All Files (*.*)\0*.*\0";
    ofn.lpstrTitle = "Choose font";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) != FALSE;
}

static bool BindingHasConflict(const HotkeySettings& settings, size_t index) {
    if (index >= settings.bindings.size()) return false;
    const KeyBinding& a = settings.bindings[index];
    if (a.vkey == 0) return false;

    for (size_t i = 0; i < settings.bindings.size(); ++i) {
        if (i == index) continue;
        const KeyBinding& b = settings.bindings[i];
        if (a.ctrl == b.ctrl && a.shift == b.shift &&
            a.alt == b.alt && a.vkey == b.vkey) {
            return true;
        }
    }
    return false;
}

// ── Title bar helpers ─────────────────────────────────────────────────────────

// Draws a single title bar button. Handles hover highlight and click detection.
// Returns true on click. The caller draws the icon on top via ImDrawList.
static bool TitleBtn(const char* id, float x, float w, float h, bool isClose) {
    ImGui::SetCursorPos(ImVec2(x, 0.0f));
    ImGui::InvisibleButton(id, ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();

    if (hovered) {
        ImVec2 wp  = ImGui::GetWindowPos();
        ImU32  col = isClose ? IM_COL32(196, 43, 28, 255)
                             : ImGui::GetColorU32(ImGuiCol_ButtonHovered);
        ImGui::GetWindowDrawList()->AddRectFilled(
            {wp.x + x,     wp.y},
            {wp.x + x + w, wp.y + h}, col);
    }
    return clicked;
}

// ── Title bar ─────────────────────────────────────────────────────────────────

void MainWindow::DrawTitleBar() {
    const float W = ImGui::GetWindowWidth();
    const float H = (float)kTitleBarHeight;
    const float BW = (float)kTitleBtnWidth;

    ImVec2      wp = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    HWND        wnd = Application::Get()->GetHwnd();

    // ── Background ───────────────────────────────────────────────────────────
    const ImU32 titleBg = ImGui::GetColorU32(ImGuiCol_WindowBg);
    const ImU32 titleLine = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 titleText = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 titleMuted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    dl->AddRectFilled(wp, {wp.x + W, wp.y + H}, titleBg);

    // ── App title text (left-aligned, vertically centred) ────────────────────
    float textY = (H - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorPos({12.0f, textY});
    ImGui::PushStyleColor(ImGuiCol_Text, titleText);
    ImGui::TextUnformatted("Clipboard++");
    ImGui::PopStyleColor();

    // ── Buttons: minimize | maximize/restore | close ──────────────────────────
    const bool isMax = IsZoomed(wnd) != 0;
    float bx = W - BW * 3.0f;

    // — Minimize —
    if (TitleBtn("##min", bx, BW, H, false)) {
        ShowWindow(wnd, SW_MINIMIZE);
        Application::Get()->HideMainWindow();
    }
    {   // icon: horizontal bar centred in the button
        ImVec2 c = {wp.x + bx + BW * 0.5f, wp.y + H * 0.5f + 1.0f};
        dl->AddLine({c.x - 5.0f, c.y}, {c.x + 5.0f, c.y},
                    titleMuted, 1.5f);
    }
    bx += BW;

    // — Maximize / Restore —
    if (TitleBtn("##max", bx, BW, H, false))
        PostMessageW(wnd, WM_SYSCOMMAND, isMax ? SC_RESTORE : SC_MAXIMIZE, 0);
    {
        ImVec2 c = {wp.x + bx + BW * 0.5f, wp.y + H * 0.5f};
        if (isMax) {
            // Restore: two overlapping squares (back then front)
            dl->AddRect({c.x - 2.0f, c.y - 5.0f}, {c.x + 5.0f, c.y + 2.0f},
                        titleMuted, 0, 0, 1.2f);
            // Erase overlap area so back square doesn't bleed through front
            dl->AddRectFilled({c.x - 5.0f, c.y - 2.0f}, {c.x + 1.0f, c.y + 5.0f},
                              titleBg);
            dl->AddRect({c.x - 5.0f, c.y - 2.0f}, {c.x + 2.0f, c.y + 5.0f},
                        titleMuted, 0, 0, 1.2f);
        } else {
            // Maximize: single square
            dl->AddRect({c.x - 5.0f, c.y - 5.0f}, {c.x + 5.0f, c.y + 5.0f},
                        titleMuted, 0, 0, 1.2f);
        }
    }
    bx += BW;

    // — Close —
    if (TitleBtn("##close", bx, BW, H, true))
        PostMessageW(wnd, WM_CLOSE, 0, 0);
    {
        ImVec2 c    = {wp.x + bx + BW * 0.5f, wp.y + H * 0.5f};
        bool hov    = ImGui::IsMouseHoveringRect({wp.x + bx, wp.y},
                                                  {wp.x + bx + BW, wp.y + H});
        ImU32 xcol  = hov ? IM_COL32(255, 255, 255, 255) : titleMuted;
        dl->AddLine({c.x - 5.0f, c.y - 5.0f}, {c.x + 5.0f, c.y + 5.0f}, xcol, 1.5f);
        dl->AddLine({c.x + 5.0f, c.y - 5.0f}, {c.x - 5.0f, c.y + 5.0f}, xcol, 1.5f);
    }

    // ── Separator line ────────────────────────────────────────────────────────
    dl->AddLine({wp.x, wp.y + H}, {wp.x + W, wp.y + H},
                titleLine);

    // Advance ImGui cursor below the title bar
    ImGui::SetCursorPos({0.0f, H});
}

// ── Main draw ─────────────────────────────────────────────────────────────────

void MainWindow::Draw(bool& open) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin("##main", nullptr, flags)) {
        ImGui::End();
        return;
    }

    DrawTitleBar();

    // ── Sidebar + content layout below the title bar ──────────────────────────
    const float pad      = 16.0f;
    const float sidebarW = 168.0f;
    const float contentW = ImGui::GetContentRegionAvail().x - sidebarW - pad * 2.0f;

    ImGui::SetCursorPos({pad, (float)kTitleBarHeight + pad});

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.0f, 10.0f});
    ImGui::BeginChild("##sidebar", {sidebarW, 0}, ImGuiChildFlags_Borders);
    DrawSidebarNav(s_activeSection);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine(0, pad);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.0f, 12.0f});
    ImGui::BeginChild("##content", {contentW, 0}, ImGuiChildFlags_None);
    switch (s_activeSection) {
    case SEC_GENERAL:    DrawGeneral();    break;
    case SEC_HOTKEYS:    DrawHotkeys();    break;
    case SEC_APPEARANCE: DrawAppearance(); break;
    case SEC_HISTORY:    DrawHistory();    break;
    case SEC_PRIVACY:    DrawPrivacy();    break;
    case SEC_DEVELOPER:  DrawDeveloper();  break;
    case SEC_ABOUT:      DrawAbout();      break;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
}

// ── Sidebar nav ───────────────────────────────────────────────────────────────

void MainWindow::DrawSidebarNav(int& selected) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 2.0f});
    for (int i = 0; i < SEC_COUNT; ++i) {
        bool active = (i == selected);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(kSectionLabels[i], {-1.0f, 32.0f}))
            selected = i;
        if (active)
            ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
}

// ── Section: General ─────────────────────────────────────────────────────────

void MainWindow::DrawGeneral() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("General");
    ImGui::Separator();
    ImGui::Spacing();

    static bool startWithWindows = true;
    static bool deduplication    = true;

    ImGui::Checkbox("Start with Windows", &startWithWindows);
    ImGui::Spacing();
    bool newItemsAtTop = app->GetNewItemsAtTop();
    if (ImGui::Checkbox("New items added to top of list", &newItemsAtTop)) {
        app->SetNewItemsAtTop(newItemsAtTop);
    }
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When off, new items are added to the bottom.");
    ImGui::Spacing();
    ImGui::Checkbox("Deduplicate — move existing copy to configured position", &deduplication);

    ImGui::Spacing(); ImGui::Spacing();
    ImGui::TextDisabled("Full configuration wired in Milestone 5.");
}

// ── Section: Hotkeys ─────────────────────────────────────────────────────────

void MainWindow::DrawHotkeys() {
    Application* app = Application::Get();
    if (!app) return;
    HotkeyManager* hotkeys = app->GetHotkeys();
    if (!hotkeys) return;

    ImGui::TextDisabled("Hotkeys");
    ImGui::Separator();
    ImGui::Spacing();

    static HotkeySettings draft = app->GetHotkeySettings();
    static bool initialized = false;
    static int captureIndex = -1;
    if (!initialized) {
        draft = app->GetHotkeySettings();
        initialized = true;
    }

    KeyBinding captured;
    if (hotkeys->ConsumeCapturedBinding(captured) && captureIndex >= 0 &&
        static_cast<size_t>(captureIndex) < draft.bindings.size()) {
        captured.action = draft.bindings[static_cast<size_t>(captureIndex)].action;
        captured.data = draft.bindings[static_cast<size_t>(captureIndex)].data;
        draft.bindings[static_cast<size_t>(captureIndex)] = captured;
        app->RequestHotkeySettings(draft);
        captureIndex = -1;
    }

    if (ImGui::BeginTable("##hotkeys", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Binding",  ImGuiTableColumnFlags_WidthFixed, 240.0f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < draft.bindings.size(); ++i) {
            const KeyBinding& binding = draft.bindings[i];
            const bool conflict = BindingHasConflict(draft, i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(HotkeyManager::ActionName(binding.action));
            if (conflict) {
                ImGui::SameLine();
                ImGui::TextColored({1.0f, 0.45f, 0.25f, 1.0f}, "Conflict");
            }

            ImGui::TableSetColumnIndex(1);
            const std::string label = captureIndex == static_cast<int>(i)
                ? "Press keys...##capture" + std::to_string(i)
                : HotkeyManager::BindingText(binding) + "##capture" + std::to_string(i);
            if (ImGui::Button(label.c_str(), {160.0f, 0.0f})) {
                captureIndex = static_cast<int>(i);
                hotkeys->BeginCapture();
            }
            ImGui::SameLine();
            const std::string resetId = "Reset##resetHotkey" + std::to_string(i);
            if (ImGui::SmallButton(resetId.c_str())) {
                HotkeySettings defaults = HotkeyManager::DefaultSettings();
                auto it = std::find_if(defaults.bindings.begin(), defaults.bindings.end(),
                    [&](const KeyBinding& b) { return b.action == binding.action; });
                if (it != defaults.bindings.end()) {
                    draft.bindings[i] = *it;
                    app->RequestHotkeySettings(draft);
                }
            }
        }
        ImGui::EndTable();
    }

    if (captureIndex >= 0) {
        ImGui::TextDisabled("Press Esc to cancel capture.");
        if (!hotkeys->IsCapturing())
            captureIndex = -1;
    }

    ImGui::Spacing();
    ImGui::Text("Hidden paste slots");
    bool changed = false;
    changed |= ImGui::Checkbox("Ctrl##hiddenCtrl", &draft.hiddenPasteCtrl);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Shift##hiddenShift", &draft.hiddenPasteShift);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Alt##hiddenAlt", &draft.hiddenPasteAlt);
    changed |= ImGui::Checkbox("Allow F1-F12 hidden paste slots", &draft.hiddenPasteFunctionKeys);
    ImGui::TextDisabled("%s + 1-9/a-z%s",
        HotkeyManager::ModifiersText(draft.hiddenPasteCtrl,
                                     draft.hiddenPasteShift,
                                     draft.hiddenPasteAlt).c_str(),
        draft.hiddenPasteFunctionKeys ? "/F1-F12" : "");
    if (changed)
        app->RequestHotkeySettings(draft);

    ImGui::Spacing();
    if (PopupWindow* popup = Application::Get()->GetPopup()) {
        bool newline = app->GetAppendNewlineAfterPaste();
        if (ImGui::Checkbox("Append newline after paste", &newline))
            app->SetAppendNewlineAfterPaste(newline);

        int moveMode = 0;
        switch (app->GetPasteMoveTarget()) {
        case ClipboardHistory::MoveTarget::Top:    moveMode = 1; break;
        case ClipboardHistory::MoveTarget::Bottom: moveMode = 2; break;
        default:                                   moveMode = 0; break;
        }

        const char* modes[] = { "Keep item in place", "Move pasted item to top", "Move pasted item to bottom" };
        ImGui::Text("After paste");
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::Combo("##pasteMove", &moveMode, modes, IM_ARRAYSIZE(modes))) {
            ClipboardHistory::MoveTarget target = ClipboardHistory::MoveTarget::None;
            if (moveMode == 1) target = ClipboardHistory::MoveTarget::Top;
            if (moveMode == 2) target = ClipboardHistory::MoveTarget::Bottom;
            app->SetPasteMoveTarget(target);
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Hotkey changes apply immediately. Settings persistence is next.");
}

// ── Section: Appearance ──────────────────────────────────────────────────────

void MainWindow::DrawAppearance() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("Appearance");
    ImGui::Separator();
    ImGui::Spacing();

    static AppearanceSettings draft = app->GetAppearance();
    static char fontPath[512]{};
    static bool initialized = false;
    if (!initialized) {
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        initialized = true;
    }

    const char* themes[] = {
        "Dark Default", "Dracula", "Nord", "Monokai",
        "One Dark Pro", "Tokyo Night", "Solarized Dark", "GitHub Dark",
        "GitHub Light", "Solarized Light", "VS Light", "Quiet Light"
    };

    ImGui::Text("Theme");
    ImGui::SetNextItemWidth(240.0f);
    int themeIndex = static_cast<int>(draft.theme);
    if (ImGui::Combo("##theme", &themeIndex, themes, IM_ARRAYSIZE(themes))) {
        draft.theme = static_cast<ThemeId>(themeIndex);
        app->RequestAppearance(draft);
    }

    ImGui::Spacing();
    ImGui::Text("Popup opacity");
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::SliderFloat("##opacity", &draft.popupOpacity, 0.1f, 1.0f, "%.2f"))
        app->RequestAppearance(draft);

    ImGui::Spacing();
    ImGui::Text("Default popup size");
    bool sizeChanged = false;
    ImGui::SetNextItemWidth(100.0f);
    sizeChanged |= ImGui::InputInt("W##pw", &draft.popupWidth, 10);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    sizeChanged |= ImGui::InputInt("H##ph", &draft.popupHeight, 10);
    if (sizeChanged) {
        draft.popupWidth = std::max(360, draft.popupWidth);
        draft.popupHeight = std::max(260, draft.popupHeight);
        app->RequestAppearance(draft);
    }

    ImGui::Spacing();
    ImGui::Text("Font");
    ImGui::SetNextItemWidth(420.0f);
    bool fontEdited = false;
    fontEdited |= ImGui::InputText("##fontPath", fontPath, sizeof(fontPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
        if (PickFontFile(fontPath, sizeof(fontPath))) {
            fontEdited = true;
        }
    }

    ImGui::SetNextItemWidth(110.0f);
    fontEdited |= ImGui::InputFloat("Size##fontSize", &draft.fontSize, 0.5f, 1.0f, "%.1f");
    ImGui::SameLine();
    if (ImGui::Button("Apply Font")) {
        draft.fontPath = fontPath;
        draft.fontSize = std::clamp(draft.fontSize, 9.0f, 32.0f);
        app->RequestAppearance(draft);
    }
    if (fontEdited) {
        draft.fontPath = fontPath;
        draft.fontSize = std::clamp(draft.fontSize, 9.0f, 32.0f);
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Font changes apply to the main window and popup.");
}

// ── Section: History ─────────────────────────────────────────────────────────

void MainWindow::DrawHistory() {
    ImGui::TextDisabled("History");
    ImGui::Separator();
    ImGui::Spacing();

    static int  activeLimit    = 500;
    static bool persistHistory = true;
    static bool sessionOnly    = false;
    static bool vaultUnlimited = true;
    static int  vaultLimitMB   = 0;

    ImGui::Text("Active history size (items)");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("##active", &activeLimit, 1, 500);

    ImGui::Spacing();
    ImGui::Checkbox("Persist history across sessions", &persistHistory);
    if (persistHistory) {
        ImGui::Indent();
        ImGui::Checkbox("Session only (clear on exit)", &sessionOnly);
        ImGui::Unindent();
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Vault (overflow archive)");
    ImGui::Checkbox("Unlimited vault size", &vaultUnlimited);
    if (!vaultUnlimited) {
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Max vault size (MB)", &vaultLimitMB);
        if (vaultLimitMB < 1) vaultLimitMB = 1;
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Storage engine wired in Milestone 5.");

    // ── Live history preview ──────────────────────────────────────────────────
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ClipboardHistory* hist = Application::Get()->GetHistory();
    size_t count = hist ? hist->Size() : 0;
    ImGui::Text("Live history  (%zu / %d items)", count, activeLimit);
    ImGui::Spacing();

    if (ImGui::BeginChild("##histlive", {-1.0f, 220.0f}, ImGuiChildFlags_Borders)) {
        if (count == 0) {
            ImGui::TextDisabled("  Nothing captured yet — copy something!");
        } else {
            static const ContentTag kTagOrder[] = {
                TAG_SECRET, TAG_URL, TAG_EMAIL, TAG_JSON, TAG_XML,
                TAG_HEX, TAG_PATH, TAG_CODE
            };
            for (size_t i = 0; i < count && i < 35; ++i) {
                const ClipboardItem* item = hist->Get(i);
                if (!item) break;

                // Slot label: 1-9 then a-z
                char slot[4]{};
                if (i < 9)       slot[0] = (char)('1' + (int)i);
                else if (i < 35) slot[0] = (char)('a' + (int)(i - 9));
                ImGui::TextDisabled(" %s ", slot);
                ImGui::SameLine();

                // Inline tag badges
                for (ContentTag t : kTagOrder) {
                    if (!(item->tags & t)) continue;
                    ImVec4 col = (item->tags & TAG_SECRET)
                        ? ImVec4(1.f, 0.34f, 0.34f, 1.f)
                        : ImVec4(0.4f, 0.7f, 1.0f, 1.f);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::Text("[%s]", ContentDetector::TagName(t));
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                }

                ImGui::TextUnformatted(item->Preview(72).c_str());
            }
        }
    }
    ImGui::EndChild();
}

// ── Section: Privacy ─────────────────────────────────────────────────────────

void MainWindow::DrawPrivacy() {
    ImGui::TextDisabled("Privacy & Security");
    ImGui::Separator();
    ImGui::Spacing();

    static bool detectSecrets = true;
    static bool autoDiscard   = false;
    static bool clearOnLock   = false;
    static char exclusionBuf[512] = "KeePass.exe\n1Password.exe\nBitwarden.exe";

    ImGui::Checkbox("Detect secret patterns (API keys, tokens, PEM, JWTs)", &detectSecrets);
    if (detectSecrets) {
        ImGui::Indent();
        ImGui::Checkbox("Auto-discard detected secrets (no prompt)", &autoDiscard);
        ImGui::Unindent();
    }
    ImGui::Spacing();
    ImGui::Checkbox("Clear history when Windows locks", &clearOnLock);
    ImGui::Spacing();
    ImGui::Text("Process exclusion list (one per line):");
    ImGui::InputTextMultiline("##excl", exclusionBuf, sizeof(exclusionBuf), {-1, 120});
    ImGui::Spacing();
    ImGui::TextDisabled("Exclusion enforcement wired in Milestone 9.");
}

// ── Section: Developer ───────────────────────────────────────────────────────

void MainWindow::DrawDeveloper() {
    ImGui::TextDisabled("Developer Mode");
    ImGui::Separator();
    ImGui::Spacing();

    static bool devMode    = false;
    static bool cliEnabled = true;

    ImGui::Checkbox("Enable Developer Mode", &devMode);
    ImGui::Spacing();
    ImGui::Checkbox("Enable CLI interface (requires app running)", &cliEnabled);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Exposes a named pipe at \\\\.\\pipe\\clipboardpp\n"
            "allowing clipboardpp.exe <command> from a terminal.");

    if (devMode) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextDisabled("Features available in Milestone 10:");
        ImGui::BulletText("Clipboard format inspector (Win32 formats per item)");
        ImGui::BulletText("Raw hex / bytes viewer");
        ImGui::BulletText("Source process tracking");
        ImGui::BulletText("Named persistent slots");
        ImGui::BulletText("Regex transform engine");
        ImGui::BulletText("Template paste with slot interpolation");
        ImGui::BulletText("Side-by-side diff view");
        ImGui::BulletText("Auto pretty-print (JSON / XML / SQL)");
        ImGui::BulletText("Timestamped event log (exportable)");
    }
}

// ── Section: About ───────────────────────────────────────────────────────────

void MainWindow::DrawAbout() {
    ImGui::TextDisabled("About");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Clipboard++");
    ImGui::TextDisabled("Version 0.1.0 — Milestone 1");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "A lean, modern Windows clipboard manager built with C++17, "
        "Dear ImGui (docking branch), and DirectX 11.");
    ImGui::Spacing();
    ImGui::TextDisabled("Built with:");
    ImGui::BulletText("Dear ImGui (docking branch) — ocornut/imgui");
    ImGui::BulletText("nlohmann/json v3.11.3");
    ImGui::BulletText("DirectX 11 / Win32 API");
    ImGui::Spacing();
    ImGui::TextDisabled("License: MIT");
}
