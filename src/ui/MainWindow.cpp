#include "MainWindow.h"
#include "../app/Application.h"
#include "../app/ConfigStore.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ContentDetector.h"
#include "Appearance.h"
#include "PopupWindow.h"
#include <imgui.h>
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

// -- Section nav ---------------------------------------------------------------

enum Section {
    SEC_GENERAL    = 0,
    SEC_HOTKEYS    = 1,
    SEC_APPEARANCE = 2,
    SEC_HISTORY    = 3,
    SEC_PRIVACY    = 4,
#ifndef NDEBUG
    SEC_DEVELOPER  = 5,
    SEC_ABOUT      = 6,
#else
    SEC_ABOUT      = 5,
#endif
    SEC_COUNT
};

static const char* kSectionLabels[SEC_COUNT] = {
#ifndef NDEBUG
    "General", "Hotkeys", "Appearance", "History",
    "Privacy", "Developer", "About",
#else
    "General", "Hotkeys", "Appearance", "History",
    "Privacy", "About",
#endif
};

static int s_activeSection = SEC_GENERAL;
static int s_focusFrames = 0;

static constexpr ContentTag kDisplayTagOrder[] = {
    TAG_SECRET, TAG_URL, TAG_EMAIL, TAG_JSON, TAG_XML, TAG_SQL,
    TAG_CODE, TAG_COMMAND, TAG_CONFIG, TAG_FILE, TAG_FOLDER, TAG_PATH,
    TAG_IMAGE_FILE, TAG_DOCUMENT, TAG_ARCHIVE, TAG_EXECUTABLE, TAG_SCRIPT,
    TAG_DATA, TAG_AUDIO, TAG_VIDEO, TAG_MARKDOWN, TAG_CSV, TAG_HTML,
    TAG_HEX, TAG_UUID, TAG_IP, TAG_DATE, TAG_BASE64, TAG_LOG, TAG_PHONE
};

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

static std::string TimeLabel(std::chrono::system_clock::time_point tp) {
    if (tp.time_since_epoch().count() == 0)
        return "(none)";
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

static std::string TagList(uint32_t tags) {
    if (tags == TAG_NONE)
        return "(none)";

    std::string out;
    for (ContentTag tag : kDisplayTagOrder) {
        if ((tags & tag) == 0)
            continue;
        if (!out.empty())
            out += ", ";
        out += ContentDetector::TagName(tag);
    }
    return out.empty() ? "(unknown)" : out;
}

static std::string TrimAscii(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

static bool EqualsIgnoreCase(std::string a, std::string b) {
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::transform(a.begin(), a.end(), a.begin(), lower);
    std::transform(b.begin(), b.end(), b.begin(), lower);
    return a == b;
}

static bool IsBuiltInThemeName(const std::string& name) {
    for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
        if (EqualsIgnoreCase(name, ThemeName(static_cast<ThemeId>(i))))
            return true;
    }
    return false;
}

static float UiScale() {
    Application* app = Application::Get();
    return app ? EffectiveUiScale(app->GetAppearance()) : 1.0f;
}

static float S(float value) {
    return value * UiScale();
}

static void KeepMouseWheelOnLastItem() {
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
}

// -- Title bar helpers ---------------------------------------------------------

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

// -- Title bar -----------------------------------------------------------------

void MainWindow::DrawTitleBar() {
    const float W = ImGui::GetWindowWidth();
    const float H = S((float)kTitleBarHeight);
    const float BW = S((float)kTitleBtnWidth);

    ImVec2      wp = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    HWND        wnd = Application::Get()->GetHwnd();

    // -- Background -----------------------------------------------------------
    const ImU32 titleBg = ImGui::GetColorU32(ImGuiCol_WindowBg);
    const ImU32 titleLine = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 titleText = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 titleMuted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    dl->AddRectFilled(wp, {wp.x + W, wp.y + H}, titleBg);

    // -- App title text (left-aligned, vertically centred) --------------------
    float textY = (H - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorPos({S(12.0f), textY});
    ImGui::PushStyleColor(ImGuiCol_Text, titleText);
    ImGui::TextUnformatted("Clipboard++");
    ImGui::PopStyleColor();

    // -- Buttons: minimize | maximize/restore | close --------------------------
    const bool isMax = IsZoomed(wnd) != 0;
    float bx = W - BW * 3.0f;

    // - Minimize -
    if (TitleBtn("##min", bx, BW, H, false)) {
        ShowWindow(wnd, SW_MINIMIZE);
        Application::Get()->HideMainWindow();
    }
    {   // icon: horizontal bar centred in the button
        ImVec2 c = {wp.x + bx + BW * 0.5f, wp.y + H * 0.5f + 1.0f};
        dl->AddLine({c.x - S(5.0f), c.y}, {c.x + S(5.0f), c.y},
                    titleMuted, S(1.5f));
    }
    bx += BW;

    // - Maximize / Restore -
    if (TitleBtn("##max", bx, BW, H, false))
        PostMessageW(wnd, WM_SYSCOMMAND, isMax ? SC_RESTORE : SC_MAXIMIZE, 0);
    {
        ImVec2 c = {wp.x + bx + BW * 0.5f, wp.y + H * 0.5f};
        if (isMax) {
            // Restore: two overlapping squares (back then front)
            dl->AddRect({c.x - S(2.0f), c.y - S(5.0f)}, {c.x + S(5.0f), c.y + S(2.0f)},
                        titleMuted, 0, 0, S(1.2f));
            // Erase overlap area so back square doesn't bleed through front
            dl->AddRectFilled({c.x - S(5.0f), c.y - S(2.0f)}, {c.x + S(1.0f), c.y + S(5.0f)},
                              titleBg);
            dl->AddRect({c.x - S(5.0f), c.y - S(2.0f)}, {c.x + S(2.0f), c.y + S(5.0f)},
                        titleMuted, 0, 0, S(1.2f));
        } else {
            // Maximize: single square
            dl->AddRect({c.x - S(5.0f), c.y - S(5.0f)}, {c.x + S(5.0f), c.y + S(5.0f)},
                        titleMuted, 0, 0, S(1.2f));
        }
    }
    bx += BW;

    // - Close -
    if (TitleBtn("##close", bx, BW, H, true))
        PostMessageW(wnd, WM_CLOSE, 0, 0);
    {
        ImVec2 c    = {wp.x + bx + BW * 0.5f, wp.y + H * 0.5f};
        bool hov    = ImGui::IsMouseHoveringRect({wp.x + bx, wp.y},
                                                  {wp.x + bx + BW, wp.y + H});
        ImU32 xcol  = hov ? IM_COL32(255, 255, 255, 255) : titleMuted;
        dl->AddLine({c.x - S(5.0f), c.y - S(5.0f)}, {c.x + S(5.0f), c.y + S(5.0f)}, xcol, S(1.5f));
        dl->AddLine({c.x + S(5.0f), c.y - S(5.0f)}, {c.x - S(5.0f), c.y + S(5.0f)}, xcol, S(1.5f));
    }

    // -- Separator line --------------------------------------------------------
    dl->AddLine({wp.x, wp.y + H}, {wp.x + W, wp.y + H},
                titleLine);

    // Advance ImGui cursor below the title bar
    ImGui::SetCursorPos({0.0f, H});
}

// -- Main draw -----------------------------------------------------------------

void MainWindow::RequestFocus() {
    s_focusFrames = 3;
}

void MainWindow::Draw(bool& open) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    if (s_focusFrames > 0) {
        ImGui::SetNextWindowFocus();
        --s_focusFrames;
    }

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

    // -- Sidebar + content layout below the title bar --------------------------
    const float titleH   = S((float)kTitleBarHeight);
    const float pad      = S(18.0f);
    const float sidebarW = S(168.0f);
    const float contentW = ImGui::GetContentRegionAvail().x - sidebarW - pad * 2.0f;

    ImGui::SetCursorPos({pad, titleH + pad});

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(12.0f), S(12.0f)});
    ImGui::BeginChild("##sidebar", {sidebarW, 0},
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    DrawSidebarNav(s_activeSection);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine(0, pad);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(22.0f), S(20.0f)});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {S(10.0f), S(13.0f)});
    ImGui::BeginChild("##content", {contentW, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (s_activeSection) {
    case SEC_GENERAL:    DrawGeneral();    break;
    case SEC_HOTKEYS:    DrawHotkeys();    break;
    case SEC_APPEARANCE: DrawAppearance(); break;
    case SEC_HISTORY:    DrawHistory();    break;
    case SEC_PRIVACY:    DrawPrivacy();    break;
#ifndef NDEBUG
    case SEC_DEVELOPER:  DrawDeveloper();  break;
#endif
    case SEC_ABOUT:      DrawAbout();      break;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    ImGui::End();
}

// -- Sidebar nav ---------------------------------------------------------------

void MainWindow::DrawSidebarNav(int& selected) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, S(2.0f)});
    const AppearanceSettings appearance = Application::Get()
        ? Application::Get()->GetAppearance()
        : AppearanceSettings{};
    const ImVec4 selectedColor = appearance.customColors
        ? appearance.selectedTab
        : ThemeDefaults(appearance.theme).selectedTab;
    for (int i = 0; i < SEC_COUNT; ++i) {
        bool active = (i == selected);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, selectedColor);
        if (ImGui::Button(kSectionLabels[i], {-1.0f, S(32.0f)}))
            selected = i;
        if (active)
            ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
}

// -- Section: General ---------------------------------------------------------

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
    ImGui::Checkbox("Deduplicate - move existing copy to configured position", &deduplication);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Clipboards");

    const ClipboardProfileConfig* activeProfile = app->GetActiveClipboardProfile();
    static std::string lastProfileId;
    static char clipboardNameBuf[128]{};
    enum class PendingClipboardAction { None, Rename, Create, Delete };
    static PendingClipboardAction pendingAction = PendingClipboardAction::None;
    static std::string pendingName;
    if (activeProfile && activeProfile->id != lastProfileId) {
        lastProfileId = activeProfile->id;
        strncpy_s(clipboardNameBuf, activeProfile->name.c_str(), _TRUNCATE);
    }

    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputText("Clipboard", clipboardNameBuf, sizeof(clipboardNameBuf));
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button("v##clipboard_dropdown", {S(28.0f), 0.0f}))
        ImGui::OpenPopup("##clipboard_profile_picker");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Select another clipboard");

    if (ImGui::BeginPopup("##clipboard_profile_picker")) {
        for (const ClipboardProfileConfig& profile : app->GetClipboardProfiles()) {
            const bool selected = activeProfile && profile.id == activeProfile->id;
            std::string label = profile.name;
            if (!profile.processName.empty())
                label += " (" + profile.processName + ")";
            if (ImGui::Selectable(label.c_str(), selected)) {
                app->SetActiveClipboardProfile(profile.id);
                strncpy_s(clipboardNameBuf, profile.name.c_str(), _TRUNCATE);
                lastProfileId = profile.id;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndPopup();
    }

    auto requestConfirmation = [&](PendingClipboardAction action, std::string name) {
        pendingAction = action;
        pendingName = std::move(name);
        MessageBeep(MB_ICONQUESTION);
        ImGui::OpenPopup("Confirm clipboard action");
    };

    std::string typedName = TrimAscii(clipboardNameBuf);
    if (ImGui::SmallButton("Set name")) {
        if (typedName.empty() && activeProfile)
            typedName = activeProfile->name;
        requestConfirmation(PendingClipboardAction::Rename, typedName);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("New clipboard")) {
        std::string name = typedName;
        if (name.empty())
            name = "Clipboard " + std::to_string(app->GetClipboardProfiles().size() + 1);
        requestConfirmation(PendingClipboardAction::Create, name);
    }
    ImGui::SameLine();
    if (!app->CanDeleteActiveClipboardProfile())
        ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(220, 35, 35, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 65, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 20, 20, 255));
    if (ImGui::SmallButton("Delete active")) {
        requestConfirmation(PendingClipboardAction::Delete,
                            activeProfile ? activeProfile->name : "Clipboard");
    }
    ImGui::PopStyleColor(3);
    if (!app->CanDeleteActiveClipboardProfile())
        ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("Confirm clipboard action", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* actionText = "continue";
        if (pendingAction == PendingClipboardAction::Rename)
            actionText = "rename the active clipboard";
        else if (pendingAction == PendingClipboardAction::Create)
            actionText = "create a new clipboard";
        else if (pendingAction == PendingClipboardAction::Delete)
            actionText = "delete the active clipboard";

        ImGui::TextWrapped("Confirm that you want to %s.", actionText);
        if (!pendingName.empty())
            ImGui::TextDisabled("%s", pendingName.c_str());
        ImGui::Spacing();

        if (ImGui::Button("Confirm", {S(110.0f), 0.0f})) {
            if (pendingAction == PendingClipboardAction::Rename) {
                app->RenameActiveClipboardProfile(pendingName);
                strncpy_s(clipboardNameBuf, pendingName.c_str(), _TRUNCATE);
            } else if (pendingAction == PendingClipboardAction::Create) {
                app->CreateClipboardProfile(pendingName);
                strncpy_s(clipboardNameBuf, pendingName.c_str(), _TRUNCATE);
            } else if (pendingAction == PendingClipboardAction::Delete) {
                app->DeleteActiveClipboardProfile();
                if (const ClipboardProfileConfig* profile = app->GetActiveClipboardProfile())
                    strncpy_s(clipboardNameBuf, profile->name.c_str(), _TRUNCATE);
            }
            pendingAction = PendingClipboardAction::None;
            pendingName.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {S(90.0f), 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pendingAction = PendingClipboardAction::None;
            pendingName.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (activeProfile) {
        ImGui::TextDisabled("ID: %s", activeProfile->id.c_str());
        ImGui::TextDisabled("Created: %s", activeProfile->createdAt.c_str());
        ImGui::TextDisabled("Updated: %s", activeProfile->updatedAt.c_str());
        ImGui::TextDisabled("Bound app: %s",
                            activeProfile->processName.empty()
                                ? "(none)"
                                : activeProfile->processName.c_str());
    }

    ImGui::Spacing(); ImGui::Spacing();
    ImGui::TextDisabled("Full configuration wired in Milestone 5.");
}

// -- Section: Hotkeys ---------------------------------------------------------

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

// -- Section: Appearance ------------------------------------------------------

static bool ColorControl(const char* label, ImVec4& color) {
    ImGui::SetNextItemWidth(220.0f);
    return ImGui::ColorEdit4(label, &color.x,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
}

static bool ColorControlWithReset(const char* label, ImVec4& color, const ImVec4& resetValue) {
    bool changed = ColorControl(label, color);
    ImGui::SameLine();
    std::string id = std::string("Reset##reset_") + label;
    if (ImGui::SmallButton(id.c_str())) {
        color = resetValue;
        changed = true;
    }
    return changed;
}

static void DrawPopupPreview(const AppearanceSettings& draft) {
    AppearanceSettings preview = draft.customColors ? draft : ThemeDefaults(draft.theme);
    const PopupToggleColors toggles = GetPopupToggleColors(draft);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    ImVec2 size = {std::min(300.0f, ImGui::GetContentRegionAvail().x), 150.0f};
    ImVec2 end = {start.x + size.x, start.y + size.y};

    dl->AddRectFilled(start, end, ImGui::GetColorU32(preview.windowBg), 6.0f);
    dl->AddRect(start, end, ImGui::GetColorU32(preview.accent), 6.0f, 0, 1.0f);

    ImVec2 closeA = {start.x + 10.0f, start.y + 10.0f};
    ImVec2 closeB = {closeA.x + 22.0f, closeA.y + 22.0f};
    dl->AddRectFilled(closeA, closeB, ImGui::GetColorU32(toggles.off), 3.0f);
    dl->AddText({closeA.x + 8.0f, closeA.y + 3.0f}, ImGui::GetColorU32(preview.text), "x");

    ImVec2 comboA = {closeB.x + 12.0f, closeA.y};
    ImVec2 comboB = {end.x - 12.0f, closeB.y};
    dl->AddRectFilled(comboA, comboB, ImGui::GetColorU32(preview.panelBg), 3.0f);
    dl->AddText({comboA.x + 8.0f, comboA.y + 4.0f}, ImGui::GetColorU32(preview.text), "Clipboard");

    ImVec2 searchA = {start.x + 10.0f, closeB.y + 12.0f};
    ImVec2 searchB = {end.x - 10.0f, searchA.y + 24.0f};
    dl->AddRectFilled(searchA, searchB, ImGui::GetColorU32(preview.panelBg), 3.0f);
    dl->AddText({searchA.x + 8.0f, searchA.y + 4.0f}, ImGui::GetColorU32(preview.mutedText), "Search...");

    const char* labels[] = {"All", "Text", "Image"};
    float x = searchA.x;
    float y = searchB.y + 12.0f;
    for (int i = 0; i < IM_ARRAYSIZE(labels); ++i) {
        ImVec2 a = {x, y};
        ImVec2 b = {x + 50.0f, y + 22.0f};
        const ImU32 col = ImGui::GetColorU32(i == 0 ? toggles.on : toggles.off);
        dl->AddRectFilled(a, b, col, 3.0f);
        dl->AddText({a.x + 8.0f, a.y + 3.0f}, ImGui::GetColorU32(preview.text), labels[i]);
        x += 56.0f;
    }

    ImVec2 rowA = {start.x + 10.0f, y + 36.0f};
    ImVec2 rowB = {end.x - 10.0f, rowA.y + 26.0f};
    dl->AddRectFilled(rowA, rowB, ImGui::GetColorU32(preview.panelBg), 3.0f);
    dl->AddCircleFilled({rowA.x + 10.0f, rowA.y + 13.0f}, 3.0f, IM_COL32(255, 196, 64, 255), 12);
    dl->AddText({rowA.x + 20.0f, rowA.y + 5.0f}, ImGui::GetColorU32(preview.text), "1  [P] Copied item preview");

    ImGui::Dummy(size);
}

static void DrawSettingsPreview(const AppearanceSettings& draft) {
    AppearanceSettings preview = draft.customColors ? draft : ThemeDefaults(draft.theme);
    const PopupToggleColors toggles = GetPopupToggleColors(draft);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    ImVec2 size = {std::min(300.0f, ImGui::GetContentRegionAvail().x), 130.0f};
    ImVec2 end = {start.x + size.x, start.y + size.y};

    dl->AddRectFilled(start, end, ImGui::GetColorU32(preview.windowBg), 0.0f);
    dl->AddRectFilled(start, {end.x, start.y + 28.0f}, ImGui::GetColorU32(preview.panelBg), 0.0f);
    dl->AddText({start.x + 12.0f, start.y + 7.0f}, ImGui::GetColorU32(preview.text), "Clipboard++");

    ImVec2 sideA = {start.x + 10.0f, start.y + 38.0f};
    ImVec2 sideB = {sideA.x + 96.0f, end.y - 12.0f};
    dl->AddRectFilled(sideA, sideB, ImGui::GetColorU32(preview.panelBg), 4.0f);
    dl->AddRectFilled({sideA.x + 8.0f, sideA.y + 10.0f}, {sideB.x - 8.0f, sideA.y + 34.0f},
                      ImGui::GetColorU32(toggles.on), 3.0f);
    dl->AddText({sideA.x + 16.0f, sideA.y + 14.0f}, ImGui::GetColorU32(preview.text), "Appearance");
    dl->AddText({sideA.x + 16.0f, sideA.y + 46.0f}, ImGui::GetColorU32(preview.mutedText), "General");

    ImVec2 paneA = {sideB.x + 14.0f, sideA.y};
    ImVec2 paneB = {end.x - 12.0f, sideB.y};
    dl->AddRectFilled(paneA, paneB, ImGui::GetColorU32(preview.panelBg), 4.0f);
    dl->AddText({paneA.x + 12.0f, paneA.y + 12.0f}, ImGui::GetColorU32(preview.text), "Theme");
    dl->AddRectFilled({paneA.x + 12.0f, paneA.y + 44.0f}, {paneA.x + 118.0f, paneA.y + 68.0f},
                      ImGui::GetColorU32(toggles.off), 3.0f);
    dl->AddText({paneA.x + 24.0f, paneA.y + 48.0f}, ImGui::GetColorU32(preview.text), "Apply");
    ImGui::Dummy(size);
}

void MainWindow::DrawAppearance() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("Appearance");
    ImGui::Separator();
    ImGui::Spacing();

    static AppearanceSettings draft = app->GetAppearance();
    static char fontPath[512]{};
    static char customName[96] = "Custom";
    static bool initialized = false;
    if (!initialized) {
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        std::snprintf(customName, sizeof(customName), "%s",
                      draft.customColors ? draft.customThemeName.c_str() : ThemeName(draft.theme));
        initialized = true;
    }

    ImGui::Text("Theme");
    auto syncThemeName = [&]() {
        std::snprintf(customName, sizeof(customName), "%s",
                      draft.customColors ? draft.customThemeName.c_str() : ThemeName(draft.theme));
    };
    auto applyBuiltInTheme = [&](ThemeId theme) {
        AppearanceSettings next = ThemeDefaults(theme);
        const AppearanceSettings current = app->GetAppearance();
        next.fontPath = current.fontPath;
        next.fontSize = current.fontSize;
        next.uiScale = current.uiScale;
        next.popupOpacity = current.popupOpacity;
        next.popupOutlineStrength = current.popupOutlineStrength;
        next.popupWidth = current.popupWidth;
        next.popupHeight = current.popupHeight;
        next.popupRounding = current.popupRounding;
        next.controlRounding = current.controlRounding;
        next.savedThemes = current.savedThemes;
        next.customColors = false;
        next.customThemeName = ThemeName(theme);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };
    auto applyUserTheme = [&](const SavedAppearanceTheme& saved) {
        AppearanceSettings next = app->GetAppearance();
        ApplySavedTheme(next, saved);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };
    auto saveUserTheme = [&](const std::string& requestedName) {
        AppearanceSettings next = draft;
        next.savedThemes = app->GetAppearance().savedThemes;
        next.fontPath = app->GetAppearance().fontPath;
        next.fontSize = app->GetAppearance().fontSize;
        next.uiScale = app->GetAppearance().uiScale;
        next.customColors = true;
        next.customThemeName = requestedName.empty() ? "Custom" : requestedName;
        SavedAppearanceTheme saved = ToSavedTheme(next, next.customThemeName);
        auto it = std::find_if(next.savedThemes.begin(), next.savedThemes.end(),
            [&](const SavedAppearanceTheme& existing) { return EqualsIgnoreCase(existing.name, saved.name); });
        if (it == next.savedThemes.end())
            next.savedThemes.push_back(saved);
        else
            *it = saved;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };
    auto findUserTheme = [&](const std::string& name) {
        const std::vector<SavedAppearanceTheme>& themes = app->GetAppearance().savedThemes;
        return std::find_if(themes.begin(), themes.end(),
            [&](const SavedAppearanceTheme& saved) { return EqualsIgnoreCase(saved.name, name); });
    };
    auto deleteUserTheme = [&](const std::string& name) {
        AppearanceSettings next = app->GetAppearance();
        auto it = std::remove_if(next.savedThemes.begin(), next.savedThemes.end(),
            [&](const SavedAppearanceTheme& saved) { return EqualsIgnoreCase(saved.name, name); });
        if (it == next.savedThemes.end())
            return;

        next.savedThemes.erase(it, next.savedThemes.end());
        if (next.customColors && EqualsIgnoreCase(next.customThemeName, name)) {
            AppearanceSettings fallback = ThemeDefaults(next.theme);
            fallback.fontPath = next.fontPath;
            fallback.fontSize = next.fontSize;
            fallback.uiScale = next.uiScale;
            fallback.popupOpacity = next.popupOpacity;
            fallback.popupOutlineStrength = next.popupOutlineStrength;
            fallback.popupWidth = next.popupWidth;
            fallback.popupHeight = next.popupHeight;
            fallback.popupRounding = next.popupRounding;
            fallback.controlRounding = next.controlRounding;
            fallback.savedThemes = next.savedThemes;
            next = fallback;
        }
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };

    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputText("##theme_name", customName, sizeof(customName));
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::Button("v##theme_dropdown", {S(28.0f), 0.0f}))
        ImGui::OpenPopup("##theme_picker");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Select a theme");

    if (ImGui::BeginPopup("##theme_picker")) {
        ImGui::TextDisabled("Built-in themes");
        for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
            const ThemeId theme = static_cast<ThemeId>(i);
            const bool selected = !draft.customColors && draft.theme == theme;
            if (ImGui::Selectable(ThemeName(theme), selected))
                applyBuiltInTheme(theme);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::Separator();
        ImGui::TextDisabled("User configured");
        if (draft.savedThemes.empty()) {
            ImGui::TextDisabled("No saved themes");
        } else {
            for (const SavedAppearanceTheme& saved : draft.savedThemes) {
                const bool selected = draft.customColors && saved.name == draft.customThemeName;
                if (ImGui::Selectable(saved.name.c_str(), selected))
                    applyUserTheme(saved);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndPopup();
    }

    const std::string requestedName = TrimAscii(customName);
    const std::string selectedThemeName = draft.customColors
        ? draft.customThemeName
        : ThemeName(draft.theme);
    const bool themeNameChanged = !requestedName.empty() &&
        !EqualsIgnoreCase(requestedName, selectedThemeName);
    if (themeNameChanged) {
        ImGui::SameLine();
        if (ImGui::Button("Save theme")) {
            if (IsBuiltInThemeName(requestedName)) {
                MessageBeep(MB_ICONWARNING);
                ImGui::OpenPopup("Rename custom theme");
            } else {
                saveUserTheme(requestedName);
            }
        }
    }

    const std::string themeNameForDelete = TrimAscii(customName);
    const bool canDeleteTheme = !themeNameForDelete.empty() &&
        findUserTheme(themeNameForDelete) != app->GetAppearance().savedThemes.end();
    if (canDeleteTheme || themeNameChanged)
        ImGui::SameLine();
    if (!canDeleteTheme)
        ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(220, 35, 35, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 65, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 20, 20, 255));
    if (ImGui::Button("Delete theme")) {
        MessageBeep(MB_ICONWARNING);
        ImGui::OpenPopup("Delete custom theme");
    }
    ImGui::PopStyleColor(3);
    if (!canDeleteTheme)
        ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("Delete custom theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this user theme?");
        ImGui::TextDisabled("%s", themeNameForDelete.c_str());
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(220, 35, 35, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 65, 65, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 20, 20, 255));
        if (ImGui::Button("Delete", {S(100.0f), 0.0f})) {
            deleteUserTheme(themeNameForDelete);
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {S(90.0f), 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Rename custom theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char renameThemeBuf[96]{};
        if (renameThemeBuf[0] == '\0') {
            std::string base = TrimAscii(customName);
            if (base.empty() || IsBuiltInThemeName(base))
                base = std::string(base.empty() ? "Custom" : base) + " Custom";
            std::snprintf(renameThemeBuf, sizeof(renameThemeBuf), "%s", base.c_str());
        }

        ImGui::TextWrapped("Built-in theme names are reserved. Choose a custom theme name.");
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("Name##rename_theme", renameThemeBuf, sizeof(renameThemeBuf));

        const std::string renameName = TrimAscii(renameThemeBuf);
        if (renameName.empty() || IsBuiltInThemeName(renameName))
            ImGui::BeginDisabled();
        if (ImGui::Button("Save custom theme", {S(150.0f), 0.0f})) {
            saveUserTheme(renameName);
            renameThemeBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        if (renameName.empty() || IsBuiltInThemeName(renameName))
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {S(90.0f), 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            renameThemeBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing(); ImGui::SeparatorText("Popup");
    ImGui::Spacing();
    ImGui::Text("Popup opacity");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::SliderFloat("##opacity", &draft.popupOpacity, 0.1f, 1.0f, "%.2f");
    KeepMouseWheelOnLastItem();
    ImGui::Text("Popup outline");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::SliderFloat("##outline_strength", &draft.popupOutlineStrength, 0.0f, 1.0f, "%.2f");
    KeepMouseWheelOnLastItem();

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
    }
    SIZE livePopupSize = app->PopupCurrentSize();
    ImGui::TextDisabled("Current popup: %ldx%ld",
                        static_cast<long>(livePopupSize.cx),
                        static_cast<long>(livePopupSize.cy));
    ImGui::SameLine();
    if (ImGui::SmallButton("Use current as default")) {
        app->UseCurrentPopupSizeAsDefault();
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Apply popup settings")) {
        AppearanceSettings next = app->GetAppearance();
        next.popupOpacity = std::clamp(draft.popupOpacity, 0.1f, 1.0f);
        next.popupOutlineStrength = std::clamp(draft.popupOutlineStrength, 0.0f, 1.0f);
        next.popupWidth = std::max(360, draft.popupWidth);
        next.popupHeight = std::max(260, draft.popupHeight);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }

    ImGui::Spacing(); ImGui::SeparatorText("Font");
    ImGui::Text("Font");
    auto applyFont = [&]() {
        AppearanceSettings next = app->GetAppearance();
        next.fontPath = fontPath;
        next.fontSize = std::clamp(std::round(draft.fontSize * 2.0f) * 0.5f, 9.0f, 32.0f);
        {
            std::ostringstream out;
            out << "MainWindow::ApplyFont: requested"
                << " path=\"" << next.fontPath << "\""
                << " size=" << next.fontSize
                << " draftSize=" << draft.fontSize;
            app->LogDebug(out.str());
        }
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    };

    ImGui::SetNextItemWidth(420.0f);
    bool fontPathEdited = ImGui::InputText("##fontPath", fontPath, sizeof(fontPath),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    bool fontPathDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
        if (PickFontFile(fontPath, sizeof(fontPath)))
            applyFont();
    }
    if (fontPathEdited)
        applyFont();
    else if (fontPathDeactivated) {
        draft.fontPath = fontPath;
    }

    ImGui::SetNextItemWidth(240.0f);
    float fontSize = draft.fontSize;
    bool fontSizeChanged = ImGui::SliderFloat("Font size##fontSizeSlider", &fontSize,
                                              9.0f, 32.0f, "%.1f",
                                              ImGuiSliderFlags_AlwaysClamp);
    KeepMouseWheelOnLastItem();
    if (ImGui::IsItemHovered()) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            fontSize = draft.fontSize + wheel * 0.5f;
            fontSizeChanged = true;
        }
    }
    if (fontSizeChanged) {
        const float snapped = std::clamp(std::round(fontSize * 2.0f) * 0.5f, 9.0f, 32.0f);
        draft.fontSize = snapped;
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply font"))
        applyFont();

    ImGui::Spacing(); ImGui::SeparatorText("Interface");
    const float scalePresets[] = {0.75f, 1.0f, 1.25f, 1.5f, 1.75f};
    const char* scaleLabels[] = {"0.75x", "1.00x", "1.25x", "1.50x", "1.75x", "Custom"};
    static bool customScaleMode = false;
    auto applyScale = [&]() {
        draft.uiScale = std::clamp(draft.uiScale, 0.75f, 2.0f);
        AppearanceSettings next = draft;
        next.fontPath = app->GetAppearance().fontPath;
        next.fontSize = app->GetAppearance().fontSize;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    };

    int scaleChoice = IM_ARRAYSIZE(scaleLabels) - 1;
    for (int i = 0; i < IM_ARRAYSIZE(scalePresets); ++i) {
        if (std::fabs(draft.uiScale - scalePresets[i]) < 0.01f) {
            scaleChoice = i;
            break;
        }
    }
    if (customScaleMode)
        scaleChoice = IM_ARRAYSIZE(scaleLabels) - 1;

    ImGui::Text("Interface scale");
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::Combo("##uiScalePreset", &scaleChoice, scaleLabels, IM_ARRAYSIZE(scaleLabels))) {
        if (scaleChoice < IM_ARRAYSIZE(scalePresets)) {
            customScaleMode = false;
            draft.uiScale = scalePresets[scaleChoice];
            applyScale();
        } else {
            customScaleMode = true;
        }
    }

    if (customScaleMode || scaleChoice == IM_ARRAYSIZE(scaleLabels) - 1) {
        ImGui::SetNextItemWidth(240.0f);
        bool scaleChanged = ImGui::SliderFloat("Custom scale##uiScaleCustom", &draft.uiScale,
                                               0.75f, 2.0f, "%.2fx",
                                               ImGuiSliderFlags_AlwaysClamp);
        KeepMouseWheelOnLastItem();
        if (ImGui::IsItemHovered()) {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                draft.uiScale = std::clamp(draft.uiScale + wheel * 0.05f, 0.75f, 2.0f);
                scaleChanged = true;
            }
        }
        if (scaleChanged)
            applyScale();
    }

    ImGui::Spacing(); ImGui::SeparatorText("Colors");
    const AppearanceSettings reset = ThemeDefaults(draft.theme);
    if (ImGui::BeginTable("##appearance_color_preview", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Colors", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        ImGui::Checkbox("Enable custom colors", &draft.customColors);
        if (!draft.customColors)
            ImGui::BeginDisabled();
        ColorControlWithReset("Window background", draft.windowBg, reset.windowBg);
        ColorControlWithReset("Panel background", draft.panelBg, reset.panelBg);
        ColorControlWithReset("Text", draft.text, reset.text);
        ColorControlWithReset("Muted text", draft.mutedText, reset.mutedText);
        ColorControlWithReset("Accent", draft.accent, reset.accent);
        ColorControlWithReset("Hover", draft.hover, reset.hover);
        ColorControlWithReset("Selected tab", draft.selectedTab, reset.selectedTab);
        ColorControlWithReset("Button off", draft.buttonOff, reset.buttonOff);
        ColorControlWithReset("Button on", draft.buttonOn, reset.buttonOn);
        ColorControlWithReset("Popup close", draft.closeButton, reset.closeButton);
        ColorControlWithReset("Popup close hover", draft.closeButtonHover, reset.closeButtonHover);
        ColorControlWithReset("Popup close text", draft.closeButtonText, reset.closeButtonText);
        ColorControlWithReset("Opacity knob fill", draft.opacityKnobFill, reset.opacityKnobFill);
        ColorControlWithReset("Opacity knob ring", draft.opacityKnobRing, reset.opacityKnobRing);
        if (!draft.customColors)
            ImGui::EndDisabled();
        if (ImGui::Button("Apply colors")) {
            AppearanceSettings next = draft;
            next.fontPath = app->GetAppearance().fontPath;
            next.fontSize = app->GetAppearance().fontSize;
            next.uiScale = app->GetAppearance().uiScale;
            next.savedThemes = app->GetAppearance().savedThemes;
            app->RequestAppearance(next);
            draft = app->GetAppearance();
            syncThemeName();
            std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Preview");
        DrawPopupPreview(draft);
        ImGui::Spacing();
        DrawSettingsPreview(draft);
        ImGui::EndTable();
    }

    ImGui::Spacing(); ImGui::SeparatorText("Advanced shape");
    ImGui::SetNextItemWidth(240.0f);
    bool shapeChanged = ImGui::SliderFloat("Popup corner rounding", &draft.popupRounding,
                                           0.0f, 18.0f, "%.1f",
                                           ImGuiSliderFlags_AlwaysClamp);
    KeepMouseWheelOnLastItem();
    ImGui::SetNextItemWidth(240.0f);
    shapeChanged |= ImGui::SliderFloat("Control rounding", &draft.controlRounding,
                                       0.0f, 12.0f, "%.1f",
                                       ImGuiSliderFlags_AlwaysClamp);
    KeepMouseWheelOnLastItem();
    if (shapeChanged) {
        draft.popupRounding = std::clamp(draft.popupRounding, 0.0f, 18.0f);
        draft.controlRounding = std::clamp(draft.controlRounding, 0.0f, 12.0f);
    }
    if (ImGui::Button("Apply shape")) {
        AppearanceSettings next = app->GetAppearance();
        next.popupRounding = std::clamp(draft.popupRounding, 0.0f, 18.0f);
        next.controlRounding = std::clamp(draft.controlRounding, 0.0f, 12.0f);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        syncThemeName();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }
}

// -- Section: History ---------------------------------------------------------

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
    KeepMouseWheelOnLastItem();

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

    // -- Live history preview --------------------------------------------------
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ClipboardHistory* hist = Application::Get()->GetHistory();
    size_t count = hist ? hist->Size() : 0;
    ImGui::Text("Live history  (%zu / %d items)", count, activeLimit);
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
    if (ImGui::BeginChild("##histlive", {-1.0f, 220.0f},
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding)) {
        if (count == 0) {
            ImGui::TextDisabled("  Nothing captured yet - copy something!");
        } else {
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
                for (ContentTag t : kDisplayTagOrder) {
                    if (!(item->tags & t)) continue;
                    ImVec4 col = (item->tags & TAG_SECRET)
                        ? ImVec4(1.f, 0.34f, 0.34f, 1.f)
                        : ImVec4(0.4f, 0.7f, 1.0f, 1.f);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::Text("[%s]", ContentDetector::TagName(t));
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                }

#ifndef NDEBUG
                const DeveloperSettings& dev = Application::Get()->GetDeveloperSettings();
                if (dev.enabled && dev.showSourceProcess && !item->sourceProcess.empty()) {
                    ImGui::TextDisabled("{%s}", item->sourceProcess.c_str());
                    ImGui::SameLine();
                }
#endif

                ImGui::TextUnformatted(item->Preview(72).c_str());
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// -- Section: Privacy ---------------------------------------------------------

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

// -- Section: Developer -------------------------------------------------------

#ifndef NDEBUG
void MainWindow::DrawDeveloper() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("Developer Mode");
    ImGui::Separator();
    ImGui::Spacing();

    DeveloperSettings dev = app->GetDeveloperSettings();
    bool changed = false;

    changed |= ImGui::Checkbox("Enable Developer Mode", &dev.enabled);
    ImGui::Spacing();
    changed |= ImGui::Checkbox("Enable CLI interface", &dev.cliEnabled);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When off, runtime CLI commands are blocked. Help, status, config, and --set still work.");
    changed |= ImGui::Checkbox("Show source process metadata", &dev.showSourceProcess);
    changed |= ImGui::Checkbox("Enable developer event log", &dev.eventLogEnabled);

    if (changed)
        app->SetDeveloperSettings(dev);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Advanced Clipboard Routing");
    if (!dev.enabled)
        ImGui::BeginDisabled();

    if (ImGui::SmallButton("Bind active clipboard to focused app"))
        app->BindActiveClipboardToForegroundProcess();

    bool autoSwitch = app->GetAutoSwitchClipboardByProcess();
    if (ImGui::Checkbox("Auto-switch clipboard by focused app", &autoSwitch))
        app->SetAutoSwitchClipboardByProcess(autoSwitch);

    bool autoCreate = app->GetAutoCreateClipboardByProcess();
    if (ImGui::Checkbox("Auto-create clipboard for focused app", &autoCreate))
        app->SetAutoCreateClipboardByProcess(autoCreate);

    if (!dev.enabled)
        ImGui::EndDisabled();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Runtime Diagnostics");
    const ClipboardProfileConfig* active = app->GetActiveClipboardProfile();
    ClipboardHistory* hist = app->GetHistory();
    const SIZE livePopupSize = app->PopupCurrentSize();
    ImGui::Text("Process ID: %lu", static_cast<unsigned long>(app->ProcessId()));
    ImGui::TextWrapped("Executable: %s    Popup: %ldx%ld",
                       app->ExecutablePath().c_str(),
                       static_cast<long>(livePopupSize.cx),
                       static_cast<long>(livePopupSize.cy));
    ImGui::TextWrapped("Working directory: %s", app->WorkingDirectory().c_str());
    ImGui::Text("Foreground process: %s", app->ForegroundProcessName().c_str());
    ImGui::Text("Active clipboard: %s", active ? active->name.c_str() : "(none)");
    ImGui::Text("Clipboard ID: %s", active ? active->id.c_str() : "(none)");
    ImGui::Text("Bound process: %s",
                active && !active->processName.empty() ? active->processName.c_str() : "(none)");
    ImGui::Text("History items: %zu", hist ? hist->Size() : 0);
    ImGui::Text("Pinned items: %zu", hist ? hist->PinnedSize() : 0);
    ImGui::Text("Config: %s", ConfigStore::Path().string().c_str());
    ImGui::Text("Fonts: %s", ConfigStore::FontsDirectory().string().c_str());
    if (ImGui::SmallButton("Toggle debug output"))
        app->ToggleDebugWindow();

    if (dev.enabled && hist && hist->Size() > 0) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextDisabled("Clipboard Item Inspector");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
        if (ImGui::BeginChild("##dev_item_inspector", {-1.0f, 210.0f},
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding)) {
            const size_t limit = std::min<size_t>(hist->Size(), 25);
            for (size_t i = 0; i < limit; ++i) {
                const ClipboardItem* item = hist->Get(i);
                if (!item) continue;
                ImGui::PushID(static_cast<int>(i));
                const std::string label = std::to_string(i + 1) + "  " + item->Preview(64);
                if (ImGui::TreeNode(label.c_str())) {
                    ImGui::Text("ID: %llu", static_cast<unsigned long long>(item->id));
                    ImGui::Text("Hash: %llu", static_cast<unsigned long long>(item->contentHash));
                    ImGui::Text("Type: %s", ContentTypeName(item->type));
                    ImGui::TextWrapped("Tags: %s", TagList(item->tags).c_str());
                    ImGui::Text("Pinned: %s", item->pinned ? "yes" : "no");
                    ImGui::Text("Source: %s",
                                item->sourceProcess.empty() ? "(unknown)" : item->sourceProcess.c_str());
                    ImGui::Text("Captured: %s", TimeLabel(item->timestamp).c_str());
                    ImGui::Text("Created: %s", TimeLabel(item->createdAt).c_str());
                    ImGui::Text("Updated: %s", TimeLabel(item->updatedAt).c_str());
                    ImGui::Text("Last used: %s", TimeLabel(item->lastUsedAt).c_str());
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    if (dev.enabled) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextDisabled("Developer Event Log");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy log")) {
            std::string text;
            for (const std::string& line : app->GetDeveloperEvents()) {
                text += line;
                text += "\n";
            }
            ImGui::SetClipboardText(text.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear log"))
            app->ClearDeveloperEvents();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
        if (ImGui::BeginChild("##dev_event_log", {-1.0f, 180.0f},
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding)) {
            const auto& events = app->GetDeveloperEvents();
            if (!dev.eventLogEnabled) {
                ImGui::TextDisabled("  Enable developer event log to collect new events.");
            } else if (events.empty()) {
                ImGui::TextDisabled("  No developer events yet.");
            } else {
                for (const std::string& line : events)
                    ImGui::Selectable(line.c_str(), false);
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::Spacing();
        ImGui::TextDisabled("Still planned: regex transforms, named slots, raw bytes viewer, pretty-print, exports.");
    }
}
#endif

// -- Section: About -----------------------------------------------------------

void MainWindow::DrawAbout() {
    ImGui::TextDisabled("About");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Clipboard++");
    ImGui::TextDisabled("Version 0.1.0 - Milestone 1");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "A lean, modern Windows clipboard manager built with C++17, "
        "Dear ImGui (docking branch), and DirectX 11.");
    ImGui::Spacing();
    ImGui::TextDisabled("Built with:");
    ImGui::BulletText("Dear ImGui (docking branch) - ocornut/imgui");
    ImGui::BulletText("nlohmann/json v3.11.3");
    ImGui::BulletText("DirectX 11 / Win32 API");
    ImGui::Spacing();
    ImGui::TextDisabled("License: MIT");
}
