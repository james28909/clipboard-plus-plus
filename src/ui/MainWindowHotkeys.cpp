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
#include <initializer_list>
#include <sstream>
#include <string>


using namespace MainWindowInternal;

// -- Section: Hotkeys ---------------------------------------------------------

void MainWindow::DrawHotkeys() {
    Application* app = Application::Get();
    if (!app) return;
    HotkeyManager* hotkeys = app->GetHotkeys();
    if (!hotkeys) return;

    PageHeader("Hotkeys", "Customize global actions, direct-paste slots, and keys the popup should leave to Windows.");

    // Keep Hotkeys controls theme-aware without changing the application's
    // normal compact button and checkbox dimensions.
    const AppearanceSettings currentAppearance = app->GetAppearance();
    const AppearanceSettings hotkeyTheme = currentAppearance.customColors
        ? currentAppearance : ThemeDefaults(currentAppearance.theme);
    const ImVec2 hotkeyGridCellPadding = ImGui::GetStyle().CellPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, hotkeyGridCellPadding);
    ImGui::PushStyleColor(ImGuiCol_Button, hotkeyTheme.buttonOff);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hotkeyTheme.hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, hotkeyTheme.buttonOn);

    auto uniformButtonWidth = [](std::initializer_list<const char*> labels,
                                 float extraPadding = 8.0f) {
        float widest = 0.0f;
        for (const char* label : labels)
            widest = std::max(widest, ImGui::CalcTextSize(label, nullptr, true).x);
        return std::ceil(widest + ImGui::GetStyle().FramePadding.x * 2.0f +
                         extraPadding);
    };
    const float hotkeyGridRowHeight = ImGui::GetFrameHeight();
    auto gridHeaders = [&](std::initializer_list<const char*> labels) {
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers, hotkeyGridRowHeight);
        int column = 0;
        for (const char* label : labels) {
            ImGui::TableSetColumnIndex(column++);
            ImGui::AlignTextToFramePadding();
            ImGui::TableHeader(label);
        }
    };
    auto gridTextColumn = [](int column) {
        ImGui::TableSetColumnIndex(column);
        ImGui::AlignTextToFramePadding();
    };

    static HotkeySettings draft = app->GetHotkeySettings();
    static bool initialized = false;
    static int captureIndex = -1;
    static int bankCaptureIndex = -1;
    static int64_t namedCaptureSlotId = 0;
    static std::string namedCaptureStatus;
    static bool namedSlotEditorOpen = false;
    static int64_t editingNamedSlotId = 0;
    static int64_t pendingNamedSlotDeleteId = 0;
    static char namedSlotName[128]{};
    static char namedSlotText[16384]{};
    static bool passthroughCaptureOpen = false;
    static bool passthroughCaptureReady = false;
    static KeyBinding passthroughPending{};
    if (!initialized) {
        draft = app->GetHotkeySettings();
        initialized = true;
    }
    if (captureIndex < 0 && bankCaptureIndex < 0 && namedCaptureSlotId == 0 &&
        !passthroughCaptureOpen && !hotkeys->IsCapturing())
        draft = app->GetHotkeySettings();

    auto bankForIndex = [&](int index) -> SlotBankSettings* {
        switch (index) {
        case 0: return &draft.popupHistoryBank;
        case 1: return &draft.globalHistoryBank;
        case 2: return &draft.pinnedHistoryBank;
        case 3: return &draft.profileBank;
        default: return nullptr;
        }
    };

    auto centerInTableCell = [](float itemWidth) {
        const float available = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max(0.0f, (available - itemWidth) * 0.5f));
    };
    auto centeredTableHeader = [&](const char* label) {
        const float textWidth = ImGui::CalcTextSize(label).x;
        centerInTableCell(textWidth);
        ImGui::TextDisabled("%s", label);
    };

    KeyBinding captured;
    if (hotkeys->ConsumeCapturedBinding(captured)) {
        if (passthroughCaptureOpen) {
            passthroughPending = captured;
            passthroughCaptureReady = true;
        } else if (namedCaptureSlotId != 0) {
            const bool conflict = std::any_of(
                draft.bindings.begin(), draft.bindings.end(),
                [&](const KeyBinding& binding) {
                    return !(binding.action == HotkeyAction::PasteNamedSlot &&
                             binding.data == static_cast<int>(namedCaptureSlotId)) &&
                           binding.Overlaps(captured);
                });
            if (conflict) {
                namedCaptureStatus = "That chord is already assigned to another explicit action.";
            } else {
                draft.bindings.erase(std::remove_if(
                    draft.bindings.begin(), draft.bindings.end(),
                    [&](const KeyBinding& binding) {
                        return binding.action == HotkeyAction::PasteNamedSlot &&
                               binding.data == static_cast<int>(namedCaptureSlotId);
                    }), draft.bindings.end());
                captured.action = HotkeyAction::PasteNamedSlot;
                captured.data = static_cast<int>(namedCaptureSlotId);
                draft.bindings.push_back(captured);
                app->RequestHotkeySettings(draft);
                namedCaptureStatus = "Named-slot shortcut saved. It overrides matching slot banks.";
            }
            namedCaptureSlotId = 0;
        } else if (bankCaptureIndex >= 0) {
            if (SlotBankSettings* bank = bankForIndex(bankCaptureIndex)) {
                captured.vkey = 0;
                captured.action = HotkeyAction::None;
                captured.data = 0;
                bank->chord = captured;
                app->RequestHotkeySettings(draft);
            }
            bankCaptureIndex = -1;
        } else if (captureIndex >= 0 &&
                   static_cast<size_t>(captureIndex) < draft.bindings.size()) {
            captured.action = draft.bindings[static_cast<size_t>(captureIndex)].action;
            captured.data = draft.bindings[static_cast<size_t>(captureIndex)].data;
            draft.bindings[static_cast<size_t>(captureIndex)] = captured;
            app->RequestHotkeySettings(draft);
            captureIndex = -1;
        }
    }

    if (BeginSettingsCard("##hotkeys_global", "Application shortcuts",
                          "Click a binding to capture a replacement. Conflicts are highlighted immediately.")) {
        float bindingButtonW = uniformButtonWidth({"Press keys..."});
        for (const KeyBinding& binding : draft.bindings) {
            if (binding.action != HotkeyAction::PasteNamedSlot)
                bindingButtonW = std::max(bindingButtonW,
                    uniformButtonWidth({HotkeyManager::BindingText(binding).c_str()}));
        }
        const float actionButtonW = uniformButtonWidth({"Reset"});
        const float tableCellPadding = ImGui::GetStyle().CellPadding.x * 2.0f;
        if (BeginSettingsTable("##hotkeys", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthFixed,
                                    bindingButtonW + tableCellPadding);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed,
                                    actionButtonW + tableCellPadding);
            gridHeaders({"Function", "Binding", "Action"});
            for (size_t i = 0; i < draft.bindings.size(); ++i) {
                const KeyBinding& binding = draft.bindings[i];
                if (binding.action == HotkeyAction::PasteNamedSlot)
                    continue;
                const bool conflict = BindingHasConflict(draft, i);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, hotkeyGridRowHeight);
                gridTextColumn(0);
                std::string functionName = HotkeyManager::ActionName(binding.action);
                if (binding.action == HotkeyAction::PasteNamedSlot) {
                    const auto slots = app->GetNamedSlots();
                    auto slot = std::find_if(slots.begin(), slots.end(),
                        [&](const NamedClipboardSlot& value) {
                            return value.slotId == binding.data;
                        });
                    if (slot != slots.end())
                        functionName += ": " + slot->name;
                }
                ImGui::TextUnformatted(functionName.c_str());
                if (conflict) {
                    ImGui::SameLine();
                    ImGui::TextColored({1.0f, 0.45f, 0.25f, 1.0f}, "Conflict");
                }

                ImGui::TableSetColumnIndex(1);
                const std::string label = captureIndex == static_cast<int>(i)
                    ? "Press keys...##capture" + std::to_string(i)
                    : HotkeyManager::BindingText(binding) + "##capture" + std::to_string(i);
                if (ImGui::Button(label.c_str(), {bindingButtonW, 0.0f})) {
                    captureIndex = static_cast<int>(i);
                    hotkeys->BeginCapture();
                }

                ImGui::TableSetColumnIndex(2);
                const bool removable = binding.action == HotkeyAction::PasteNamedSlot;
                const std::string resetId = std::string(removable ? "Remove" : "Reset") +
                    "##resetHotkey" + std::to_string(i);
                if (ImGui::Button(resetId.c_str(), {actionButtonW, 0.0f})) {
                    if (removable) {
                        draft.bindings.erase(draft.bindings.begin() +
                                             static_cast<std::ptrdiff_t>(i));
                        app->RequestHotkeySettings(draft);
                        break;
                    }
                    HotkeySettings defaults = HotkeyManager::DefaultSettings();
                    auto it = std::find_if(defaults.bindings.begin(), defaults.bindings.end(),
                        [&](const KeyBinding& b) { return b.action == binding.action; });
                    if (it != defaults.bindings.end()) {
                        draft.bindings[i] = *it;
                        app->RequestHotkeySettings(draft);
                    }
                }
            }
            EndSettingsTable();
        }

        if (captureIndex >= 0) {
            ImGui::TextDisabled("Press Esc to cancel capture.");
            if (!hotkeys->IsCapturing())
                captureIndex = -1;
        }
    }
    EndSettingsCard();

    if (BeginSettingsCard("##hotkeys_slot_banks", "Slot banks",
                          "Assign an exact physical modifier chord to each numbered list. The checkboxes choose which slot-key ranges use that chord.")) {
        bool changed = false;
        changed |= ImGui::Checkbox("Enable hotkey double taps",
                                   &draft.hotkeyDoubleTaps);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("When a named-slot shortcut overlaps a bank: release a required modifier for the bank action, or press the same slot key again while holding the modifiers for the named slot.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        const char* names[] = {
            "Popup history", "Global history", "Pinned history", "Clipboard profiles"};
        const char* scopes[] = {
            "Popup only", "Always", "Always", "Always"};
        HotkeySettings defaults = HotkeyManager::DefaultSettings();
        auto defaultBank = [&](int index) -> const SlotBankSettings& {
            switch (index) {
            case 0: return defaults.popupHistoryBank;
            case 1: return defaults.globalHistoryBank;
            case 2: return defaults.pinnedHistoryBank;
            default: return defaults.profileBank;
            }
        };
        auto banksConflict = [&](int index) {
            SlotBankSettings* a = bankForIndex(index);
            if (!a || !a->enabled) return false;
            for (int other = 0; other < 4; ++other) {
                if (other == index) continue;
                SlotBankSettings* b = bankForIndex(other);
                if (!b || !b->enabled) continue;
                KeyBinding aChord = a->chord;
                KeyBinding bChord = b->chord;
                aChord.vkey = bChord.vkey = '1';
                const bool keyOverlap =
                    (a->numberKeys && b->numberKeys) ||
                    (a->letterKeys && b->letterKeys) ||
                    (a->functionKeys && b->functionKeys);
                if (keyOverlap && aChord.Overlaps(bChord))
                    return true;
            }
            return false;
        };
        float chordButtonW = uniformButtonWidth({"Press and release modifiers..."});
        for (int i = 0; i < 4; ++i) {
            if (SlotBankSettings* bank = bankForIndex(i))
                chordButtonW = std::max(chordButtonW,
                    uniformButtonWidth({HotkeyManager::ModifierChordText(bank->chord).c_str()}));
        }
        const float bankActionButtonW = uniformButtonWidth({"Reset"});
        const float bankCellPadding = ImGui::GetStyle().CellPadding.x * 2.0f;
        if (BeginSettingsTable("##slot_bank_table", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("List", ImGuiTableColumnFlags_WidthFixed, 230.0f);
            ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Modifier chord", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Slot keys", ImGuiTableColumnFlags_WidthFixed, 250.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed,
                                    bankActionButtonW + bankCellPadding);
            gridHeaders({"List", "Scope", "Modifier chord", "Slot keys", "Action"});
            for (int i = 0; i < 4; ++i) {
                SlotBankSettings* bank = bankForIndex(i);
                if (!bank) continue;
                ImGui::PushID(i);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, hotkeyGridRowHeight);
                ImGui::TableSetColumnIndex(0);
                changed |= ImGui::Checkbox("##enabled", &bank->enabled);
                ImGui::SameLine();
                ImGui::TextUnformatted(names[i]);
                if (banksConflict(i)) {
                    ImGui::SameLine();
                    ImGui::TextColored({1.0f, 0.45f, 0.25f, 1.0f}, "Conflict");
                }
                gridTextColumn(1);
                ImGui::TextDisabled("%s", scopes[i]);
                ImGui::TableSetColumnIndex(2);
                const std::string chordLabel = bankCaptureIndex == i
                    ? "Press and release modifiers...##bank_chord"
                    : HotkeyManager::ModifierChordText(bank->chord) + "##bank_chord";
                centerInTableCell(chordButtonW);
                if (ImGui::Button(chordLabel.c_str(), {chordButtonW, 0.0f})) {
                    bankCaptureIndex = i;
                    captureIndex = -1;
                    hotkeys->BeginModifierCapture();
                }
                ImGui::TableSetColumnIndex(3);
                changed |= ImGui::Checkbox("1-9", &bank->numberKeys);
                ImGui::SameLine();
                changed |= ImGui::Checkbox("A-Z", &bank->letterKeys);
                ImGui::SameLine();
                changed |= ImGui::Checkbox("F1-F12", &bank->functionKeys);
                ImGui::TableSetColumnIndex(4);
                if (ImGui::Button("Reset", {bankActionButtonW, 0.0f})) {
                    *bank = defaultBank(i);
                    changed = true;
                }
                ImGui::PopID();
            }
            EndSettingsTable();
        }
        if (bankCaptureIndex >= 0) {
            ImGui::TextDisabled("Press any combination of Left/Right Ctrl, Shift, or Alt, then release the modifiers to save. Esc cancels.");
            if (!hotkeys->IsCapturing())
                bankCaptureIndex = -1;
        }
        ImGui::TextDisabled("Explicit application and named-slot shortcuts override matching bank routes.");
        if (changed)
            app->RequestHotkeySettings(draft);
    }
    EndSettingsCard();

    if (BeginSettingsCard("##hotkeys_named_slots", "Named-slot shortcuts",
                          "Create reusable text slots and give each one an exact global shortcut.")) {
        const std::vector<NamedClipboardSlot> slots = app->GetNamedSlots();
        auto openNamedSlotEditor = [&](const NamedClipboardSlot* slot) {
            namedSlotEditorOpen = true;
            editingNamedSlotId = slot ? slot->slotId : 0;
            std::snprintf(namedSlotName, sizeof(namedSlotName), "%s",
                          slot ? slot->name.c_str() : "");
            std::snprintf(namedSlotText, sizeof(namedSlotText), "%s",
                          slot ? slot->text.c_str() : "");
            namedCaptureStatus.clear();
        };

        bool requestNamedSlotDelete = false;
        float namedShortcutButtonW = uniformButtonWidth(
            {"Set shortcut", "Press shortcut..."});
        for (const KeyBinding& binding : draft.bindings) {
            if (binding.action == HotkeyAction::PasteNamedSlot) {
                const std::string label = HotkeyManager::BindingText(binding) + " x2";
                namedShortcutButtonW = std::max(namedShortcutButtonW,
                                                uniformButtonWidth({label.c_str()}));
            }
        }
        const float namedActionButtonW = uniformButtonWidth({"Edit", "Clear", "Delete"});
        const float namedCellPadding = ImGui::GetStyle().CellPadding.x * 2.0f;
        const float namedActionColumnW = namedActionButtonW * 3.0f +
            ImGui::GetStyle().ItemSpacing.x * 2.0f + namedCellPadding;
        if (slots.empty()) {
            EmptyState("No named slots yet. Create one below to assign its content and shortcut.");
        } else if (BeginSettingsTable("##named_slot_hotkeys", 4,
                                     ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Named slot", ImGuiTableColumnFlags_WidthFixed, 165.0f);
            ImGui::TableSetupColumn("Contents", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed,
                                    namedShortcutButtonW + namedCellPadding);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed,
                                    namedActionColumnW);
            gridHeaders({"Named slot", "Contents", "Shortcut", "Actions"});
            for (const NamedClipboardSlot& slot : slots) {
                auto assigned = std::find_if(draft.bindings.begin(), draft.bindings.end(),
                    [&](const KeyBinding& binding) {
                        return binding.action == HotkeyAction::PasteNamedSlot &&
                               binding.data == static_cast<int>(slot.slotId);
                    });
                ImGui::PushID(static_cast<int>(slot.slotId));
                ImGui::TableNextRow(ImGuiTableRowFlags_None, hotkeyGridRowHeight);
                gridTextColumn(0);
                ImGui::TextUnformatted(slot.name.c_str());
                gridTextColumn(1);
                std::string preview = slot.text;
                std::replace(preview.begin(), preview.end(), '\n', ' ');
                std::replace(preview.begin(), preview.end(), '\r', ' ');
                if (preview.size() > 80)
                    preview = preview.substr(0, 77) + "...";
                ImGui::TextDisabled("%s", preview.empty() ? "(empty)" : preview.c_str());
                if (ImGui::IsItemHovered() && !slot.text.empty())
                    ImGui::SetTooltip("%s", slot.text.c_str());
                ImGui::TableSetColumnIndex(2);
                bool doubleRoute = false;
                if (draft.hotkeyDoubleTaps && assigned != draft.bindings.end() &&
                    assigned->exactModifiers && assigned->physicalModifiers != 0) {
                    int overlapSlot = -1;
                    doubleRoute = HotkeyManager::ResolveSlotBank(
                        draft, ModifierState::FromMask(assigned->physicalModifiers),
                        assigned->vkey, true, overlapSlot) != HotkeyAction::None;
                }
                const std::string label = namedCaptureSlotId == slot.slotId
                    ? "Press shortcut...##named_capture"
                    : (assigned == draft.bindings.end()
                        ? "Set shortcut##named_capture"
                        : HotkeyManager::BindingText(*assigned) +
                          (doubleRoute ? " x2" : "") +
                          "##named_capture");
                if (ImGui::Button(label.c_str(), {namedShortcutButtonW, 0.0f})) {
                    namedCaptureSlotId = slot.slotId;
                    captureIndex = -1;
                    bankCaptureIndex = -1;
                    namedCaptureStatus.clear();
                    hotkeys->BeginCapture();
                }
                ImGui::TableSetColumnIndex(3);
                if (ImGui::Button("Edit", {namedActionButtonW, 0.0f}))
                    openNamedSlotEditor(&slot);
                ImGui::SameLine();
                if (assigned != draft.bindings.end() &&
                    ImGui::Button("Clear", {namedActionButtonW, 0.0f})) {
                    draft.bindings.erase(assigned);
                    app->RequestHotkeySettings(draft);
                    namedCaptureStatus = "Named-slot shortcut removed.";
                    ImGui::PopID();
                    break;
                }
                if (assigned == draft.bindings.end()) {
                    ImGui::BeginDisabled();
                    ImGui::Button("Clear", {namedActionButtonW, 0.0f});
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete", {namedActionButtonW, 0.0f})) {
                    pendingNamedSlotDeleteId = slot.slotId;
                    requestNamedSlotDelete = true;
                }
                ImGui::PopID();
            }
            EndSettingsTable();
        }

        if (requestNamedSlotDelete)
            ImGui::OpenPopup("Delete named slot from hotkeys?");
        if (ImGui::BeginPopupModal("Delete named slot from hotkeys?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Delete this named slot and its shortcut permanently?");
            ImGui::Spacing();
            const float deleteModalButtonW = uniformButtonWidth({"Delete", "Cancel"});
            if (ImGui::Button("Delete", {deleteModalButtonW, 0.0f})) {
                const bool deleted = app->DeleteNamedSlot(pendingNamedSlotDeleteId);
                namedCaptureStatus = deleted ? "Named slot deleted."
                                             : "Could not delete the named slot.";
                if (deleted) {
                    draft.bindings.erase(std::remove_if(
                        draft.bindings.begin(), draft.bindings.end(),
                        [&](const KeyBinding& binding) {
                            return binding.action == HotkeyAction::PasteNamedSlot &&
                                   binding.data == static_cast<int>(pendingNamedSlotDeleteId);
                        }), draft.bindings.end());
                    if (editingNamedSlotId == pendingNamedSlotDeleteId) {
                        namedSlotEditorOpen = false;
                        editingNamedSlotId = 0;
                    }
                }
                pendingNamedSlotDeleteId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {deleteModalButtonW, 0.0f})) {
                pendingNamedSlotDeleteId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        if (!namedSlotEditorOpen) {
            const float newSlotButtonW = uniformButtonWidth({"New named slot"});
            if (ImGui::Button("New named slot", {newSlotButtonW, 0.0f}))
                openNamedSlotEditor(nullptr);
        } else {
            ImGui::SeparatorText(editingNamedSlotId == 0
                ? "New named slot" : "Edit named slot");
            ImGui::TextUnformatted("Name");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##hotkey_named_slot_name", namedSlotName,
                             sizeof(namedSlotName));
            ImGui::TextUnformatted("Text to paste");
            ImGui::InputTextMultiline("##hotkey_named_slot_text", namedSlotText,
                                      sizeof(namedSlotText), {-1.0f, 110.0f});
            const char* saveLabel = editingNamedSlotId == 0
                ? "Save and set shortcut" : "Save changes";
            const float editorButtonW = uniformButtonWidth(
                {"Save and set shortcut", "Save changes", "Cancel"});
            if (ImGui::Button(saveLabel, {editorButtonW, 0.0f})) {
                std::string name = namedSlotName;
                const size_t first = name.find_first_not_of(" \t\r\n");
                const size_t last = name.find_last_not_of(" \t\r\n");
                name = first == std::string::npos ? std::string{} :
                    name.substr(first, last - first + 1);
                if (name.empty()) {
                    namedCaptureStatus = "A named-slot name is required.";
                } else {
                    NamedClipboardSlot slot;
                    slot.slotId = editingNamedSlotId;
                    slot.name = std::move(name);
                    slot.text = namedSlotText;
                    const bool wasNew = slot.slotId == 0;
                    if (!app->SaveNamedSlot(slot)) {
                        namedCaptureStatus =
                            "Could not save the named slot. Names must be unique.";
                    } else {
                        namedSlotEditorOpen = false;
                        editingNamedSlotId = 0;
                        if (wasNew) {
                            namedCaptureSlotId = slot.slotId;
                            namedCaptureStatus = "Named slot saved. Press its shortcut now.";
                            captureIndex = -1;
                            bankCaptureIndex = -1;
                            hotkeys->BeginCapture();
                        } else {
                            namedCaptureStatus = "Named slot updated.";
                        }
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {editorButtonW, 0.0f})) {
                namedSlotEditorOpen = false;
                editingNamedSlotId = 0;
                namedCaptureStatus.clear();
            }
        }
        if (namedCaptureSlotId != 0 && !hotkeys->IsCapturing())
            namedCaptureSlotId = 0;
        if (!namedCaptureStatus.empty())
            ImGui::TextDisabled("%s", namedCaptureStatus.c_str());
    }
    EndSettingsCard();

    if (BeginSettingsCard("##hotkeys_effective_routes", "Effective routes",
                          "Review the active resolver order. Explicit shortcuts win over banks; higher banks win when bank definitions overlap.")) {
        if (BeginSettingsTable("##effective_hotkey_routes", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("Route", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Chord", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Keys / scope", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            gridHeaders({"Priority", "Route", "Chord", "Keys / scope"});
            const std::vector<NamedClipboardSlot> slots = app->GetNamedSlots();
            for (const KeyBinding& binding : draft.bindings) {
                if (binding.vkey == 0) continue;
                std::string route = HotkeyManager::ActionName(binding.action);
                if (binding.action == HotkeyAction::PasteNamedSlot) {
                    auto slot = std::find_if(slots.begin(), slots.end(),
                        [&](const NamedClipboardSlot& value) {
                            return value.slotId == binding.data;
                        });
                    if (slot != slots.end())
                        route += ": " + slot->name;
                }
                std::string chord = HotkeyManager::BindingText(binding);
                if (draft.hotkeyDoubleTaps &&
                    binding.action == HotkeyAction::PasteNamedSlot &&
                    binding.exactModifiers && binding.physicalModifiers != 0) {
                    int overlapSlot = -1;
                    if (HotkeyManager::ResolveSlotBank(
                            draft, ModifierState::FromMask(binding.physicalModifiers),
                            binding.vkey, true, overlapSlot) != HotkeyAction::None)
                        chord += " x2";
                }
                ImGui::TableNextRow(ImGuiTableRowFlags_None, hotkeyGridRowHeight);
                gridTextColumn(0); ImGui::TextDisabled("1");
                gridTextColumn(1); ImGui::TextUnformatted(route.c_str());
                gridTextColumn(2);
                ImGui::TextUnformatted(chord.c_str());
                gridTextColumn(3); ImGui::TextDisabled("Explicit / always");
            }
            const char* bankNames[] = {
                "Global history bank", "Pinned history bank",
                "Clipboard profile bank", "Popup history bank"};
            const SlotBankSettings* banks[] = {
                &draft.globalHistoryBank, &draft.pinnedHistoryBank,
                &draft.profileBank, &draft.popupHistoryBank};
            for (int i = 0; i < 4; ++i) {
                const SlotBankSettings& bank = *banks[i];
                if (!bank.enabled) continue;
                std::string keys;
                if (bank.numberKeys) keys += "1-9";
                if (bank.letterKeys) keys += (keys.empty() ? "" : ", ") + std::string("A-Z");
                if (bank.functionKeys) keys += (keys.empty() ? "" : ", ") + std::string("F1-F12");
                if (i == 3) keys += " / popup only";
                else keys += " / always";
                ImGui::TableNextRow(ImGuiTableRowFlags_None, hotkeyGridRowHeight);
                gridTextColumn(0); ImGui::TextDisabled("%d", i + 2);
                gridTextColumn(1); ImGui::TextUnformatted(bankNames[i]);
                gridTextColumn(2);
                ImGui::TextUnformatted(HotkeyManager::ModifierChordText(bank.chord).c_str());
                gridTextColumn(3); ImGui::TextDisabled("%s", keys.c_str());
            }
            EndSettingsTable();
        }
    }
    EndSettingsCard();

    if (BeginSettingsCard("##hotkeys_passthrough", "Popup pass-through",
                          "These combinations bypass Clipboard++ while the popup is open and continue to Windows.")) {
      if (draft.passthroughHotkeys.empty()) {
          EmptyState("No pass-through hotkeys defined.");
      } else {
        const float removeButtonW = ButtonWidthForText("Remove", 84.0f);
        const float actionColumnW = removeButtonW + hotkeyGridCellPadding.x * 2.0f;
        if (BeginSettingsTable("##popup_passthrough_table", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Hotkey", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, actionColumnW);
            gridHeaders({"Hotkey", "Action"});
            for (size_t i = 0; i < draft.passthroughHotkeys.size(); ++i) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, hotkeyGridRowHeight);
                gridTextColumn(0);
                ImGui::TextUnformatted(draft.passthroughHotkeys[i].c_str());
                ImGui::TableSetColumnIndex(1);
                const std::string removeId = "Remove##passthrough_" + std::to_string(i);
                centerInTableCell(removeButtonW);
                if (DangerButton(removeId.c_str(), removeButtonW)) {
                    draft.passthroughHotkeys.erase(draft.passthroughHotkeys.begin() + static_cast<std::ptrdiff_t>(i));
                    app->RequestHotkeySettings(draft);
                    break;
                }
            }
            EndSettingsTable();
        }
      }

    const float passThroughButtonW = uniformButtonWidth(
        {"Add hotkey", "Restore defaults"});
    if (ImGui::Button("Add hotkey", {passThroughButtonW, 0.0f})) {
        passthroughCaptureOpen = true;
        passthroughCaptureReady = false;
        passthroughPending = {};
        hotkeys->BeginCapture();
        ImGui::OpenPopup("New pass-through hotkey");
    }
    if (!draft.passthroughHotkeys.empty())
        SameLineIfFits(passThroughButtonW);
    if (!draft.passthroughHotkeys.empty() &&
        ImGui::Button("Restore defaults", {passThroughButtonW, 0.0f})) {
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
        const float captureModalButtonW = uniformButtonWidth({"Accept", "Cancel"});
        if (ImGui::Button("Accept", {captureModalButtonW, 0.0f})) {
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
        if (ImGui::Button("Cancel", {captureModalButtonW, 0.0f}) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
    }
    EndSettingsCard();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}
