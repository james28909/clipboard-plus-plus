#include "MainWindow.h"
#include "MainWindowInternal.h"

#include "../actions/CustomAction.h"
#include "../app/Application.h"
#include "../clipboard/ContentDetector.h"
#include "../hotkeys/HotkeyManager.h"
#include "../util/Win32Util.h"
#include "../../third_party/imgui/misc/cpp/imgui_stdlib.h"

#include <imgui.h>
#include <commdlg.h>
#include <algorithm>
#include <string>
#include <vector>

using namespace MainWindowInternal;

namespace {

struct ActionEditorState {
    bool editing{};
    bool importing{};
    bool capturingHotkey{};
    CustomActionDefinition draft;
    std::string sample{"First selected item\nSecond selected item"};
    std::string sampleApplication{"sample.exe"};
    std::string importPayload;
    std::string status;
    SettingsStatus statusKind{SettingsStatus::Muted};
};

ActionEditorState& State() {
    static ActionEditorState state;
    return state;
}

bool BeginForm(const char* id) {
    const float available = ImGui::GetContentRegionAvail().x;
    const float measuredLabelWidth = ImGui::CalcTextSize(
        "Required content tag (?)").x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const float labelWidth = std::clamp(
        measuredLabelWidth, S(235.0f), available * 0.38f);
    const float measuredControlWidth = ImGui::CalcTextSize(
        "External actions only").x + ImGui::GetFrameHeight() + S(190.0f);
    const float maximumControlWidth = std::max(
        S(260.0f), available - labelWidth - S(24.0f));
    const float controlWidth = std::clamp(
        measuredControlWidth, std::min(S(480.0f), maximumControlWidth),
        maximumControlWidth);
    if (!ImGui::BeginTable(id, 3,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
        return false;
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
    ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, controlWidth);
    ImGui::TableSetupColumn("Space", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void EndForm() {
    ImGui::EndTable();
}

void FormRow(const char* label, const char* help = nullptr) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        HelpTooltip(help);
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
}

template <typename Enum>
bool EnumCombo(const char* label, Enum& value, int count,
               const char* (*name)(Enum)) {
    bool changed = false;
    if (ImGui::BeginCombo(label, name(value))) {
        for (int i = 0; i < count; ++i) {
            const Enum candidate = static_cast<Enum>(i);
            if (ImGui::Selectable(name(candidate), candidate == value)) {
                value = candidate;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void StartNew(ActionEditorState& state) {
    state.draft = {};
    state.draft.label = "New action";
    state.draft.steps.push_back({CustomActionStepType::Trim});
    state.editing = true;
    state.importing = false;
    state.status.clear();
}

void StartEdit(ActionEditorState& state, const CustomActionDefinition& action,
               bool duplicate) {
    state.draft = action;
    if (duplicate) {
        state.draft.actionId = 0;
        state.draft.createdAtMs = 0;
        state.draft.updatedAtMs = 0;
        state.draft.label += " copy";
        state.draft.hotkeyEnabled = false;
    }
    state.editing = true;
    state.importing = false;
    state.status.clear();
}

CustomActionContext SampleContext(Application* app, const std::string& sample,
                                  const std::string& application,
                                  uint32_t tags) {
    CustomActionContext context;
    context.selectedTexts = SplitLines(sample.c_str());
    if (context.selectedTexts.empty()) context.selectedTexts.push_back(sample);
    context.allSelectedItemsAreText = true;
    context.callingApplication = application;
    context.combinedTags = tags;
    context.searchText = sample;
    context.windowsClipboard = sample;
    if (const ClipboardProfileConfig* profile = app->GetActiveClipboardProfile())
        context.activeProfile = profile->name;
    for (const NamedClipboardSlot& slot : app->GetNamedSlots())
        context.namedSlots.emplace_back(slot.name, slot.text);
    return context;
}

void DrawStepEditor(CustomActionStep& step) {
    if (!BeginForm("##step_fields")) return;
    switch (step.type) {
    case CustomActionStepType::Regex:
        FormRow("Pattern", "PCRE2 pattern applied to every input value.");
        ImGui::InputText("##regex_pattern", &step.value);
        FormRow("Replacement", "Replacement text may use capture groups such as $1 and $2.");
        ImGui::InputText("##regex_replacement", &step.replacement);
        FormRow("Matching options", "Control case, replacement count, and newline behavior.");
        ImGui::Checkbox("Case sensitive##regex", &step.caseSensitive);
        ImGui::SameLine(); ImGui::Checkbox("Replace all##regex", &step.replaceAll);
        ImGui::Checkbox("Multiline##regex", &step.multiline);
        ImGui::SameLine(); ImGui::Checkbox("Dot matches newline##regex", &step.dotMatchesNewline);
        break;
    case CustomActionStepType::Template:
        FormRow("Template", "Use {{1}}, {{2}}, {{slot:name}}, or {{all}} placeholders.");
        ImGui::InputTextMultiline("##template_body", &step.value, { -1.0f, 72.0f });
        break;
    case CustomActionStepType::Join:
        FormRow("Separator", "Text inserted between the ordered input values.");
        ImGui::InputText("##join_separator", &step.value);
        break;
    default:
        FormRow("Options");
        ImGui::TextDisabled("No additional options.");
        break;
    }
    EndForm();
}

void DrawOutputOptions(CustomActionDefinition& action) {
    switch (action.output) {
    case CustomActionOutput::OpenUrl:
        FormRow("URL", "Use {{text}}, {{profile}}, {{search}}, or {{clipboard}} placeholders.");
        ImGui::InputText("##action_output_url", &action.outputValue);
        break;
    case CustomActionOutput::SaveFile:
        FormRow("File path", "Choose a full path. Output placeholders are supported.");
        ImGui::SetNextItemWidth(-(ButtonWidthForText("Choose file") + ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputText("##action_output_file", &action.outputValue);
        ImGui::SameLine();
        if (PaddedButton("Choose file##action_output")) {
            wchar_t path[32768]{};
            const std::wstring current = win32util::Utf8ToWide(action.outputValue);
            wcsncpy_s(path, current.c_str(), _TRUNCATE);
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = Application::Get() ? Application::Get()->GetHwnd() : nullptr;
            dialog.lpstrFile = path;
            dialog.nMaxFile = static_cast<DWORD>(std::size(path));
            dialog.lpstrFilter = L"Text files\0*.txt\0All files\0*.*\0\0";
            dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (GetSaveFileNameW(&dialog))
                action.outputValue = win32util::WideToUtf8(path);
        }
        break;
    case CustomActionOutput::AddTag: {
        uint32_t selected = 0;
        try { selected = static_cast<uint32_t>(std::stoul(action.outputValue, nullptr, 0)); }
        catch (...) {}
        const char* preview = selected ? "Selected tag" : "Choose a tag";
        FormRow("Tag", "Tag every history item used as this action's input.");
        if (ImGui::BeginCombo("##action_output_tag", preview)) {
            for (ContentTag tag : kDisplayTagOrder) {
                const uint32_t mask = static_cast<uint32_t>(tag);
                if (ImGui::Selectable(ContentDetector::TagName(tag), selected == mask))
                    action.outputValue = std::to_string(mask);
            }
            ImGui::EndCombo();
        }
        break;
    }
    case CustomActionOutput::LaunchExecutable:
        FormRow("Executable", "Runs this program directly without a command shell.");
        ImGui::SetNextItemWidth(-(ButtonWidthForText("Choose executable") + ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputText("##action_output_executable", &action.outputValue);
        ImGui::SameLine();
        if (PaddedButton("Choose executable##action_output")) {
            char path[32768]{};
            strncpy_s(path, action.outputValue.c_str(), _TRUNCATE);
            if (PickExecutableFile(path, static_cast<DWORD>(std::size(path))))
                action.outputValue = path;
        }
        FormRow("Arguments", "Arguments stay separate from the executable. Inserted placeholders are Windows-escaped.");
        ImGui::InputText("##action_output_arguments", &action.executableArguments);
        break;
    default:
        break;
    }
}

void DrawActionEditor(Application* app, ActionEditorState& state) {
    CustomActionDefinition& action = state.draft;
    ImGui::SeparatorText(action.actionId ? "Edit action" : "New action");

    if (BeginForm("##action_identity_form")) {
        FormRow("Button label", "Text displayed on the popup workflow button.");
        ImGui::InputText("##action_label", &action.label);
        FormRow("Optional icon", "Short text or a font-supported symbol shown before the label.");
        ImGui::InputText("##action_icon", &action.icon);
        FormRow("Enabled", "Disabled actions remain saved but do not appear or respond to hotkeys.");
        ImGui::Checkbox("##action_enabled", &action.enabled);
        FormRow("Global hotkey", "Run this action while Clipboard++ is running, even when the popup is hidden.");
        ImGui::Checkbox("Enable##action_hotkey_enabled", &action.hotkeyEnabled);
        if (action.hotkeyEnabled) {
            ImGui::SameLine();
            const std::string binding = state.capturingHotkey
                ? "Press keys..." : HotkeyManager::BindingText(action.hotkey);
            if (PaddedButton((binding + "##custom_action_hotkey").c_str(), 190.0f)) {
                state.capturingHotkey = true;
                app->GetHotkeys()->BeginCapture();
            }
            ImGui::SameLine();
            if (PaddedButton("Clear##custom_action_hotkey")) {
                action.hotkey = {};
                state.capturingHotkey = false;
                app->GetHotkeys()->CancelCapture();
            }
        }
        FormRow("Popup group", "Choose whether the button appears under Actions or Destinations.");
        EnumCombo("##action_placement", action.placement, 2, CustomActionPlacementName);
        FormRow("Toolbar order", "Lower numbers appear before higher numbers in the chosen popup group.");
        ImGui::InputInt("##action_order", &action.toolbarOrder);
        FormRow("Input", "Choose the source data supplied to the first processing step.");
        EnumCombo("##action_input", action.input, 6, CustomActionInputName);
        if (action.input == CustomActionInput::NamedSlot) {
            FormRow("Named slot", "The saved named-slot value used as this action's input.");
            if (ImGui::BeginCombo("##action_named_slot", action.namedSlot.empty() ? "Choose..." : action.namedSlot.c_str())) {
                for (const NamedClipboardSlot& slot : app->GetNamedSlots())
                    if (ImGui::Selectable(slot.name.c_str(), action.namedSlot == slot.name))
                        action.namedSlot = slot.name;
                ImGui::EndCombo();
            }
        }
        EndForm();
    }

    ImGui::SeparatorText("Visibility");
    if (BeginForm("##action_visibility_form")) {
        FormRow("Text content", "Hide this button unless every selected history item contains text.");
        ImGui::Checkbox("Require text##action_visibility", &action.visibility.requireText);
        FormRow("Minimum selection", "Smallest number of selected items required for the button to appear.");
        ImGui::InputInt("##action_minimum_selection", &action.visibility.minimumSelection);
        FormRow("Maximum selection", "Largest allowed selection. Use 0 for no upper limit.");
        ImGui::InputInt("##action_maximum_selection", &action.visibility.maximumSelection);
        FormRow("Calling application", "Optional wildcard such as chatgpt.exe or *code*.exe. Blank matches every app.");
        ImGui::InputText("##action_application_pattern", &action.visibility.applicationPattern);
        FormRow("Required content tag", "Only show the button when selected items have this detected content type.");
        const char* requiredTagLabel = action.visibility.requiredTags == 0
            ? "Any content tag" : "Selected content tag";
        if (ImGui::BeginCombo("##action_required_tag", requiredTagLabel)) {
            if (ImGui::Selectable("Any content tag", action.visibility.requiredTags == 0))
                action.visibility.requiredTags = 0;
            for (ContentTag tag : kDisplayTagOrder) {
                const uint32_t mask = static_cast<uint32_t>(tag);
                if (ImGui::Selectable(ContentDetector::TagName(tag),
                                      action.visibility.requiredTags == mask))
                    action.visibility.requiredTags = mask;
            }
            ImGui::EndCombo();
        }
        EndForm();
    }

    ImGui::SeparatorText("Processing steps");
    int remove = -1, moveFrom = -1, moveTo = -1;
    for (int i = 0; i < static_cast<int>(action.steps.size()); ++i) {
        ImGui::PushID(i);
        CustomActionStep& step = action.steps[static_cast<size_t>(i)];
        ImGui::Text("%d. %s", i + 1, CustomActionStepName(step.type));
        ImGui::SameLine();
        if (PaddedButton("Up") && i > 0) { moveFrom = i; moveTo = i - 1; }
        ImGui::SameLine();
        if (PaddedButton("Down") && i + 1 < static_cast<int>(action.steps.size())) { moveFrom = i; moveTo = i + 1; }
        ImGui::SameLine();
        if (DangerButton("Remove")) remove = i;
        DrawStepEditor(step);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (moveFrom >= 0) std::swap(action.steps[moveFrom], action.steps[moveTo]);
    if (remove >= 0) action.steps.erase(action.steps.begin() + remove);
    static CustomActionStepType addType = CustomActionStepType::Trim;
    if (BeginForm("##add_step_form")) {
        FormRow("Add processing step", "Steps run from top to bottom and may be reordered afterward.");
        ImGui::SetNextItemWidth(-(ButtonWidthForText("Add step") + ImGui::GetStyle().ItemSpacing.x));
        EnumCombo("##add_action_step", addType, 10, CustomActionStepName);
        ImGui::SameLine();
        if (PaddedButton("Add step")) action.steps.push_back({addType});
        EndForm();
    }

    ImGui::SeparatorText("Output and safety");
    if (BeginForm("##action_output_form")) {
        FormRow("Output", "Choose what Clipboard++ does after all processing steps finish.");
        EnumCombo("##action_output", action.output, 10, CustomActionOutputName);
        DrawOutputOptions(action);
        FormRow("Confirmation", "External-only confirms URLs, file writes, and executable launches. Always confirms every output.");
        EnumCombo("##action_confirmation", action.confirmation, 3, CustomActionConfirmationName);
        FormRow("External timeout", "Maximum monitoring period for a launched executable, in milliseconds.");
        ImGui::InputInt("##action_timeout", &action.timeoutMs, 100, 1000);
        EndForm();
    }

    ImGui::SeparatorText("Safe preview");
    if (BeginForm("##action_preview_form")) {
        FormRow("Sample input", "Enter one selected value per line. Preview never runs the configured output.");
        ImGui::InputTextMultiline("##custom_action_sample", &state.sample, {-1.0f, 70.0f});
        FormRow("Sample application", "Process name used to test the calling-application visibility pattern.");
        ImGui::InputText("##sample_calling_app", &state.sampleApplication);
        EndForm();
    }
    const CustomActionContext sampleContext = SampleContext(
        app, state.sample, state.sampleApplication,
        action.visibility.requiredTags);
    const CustomActionPreparation preview = PrepareCustomAction(action, sampleContext);
    if (preview.ok) {
        if (BeginForm("##action_preview_output_form")) {
            FormRow("Preview output", "The processed result only; no output action is performed here.");
            ImGui::BeginChild("##action_preview", {-1.0f, 80.0f}, ImGuiChildFlags_Borders);
            ImGui::TextWrapped("%s", preview.output.c_str());
            ImGui::EndChild();
            EndForm();
        }
    } else {
        StatusMessage(SettingsStatus::Warning, preview.error.c_str());
    }

    const std::string validation = ValidateCustomAction(action);
    if (!validation.empty()) StatusMessage(SettingsStatus::Error, validation.c_str());
    if (!validation.empty()) ImGui::BeginDisabled();
    if (BlueButton("Save action", 110.0f)) {
        if (app->SaveCustomAction(action)) {
            state.editing = false;
            state.status = "Action saved in the encrypted profile database.";
            state.statusKind = SettingsStatus::Success;
        } else {
            state.status = "The action could not be saved.";
            state.statusKind = SettingsStatus::Error;
        }
    }
    if (!validation.empty()) ImGui::EndDisabled();
    ImGui::SameLine();
    if (PaddedButton("Test safely", 110.0f)) {
        state.status = preview.ok
            ? "Safe test passed. Nothing was pasted, opened, saved, or launched."
            : "Safe test failed: " + preview.error;
        state.statusKind = preview.ok ? SettingsStatus::Success : SettingsStatus::Error;
    }
    ImGui::SameLine();
    if (PaddedButton("Cancel", 90.0f)) {
        state.editing = false;
        if (state.capturingHotkey) app->GetHotkeys()->CancelCapture();
        state.capturingHotkey = false;
    }
}

} // namespace

void MainWindow::DrawCustomActions() {
    Application* app = Application::Get();
    if (!app) return;
    ActionEditorState& state = State();

    if (state.capturingHotkey) {
        KeyBinding captured;
        if (app->GetHotkeys()->ConsumeCapturedBinding(captured)) {
            captured.action = HotkeyAction::RunCustomAction;
            state.draft.hotkey = captured;
            state.capturingHotkey = false;
        } else if (!app->GetHotkeys()->IsCapturing()) {
            state.capturingHotkey = false;
        }
    }

    if (BeginSettingsCard("##custom_workflow_actions", "Workflow buttons",
                          "Create conditional, ordered actions for the popup Actions and Destinations groups.")) {
        if (!state.editing) {
            if (BlueButton("New action", 105.0f)) StartNew(state);
            ImGui::SameLine();
            if (PaddedButton(state.importing ? "Close import" : "Import JSON", 105.0f))
                state.importing = !state.importing;
            ImGui::TextDisabled("Action definitions, templates, arguments, and paths are encrypted at rest.");

            if (state.importing) {
                StatusMessage(SettingsStatus::Warning,
                    "Imported and exported JSON is plaintext and may contain secrets. Review it before sharing.");
                ImGui::InputTextMultiline("##action_import", &state.importPayload,
                                          {-1.0f, 110.0f});
                if (BlueButton("Import", 90.0f)) {
                    std::string error;
                    if (app->ImportCustomAction(state.importPayload, &error)) {
                        state.importPayload.clear();
                        state.importing = false;
                        state.status = "Action imported into encrypted storage.";
                        state.statusKind = SettingsStatus::Success;
                    } else {
                        state.status = error;
                        state.statusKind = SettingsStatus::Error;
                    }
                }
            }

            const std::vector<CustomActionDefinition> actions = app->GetCustomActions();
            if (actions.empty()) {
                EmptyState("No workflow buttons yet. Create one to transform, route, paste, or launch with selected clipboard data.");
            } else if (BeginSettingsTable("##custom_actions_table", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthStretch, 1.70f);
                ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthStretch, 0.85f);
                ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 1.30f);
                ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch, 1.55f);
                ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthStretch, 2.35f);
                ImGui::TableHeadersRow();
                for (const CustomActionDefinition& action : actions) {
                    ImGui::PushID(static_cast<int>(action.actionId));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(action.label.c_str());
                    if (!action.enabled) { ImGui::SameLine(); ImGui::TextDisabled("(disabled)"); }
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(CustomActionPlacementName(action.placement));
                    ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(CustomActionInputName(action.input));
                    ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(CustomActionOutputName(action.output));
                    ImGui::TableSetColumnIndex(4);
                    if (PaddedButton("Edit")) StartEdit(state, action, false);
                    ImGui::SameLine();
                    if (PaddedButton("Duplicate")) StartEdit(state, action, true);
                    ImGui::SameLine();
                    if (PaddedButton("Export")) {
                        app->CopyTextToClipboard(SerializeCustomAction(action, true));
                        state.status = "Plaintext action JSON copied. It may contain secrets; share carefully.";
                        state.statusKind = SettingsStatus::Warning;
                    }
                    ImGui::SameLine();
                    if (DangerButton("Delete")) app->DeleteCustomAction(action.actionId);
                    ImGui::PopID();
                }
                EndSettingsTable();
            }
        } else {
            DrawActionEditor(app, state);
        }
        if (!state.status.empty()) StatusMessage(state.statusKind, state.status.c_str());
    }
    EndSettingsCard();
}
