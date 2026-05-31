#include "MainWindow.h"
#include <imgui.h>

// Section indices matching the sidebar nav
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
    "General",
    "Hotkeys",
    "Appearance",
    "History",
    "Privacy",
    "Developer",
    "About",
};

static int s_activeSection = SEC_GENERAL;

// ── Public ────────────────────────────────────────────────────────────────────

void MainWindow::Draw(bool& open) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_Once);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_Once, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (!ImGui::Begin("Clipboard++ Settings", &open, flags)) {
        ImGui::End();
        return;
    }

    // ── Layout: left sidebar | right content ─────────────────────────────────
    const float sidebarW = 160.0f;
    const float contentW = ImGui::GetContentRegionAvail().x - sidebarW - 8.0f;

    // Sidebar
    ImGui::BeginChild("##sidebar", ImVec2(sidebarW, 0), true);
    DrawSidebarNav(s_activeSection);
    ImGui::EndChild();

    ImGui::SameLine();

    // Content panel
    ImGui::BeginChild("##content", ImVec2(contentW, 0), false);
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

    ImGui::End();
}

// ── Sidebar nav ───────────────────────────────────────────────────────────────

void MainWindow::DrawSidebarNav(int& selected) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
    for (int i = 0; i < SEC_COUNT; ++i) {
        bool isActive = (i == selected);
        if (isActive)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(kSectionLabels[i], ImVec2(-1, 32)))
            selected = i;
        if (isActive)
            ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
}

// ── Section: General ──────────────────────────────────────────────────────────

void MainWindow::DrawGeneral() {
    ImGui::TextDisabled("General");
    ImGui::Separator();
    ImGui::Spacing();

    // Placeholders — wired up in Milestone 5 (config) and Milestone 7 (full UI)
    static bool startWithWindows = true;
    static bool newItemsAtTop    = true;
    static bool deduplication    = true;

    ImGui::Checkbox("Start with Windows", &startWithWindows);
    ImGui::Spacing();
    ImGui::Checkbox("New items added to top of list", &newItemsAtTop);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When off, new items are added to the bottom.");
    ImGui::Spacing();
    ImGui::Checkbox("Deduplicate — move existing copy to configured position", &deduplication);

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextDisabled("Full configuration available in Milestone 5.");
}

// ── Section: Hotkeys ─────────────────────────────────────────────────────────

void MainWindow::DrawHotkeys() {
    ImGui::TextDisabled("Hotkeys");
    ImGui::Separator();
    ImGui::Spacing();

    // Read-only preview table — live capture wired in Milestone 4
    struct HotkeyRow { const char* action; const char* binding; };
    static const HotkeyRow rows[] = {
        { "Toggle popup",                 "Ctrl+Shift+V" },
        { "Paste item 1",                 "Ctrl+Shift+1" },
        { "Multi-paste modifier (hold)",  "Ctrl+Alt"     },
        { "Toggle incognito mode",        "Ctrl+Shift+I" },
        { "Open settings",                "Ctrl+Shift+," },
        { "Open popup (images filter)",   "Ctrl+Shift+G" },
    };

    if (ImGui::BeginTable("hotkeys_tbl", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Action",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableHeadersRow();

        for (const auto& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(row.action);
            ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("%s", row.binding);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Live hotkey capture wired in Milestone 4.");
}

// ── Section: Appearance ───────────────────────────────────────────────────────

void MainWindow::DrawAppearance() {
    ImGui::TextDisabled("Appearance");
    ImGui::Separator();
    ImGui::Spacing();

    static int  themeIndex   = 0;
    static float popupOpacity = 0.95f;
    static int  popupW = 420, popupH = 520;

    const char* themes[] = {
        "Dark Default", "Dracula", "Nord", "Monokai",
        "One Dark Pro", "Tokyo Night", "Solarized Dark", "GitHub Dark",
        "GitHub Light", "Solarized Light", "VS Light", "Quiet Light"
    };

    ImGui::Text("Theme");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::Combo("##theme", &themeIndex, themes, IM_ARRAYSIZE(themes));

    ImGui::Spacing();
    ImGui::Text("Popup opacity");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::SliderFloat("##opacity", &popupOpacity, 0.1f, 1.0f, "%.2f");

    ImGui::Spacing();
    ImGui::Text("Default popup size");
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("W##pw", &popupW, 10);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("H##ph", &popupH, 10);

    ImGui::Spacing();
    ImGui::TextDisabled("Theme engine and live preview wired in Milestone 8.");
}

// ── Section: History ─────────────────────────────────────────────────────────

void MainWindow::DrawHistory() {
    ImGui::TextDisabled("History");
    ImGui::Separator();
    ImGui::Spacing();

    static int  activeLimit     = 100;
    static bool persistHistory  = true;
    static bool sessionOnly     = false;
    static bool vaultUnlimited  = true;
    static int  vaultLimitMB    = 0; // 0 = unlimited

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

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Vault (overflow archive)");
    ImGui::Checkbox("Unlimited vault size", &vaultUnlimited);
    if (!vaultUnlimited) {
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Max vault size (MB)", &vaultLimitMB);
        if (vaultLimitMB < 1) vaultLimitMB = 1;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Storage engine wired in Milestone 5.");
}

// ── Section: Privacy ─────────────────────────────────────────────────────────

void MainWindow::DrawPrivacy() {
    ImGui::TextDisabled("Privacy & Security");
    ImGui::Separator();
    ImGui::Spacing();

    static bool detectSecrets   = true;
    static bool autoDiscard     = false;
    static bool clearOnLock     = false;
    static char exclusionBuf[512] = "KeePass.exe\n1Password.exe\nBitwarden.exe";

    ImGui::Checkbox("Detect secret patterns (API keys, tokens, PEM keys, JWTs)", &detectSecrets);
    if (detectSecrets) {
        ImGui::Indent();
        ImGui::Checkbox("Auto-discard detected secrets (no prompt)", &autoDiscard);
        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::Checkbox("Clear history when Windows locks", &clearOnLock);

    ImGui::Spacing();
    ImGui::Text("Process exclusion list (one per line):");
    ImGui::InputTextMultiline("##excl", exclusionBuf, sizeof(exclusionBuf),
                               ImVec2(-1, 120));

    ImGui::Spacing();
    ImGui::TextDisabled("Exclusion enforcement wired in Milestone 9.");
}

// ── Section: Developer ────────────────────────────────────────────────────────

void MainWindow::DrawDeveloper() {
    ImGui::TextDisabled("Developer Mode");
    ImGui::Separator();
    ImGui::Spacing();

    static bool devMode = false;
    static bool cliEnabled = true;

    ImGui::Checkbox("Enable Developer Mode", &devMode);
    ImGui::Spacing();
    ImGui::Checkbox("Enable CLI interface (requires app running)", &cliEnabled);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Exposes a named pipe at \\\\.\\pipe\\clipboardpp\n"
            "allowing clipboardpp.exe <command> from a terminal.");

    if (devMode) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("Developer features available in Milestone 10:");
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

// ── Section: About ────────────────────────────────────────────────────────────

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
