#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "ImGuiWidgets.h"
#include "../app/Application.h"
#include "../app/ConfigStore.h"
#include "../app/TrayIcon.h"
#include "../clipboard/ImageStore.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ContentDetector.h"
#include "../filters/CustomFilter.h"
#include "Appearance.h"
#include "PopupWindow.h"
#include <imgui.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>


using namespace MainWindowInternal;

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
    static bool passthroughCaptureOpen = false;
    static bool passthroughCaptureReady = false;
    static KeyBinding passthroughPending{};
    if (!initialized) {
        draft = app->GetHotkeySettings();
        initialized = true;
    }

    KeyBinding captured;
    if (hotkeys->ConsumeCapturedBinding(captured)) {
        if (passthroughCaptureOpen) {
            passthroughPending = captured;
            passthroughCaptureReady = true;
        } else if (captureIndex >= 0 &&
                   static_cast<size_t>(captureIndex) < draft.bindings.size()) {
            captured.action = draft.bindings[static_cast<size_t>(captureIndex)].action;
            captured.data = draft.bindings[static_cast<size_t>(captureIndex)].data;
            draft.bindings[static_cast<size_t>(captureIndex)] = captured;
            app->RequestHotkeySettings(draft);
            captureIndex = -1;
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10.0f, 8.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12.0f, 6.0f});

    float widestBindingText = ButtonWidthForText("Press keys...", 180.0f);
    for (const KeyBinding& binding : draft.bindings) {
        const std::string text = HotkeyManager::BindingText(binding);
        widestBindingText = std::max(widestBindingText, ButtonWidthForText(text.c_str(), 180.0f));
    }
    const float resetButtonW = ButtonWidthForText("Reset", 84.0f);
    const float bindingColumnW = widestBindingText + resetButtonW + ImGui::GetStyle().ItemSpacing.x;

    if (ImGui::BeginTable("##hotkeys", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Binding",  ImGuiTableColumnFlags_WidthFixed, bindingColumnW);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Function");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Binding");
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
            if (ImGui::Button(label.c_str(), {widestBindingText, 0.0f})) {
                captureIndex = static_cast<int>(i);
                hotkeys->BeginCapture();
            }
            ImGui::SameLine();
            const std::string resetId = "Reset##resetHotkey" + std::to_string(i);
            if (ImGui::Button(resetId.c_str(), {resetButtonW, 0.0f})) {
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
    ImGui::PopStyleVar(2);

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

    SectionHeader("Popup Pass-through Hotkeys");
    ImGui::TextDisabled("Defined hotkeys are ignored by Clipboard++ and passed to Windows.");
    if (draft.passthroughHotkeys.empty()) {
        ImGui::TextDisabled("No pass-through hotkeys defined.");
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10.0f, 6.0f});
        if (ImGui::BeginTable("##popup_passthrough_table", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Hotkey", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            for (size_t i = 0; i < draft.passthroughHotkeys.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(draft.passthroughHotkeys[i].c_str());
                ImGui::TableSetColumnIndex(1);
                const std::string removeId = "Remove##passthrough_" + std::to_string(i);
                if (DangerButton(removeId.c_str(), 84.0f)) {
                    draft.passthroughHotkeys.erase(draft.passthroughHotkeys.begin() + static_cast<std::ptrdiff_t>(i));
                    app->RequestHotkeySettings(draft);
                    break;
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    ImGui::Spacing();
    if (PaddedButton("New pass-through hotkey", 190.0f)) {
        passthroughCaptureOpen = true;
        passthroughCaptureReady = false;
        passthroughPending = {};
        hotkeys->BeginCapture();
        ImGui::OpenPopup("New pass-through hotkey");
    }
    if (!draft.passthroughHotkeys.empty()) {
        ImGui::SameLine();
    }
    if (!draft.passthroughHotkeys.empty() && PaddedButton("Reset pass-through list", 180.0f)) {
        HotkeySettings defaults = HotkeyManager::DefaultSettings();
        draft.passthroughHotkeys = defaults.passthroughHotkeys;
        app->RequestHotkeySettings(draft);
    }

    if (ImGui::BeginPopupModal("New pass-through hotkey", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Press the hotkey Clipboard++ should ignore and pass to Windows.");
        std::string preview = passthroughCaptureReady
            ? HotkeyManager::BindingText(passthroughPending)
            : hotkeys->CapturePreviewText();
        char previewBuf[128]{};
        strncpy_s(previewBuf, preview.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputText("##passthrough_capture_preview", previewBuf, sizeof(previewBuf),
                         ImGuiInputTextFlags_ReadOnly);

        if (!passthroughCaptureReady)
            ImGui::BeginDisabled();
        if (PaddedButton("Accept", 100.0f)) {
            const std::string text = HotkeyManager::BindingText(passthroughPending);
            auto exists = std::find_if(draft.passthroughHotkeys.begin(), draft.passthroughHotkeys.end(),
                [&](const std::string& existing) { return EqualsIgnoreCase(existing, text); });
            if (exists == draft.passthroughHotkeys.end()) {
                draft.passthroughHotkeys.push_back(text);
                app->RequestHotkeySettings(draft);
            }
            passthroughCaptureOpen = false;
            passthroughCaptureReady = false;
            passthroughPending = {};
            ImGui::CloseCurrentPopup();
        }
        if (!passthroughCaptureReady)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            hotkeys->CancelCapture();
            passthroughCaptureOpen = false;
            passthroughCaptureReady = false;
            passthroughPending = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (passthroughCaptureOpen && !passthroughCaptureReady) {
        hotkeys->CancelCapture();
        passthroughCaptureOpen = false;
    }

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
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
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
