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
#include "../transforms/RegexTransform.h"
#include "../templates/PasteTemplate.h"
#include "Appearance.h"
#include "PopupWindow.h"
#include <imgui.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>


using namespace MainWindowInternal;

// -- Clipboard paste tools ----------------------------------------------------

void MainWindow::DrawPasteTools() {
    Application* app = Application::Get();
    if (!app) return;

    if (BeginSettingsCard("##developer_transforms", "Regex transforms",
                          "Named PCRE2 pattern/replacement rules applied explicitly before paste.")) {
        static bool transformsLoaded = false;
        static std::vector<RegexTransformDefinition> transforms;
        static bool transformEditorOpen = false;
        static int64_t editingTransformId = 0;
        static int64_t pendingTransformDeleteId = 0;
        static char transformName[128]{};
        static char transformPattern[4096]{};
        static char transformReplacement[4096]{};
        static char transformSample[8192]{};
        static bool transformCaseSensitive = true;
        static bool transformMultiline = false;
        static bool transformDotAll = false;
        static bool transformReplaceAll = true;
        static std::string transformStatus;
        static bool transformStatusError = false;

        auto reloadTransforms = [&]() {
            transforms = app->GetRegexTransforms();
            transformsLoaded = true;
        };
        auto openTransformEditor = [&](const RegexTransformDefinition* value) {
            transformEditorOpen = true;
            editingTransformId = value ? value->transformId : 0;
            std::snprintf(transformName, sizeof(transformName), "%s",
                          value ? value->name.c_str() : "");
            std::snprintf(transformPattern, sizeof(transformPattern), "%s",
                          value ? value->pattern.c_str() : "");
            std::snprintf(transformReplacement, sizeof(transformReplacement), "%s",
                          value ? value->replacement.c_str() : "");
            transformCaseSensitive = value ? value->caseSensitive : true;
            transformMultiline = value ? value->multiline : false;
            transformDotAll = value ? value->dotMatchesNewline : false;
            transformReplaceAll = value ? value->replaceAll : true;
            transformStatus.clear();
        };
        auto draftTransform = [&]() {
            RegexTransformDefinition draft;
            draft.transformId = editingTransformId;
            draft.name = transformName;
            draft.pattern = transformPattern;
            draft.replacement = transformReplacement;
            draft.caseSensitive = transformCaseSensitive;
            draft.multiline = transformMultiline;
            draft.dotMatchesNewline = transformDotAll;
            draft.replaceAll = transformReplaceAll;
            return draft;
        };
        if (!transformsLoaded)
            reloadTransforms();

        bool requestTransformDelete = false;
        if (transforms.empty()) {
            EmptyState("No regex transforms yet. Create one to transform text before paste.");
        } else if (BeginSettingsTable("##regex_transform_list", 4,
                   ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Pattern", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Behavior", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 135.0f);
            ImGui::TableHeadersRow();
            for (const RegexTransformDefinition& transform : transforms) {
                ImGui::PushID(static_cast<int>(transform.transformId));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(transform.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", transform.pattern.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Replace with: %s", transform.replacement.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s%s%s",
                    transform.replaceAll ? "all" : "first",
                    transform.caseSensitive ? "" : ", ignore case",
                    transform.multiline ? ", multiline" : "");
                ImGui::TableSetColumnIndex(3);
                if (ImGui::SmallButton("Edit"))
                    openTransformEditor(&transform);
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) {
                    pendingTransformDeleteId = transform.transformId;
                    requestTransformDelete = true;
                }
                ImGui::PopID();
            }
            EndSettingsTable();
        }

        if (requestTransformDelete)
            ImGui::OpenPopup("Delete regex transform?");
        if (ImGui::BeginPopupModal("Delete regex transform?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Delete this regex transform permanently?");
            ImGui::Spacing();
            if (ImGui::Button("Delete", {90.0f, 0.0f})) {
                transformStatusError =
                    !app->DeleteRegexTransform(pendingTransformDeleteId);
                transformStatus = transformStatusError
                    ? "Could not delete the transform."
                    : "Regex transform deleted.";
                if (!transformStatusError) {
                    if (editingTransformId == pendingTransformDeleteId) {
                        transformEditorOpen = false;
                        editingTransformId = 0;
                    }
                    reloadTransforms();
                }
                pendingTransformDeleteId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {90.0f, 0.0f})) {
                pendingTransformDeleteId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        if (!transformEditorOpen) {
            if (ImGui::Button("New regex transform"))
                openTransformEditor(nullptr);
        } else {
            ImGui::SeparatorText(editingTransformId == 0
                ? "New transform" : "Edit transform");
            ImGui::TextUnformatted("Name");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##regex_transform_name", transformName,
                             sizeof(transformName));
            ImGui::TextUnformatted("PCRE2 pattern");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##regex_transform_pattern", transformPattern,
                             sizeof(transformPattern));
            ImGui::TextUnformatted("Replacement ($1 or ${name} for capture groups)");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##regex_transform_replacement", transformReplacement,
                             sizeof(transformReplacement));
            ImGui::Checkbox("Case sensitive", &transformCaseSensitive);
            ImGui::SameLine();
            ImGui::Checkbox("Multiline (^/$ per line)", &transformMultiline);
            ImGui::SameLine();
            ImGui::Checkbox("Dot matches newline", &transformDotAll);
            ImGui::SameLine();
            ImGui::Checkbox("Replace all", &transformReplaceAll);

            ImGui::TextUnformatted("Test input");
            ImGui::InputTextMultiline("##regex_transform_sample", transformSample,
                                      sizeof(transformSample), {-1.0f, 80.0f});
            if (ImGui::Button("Test transform", {115.0f, 0.0f})) {
                const RegexTransformResult result =
                    ApplyRegexTransform(draftTransform(), transformSample);
                transformStatusError = !result.ok;
                transformStatus = result.ok
                    ? "Preview (" + std::to_string(result.replacements) +
                          " replacement" + (result.replacements == 1 ? "): " : "s): ") +
                          result.output
                    : result.error;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save transform", {115.0f, 0.0f})) {
                RegexTransformDefinition draft = draftTransform();
                const size_t first = draft.name.find_first_not_of(" \t\r\n");
                const size_t last = draft.name.find_last_not_of(" \t\r\n");
                draft.name = first == std::string::npos ? std::string{} :
                    draft.name.substr(first, last - first + 1);
                const std::string validation = ValidateRegexTransform(draft);
                if (!validation.empty()) {
                    transformStatusError = true;
                    transformStatus = validation;
                } else {
                    transformStatusError = !app->SaveRegexTransform(draft);
                    transformStatus = transformStatusError
                        ? "Could not save the transform. Names must be unique."
                        : "Regex transform saved.";
                    if (!transformStatusError) {
                        transformEditorOpen = false;
                        editingTransformId = 0;
                        reloadTransforms();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {90.0f, 0.0f})) {
                transformEditorOpen = false;
                editingTransformId = 0;
                transformStatus.clear();
            }
        }
        if (!transformStatus.empty()) {
            StatusMessage(transformStatusError ? SettingsStatus::Error : SettingsStatus::Success,
                          transformStatus.c_str());
        }
    }
    EndSettingsCard();
    if (BeginSettingsCard("##developer_templates", "Templates",
                          "Compose text with {{1}}, {{2}}, and {{slot:name}}, then apply it to popup selections.")) {
        static bool templatesLoaded = false;
        static std::vector<PasteTemplateDefinition> templates;
        static bool templateEditorOpen = false;
        static int64_t editingTemplateId = 0;
        static int64_t pendingTemplateDeleteId = 0;
        static char templateName[128]{};
        static char templateBody[16384]{};
        static char templateSamples[8192]{};
        static std::string templateStatus;
        static bool templateStatusError = false;

        auto reloadTemplates = [&]() {
            templates = app->GetPasteTemplates();
            templatesLoaded = true;
        };
        auto openTemplateEditor = [&](const PasteTemplateDefinition* value) {
            templateEditorOpen = true;
            editingTemplateId = value ? value->templateId : 0;
            std::snprintf(templateName, sizeof(templateName), "%s",
                          value ? value->name.c_str() : "");
            std::snprintf(templateBody, sizeof(templateBody), "%s",
                          value ? value->body.c_str() : "");
            templateStatus.clear();
        };
        auto draftTemplate = [&]() {
            PasteTemplateDefinition value;
            value.templateId = editingTemplateId;
            value.name = templateName;
            value.body = templateBody;
            return value;
        };
        if (!templatesLoaded)
            reloadTemplates();

        bool requestTemplateDelete = false;
        if (templates.empty()) {
            EmptyState("No paste templates yet. Create one to interpolate selected items or named slots.");
        } else if (BeginSettingsTable("##paste_template_list", 3,
                   ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Template", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 135.0f);
            ImGui::TableHeadersRow();
            for (const PasteTemplateDefinition& value : templates) {
                ImGui::PushID(static_cast<int>(value.templateId));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(value.name.c_str());
                ImGui::TableSetColumnIndex(1);
                std::string preview = value.body;
                std::replace(preview.begin(), preview.end(), '\n', ' ');
                std::replace(preview.begin(), preview.end(), '\r', ' ');
                if (preview.size() > 110)
                    preview = preview.substr(0, 107) + "...";
                ImGui::TextDisabled("%s", preview.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", value.body.c_str());
                ImGui::TableSetColumnIndex(2);
                if (ImGui::SmallButton("Edit"))
                    openTemplateEditor(&value);
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) {
                    pendingTemplateDeleteId = value.templateId;
                    requestTemplateDelete = true;
                }
                ImGui::PopID();
            }
            EndSettingsTable();
        }

        if (requestTemplateDelete)
            ImGui::OpenPopup("Delete paste template?");
        if (ImGui::BeginPopupModal("Delete paste template?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Delete this paste template permanently?");
            ImGui::Spacing();
            if (ImGui::Button("Delete", {90.0f, 0.0f})) {
                templateStatusError =
                    !app->DeletePasteTemplate(pendingTemplateDeleteId);
                templateStatus = templateStatusError
                    ? "Could not delete the template."
                    : "Paste template deleted.";
                if (!templateStatusError) {
                    if (editingTemplateId == pendingTemplateDeleteId) {
                        templateEditorOpen = false;
                        editingTemplateId = 0;
                    }
                    reloadTemplates();
                }
                pendingTemplateDeleteId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {90.0f, 0.0f})) {
                pendingTemplateDeleteId = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        if (!templateEditorOpen) {
            if (ImGui::Button("New paste template"))
                openTemplateEditor(nullptr);
        } else {
            ImGui::SeparatorText(editingTemplateId == 0
                ? "New template" : "Edit template");
            ImGui::TextUnformatted("Name");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##paste_template_name", templateName,
                             sizeof(templateName));
            ImGui::TextUnformatted("Template body");
            ImGui::InputTextMultiline("##paste_template_body", templateBody,
                                      sizeof(templateBody), {-1.0f, 125.0f});
            const std::vector<NamedClipboardSlot> namedSlots = app->GetNamedSlots();
            if (namedSlots.empty()) {
                ImGui::TextDisabled("No named slots are available for {{slot:name}} placeholders.");
            } else {
                std::string names = "Available named slots: ";
                for (const NamedClipboardSlot& slot : namedSlots) {
                    if (names.back() != ' ') names += ", ";
                    names += slot.name;
                }
                ImGui::TextDisabled("%s", names.c_str());
            }
            ImGui::TextUnformatted("Test numbered values (one line for each of {{1}}, {{2}}, ...)");
            ImGui::InputTextMultiline("##paste_template_samples", templateSamples,
                                      sizeof(templateSamples), {-1.0f, 75.0f});
            if (ImGui::Button("Test template", {115.0f, 0.0f})) {
                std::vector<std::pair<std::string, std::string>> namedValues;
                for (const NamedClipboardSlot& slot : namedSlots)
                    namedValues.emplace_back(slot.name, slot.text);
                const PasteTemplateResult result = ApplyPasteTemplate(
                    draftTemplate(), SplitLines(templateSamples), namedValues);
                templateStatusError = !result.ok;
                templateStatus = result.ok ? "Preview: " + result.output : result.error;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save template", {115.0f, 0.0f})) {
                PasteTemplateDefinition value = draftTemplate();
                value.name = TrimAscii(value.name);
                const std::string validation = ValidatePasteTemplate(value);
                if (!validation.empty()) {
                    templateStatusError = true;
                    templateStatus = validation;
                } else {
                    templateStatusError = !app->SavePasteTemplate(value);
                    templateStatus = templateStatusError
                        ? "Could not save the template. Names must be unique."
                        : "Paste template saved.";
                    if (!templateStatusError) {
                        templateEditorOpen = false;
                        editingTemplateId = 0;
                        reloadTemplates();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {90.0f, 0.0f})) {
                templateEditorOpen = false;
                editingTemplateId = 0;
                templateStatus.clear();
            }
        }
        if (!templateStatus.empty()) {
            StatusMessage(templateStatusError ? SettingsStatus::Error : SettingsStatus::Success,
                          templateStatus.c_str());
        }
    }
    EndSettingsCard();
    if (BeginSettingsCard("##developer_pretty_print", "Structured content",
                          "Popup context actions for JSON, XML, and SQL formatted paste plus two-item comparison.")) {
        ImGui::TextDisabled("Select two popup items and choose Compare selected items for a side-by-side diff.");
        ImGui::TextDisabled("Right-click detected JSON, XML, or SQL text and choose Paste formatted.");
    }
    EndSettingsCard();
}


// -- Section: Developer -------------------------------------------------------

#ifndef NDEBUG
void MainWindow::DrawDeveloper() {
    Application* app = Application::Get();
    if (!app) return;

    PageHeader("Developer", "Advanced diagnostics, clipboard inspection, and experiments.");

    DeveloperSettings dev = app->GetDeveloperSettings();
    bool changed = false;
    changed |= ImGui::Checkbox("Enable Developer Mode", &dev.enabled);
    if (changed)
        app->SetDeveloperSettings(dev);

    if (!dev.enabled) {
        ImGui::Spacing();
        ImGui::TextDisabled("Developer tools are hidden until Developer Mode is enabled.");
        return;
    }

    enum DeveloperTab { TAB_GENERAL, TAB_DIAGNOSTICS, TAB_INSPECTORS };
    static int developerTab = TAB_GENERAL;
    const int previousDeveloperTab = developerTab;
    if (ImGui::BeginTabBar("##developer_tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        if (ImGui::BeginTabItem("General")) { developerTab = TAB_GENERAL; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Diagnostics")) { developerTab = TAB_DIAGNOSTICS; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Inspectors")) { developerTab = TAB_INSPECTORS; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ClipboardHistory* hist = app->GetHistory();
    ImGuiWindowFlags contentFlags = app->GetAppearance().showScrollbars
        ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar;
    // Native ImGui wheel propagation can move this parent for one frame when
    // the pointer is over a nested inspector. Route all wheel input through
    // the explicit smooth-scroll ownership below instead.
    contentFlags |= ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("##developer_tab_content", {0.0f, -ImGui::GetStyle().ItemSpacing.y},
                          ImGuiChildFlags_None, contentFlags)) {
        if (developerTab != previousDeveloperTab)
            ImGui::SetScrollY(0.0f);
        bool nestedScrollerHovered = false;

        if (developerTab == TAB_GENERAL) {
            if (BeginSettingsCard("##developer_interfaces", "Developer interfaces",
                                  "Control advanced command-line access and diagnostic metadata.")) {
                bool generalChanged = false;
                generalChanged |= ImGui::Checkbox("Enable CLI interface", &dev.cliEnabled);
                ImGui::SameLine(); ImGui::TextDisabled("(?)");
                HelpTooltip("When off, runtime CLI commands are blocked. Help, status, config, and --set still work.");
                generalChanged |= ImGui::Checkbox("Show source process metadata", &dev.showSourceProcess);
                generalChanged |= ImGui::Checkbox("Enable developer event log", &dev.eventLogEnabled);
                if (generalChanged)
                    app->SetDeveloperSettings(dev);
            }
            EndSettingsCard();

            if (BeginSettingsCard("##developer_popup", "Popup experiments",
                                  "Test focus behavior without changing the normal no-activate popup design.")) {
                if (PopupWindow* popup = app->GetPopup()) {
                    bool focusTest = popup->m_focusTestMode;
                    if (ImGui::Checkbox("Activate popup, then restore focus to its caller", &focusTest))
                        popup->m_focusTestMode = focusTest;
                    HelpTooltip("Test mode: temporarily allows normal popup activation, then returns foreground focus to the app that was active when the popup opened.");
                } else {
                    StatusMessage(SettingsStatus::Warning, "Popup runtime is not available.");
                }
            }
            EndSettingsCard();
        }

        if (developerTab == TAB_DIAGNOSTICS) {
            if (BeginSettingsCard("##developer_runtime", "Runtime diagnostics",
                                  "Live process, profile, storage, and window information.")) {
                const ClipboardProfileConfig* active = app->GetActiveClipboardProfile();
                const SIZE livePopupSize = app->PopupCurrentSize();
                ImGui::Text("Process ID: %lu", static_cast<unsigned long>(app->ProcessId()));
                ImGui::TextWrapped("Executable: %s", app->ExecutablePath().c_str());
                ImGui::Text("Popup size: %ld x %ld", static_cast<long>(livePopupSize.cx),
                            static_cast<long>(livePopupSize.cy));
                ImGui::TextWrapped("Working directory: %s", app->WorkingDirectory().c_str());
                ImGui::Text("Foreground process: %s", app->ForegroundProcessName().c_str());
                ImGui::Text("Active clipboard: %s", active ? active->name.c_str() : "(none)");
                ImGui::Text("Clipboard ID: %s", active ? active->id.c_str() : "(none)");
                ImGui::Text("Bound process: %s", active && !active->processName.empty()
                            ? active->processName.c_str() : "(none)");
                ImGui::Text("History items: %zu", hist ? hist->Size() : 0);
                ImGui::Text("Pinned items: %zu", hist ? hist->PinnedSize() : 0);
                ImGui::TextWrapped("Config: %s", ConfigStore::Path().string().c_str());
                ImGui::TextWrapped("Fonts: %s", ConfigStore::FontsDirectory().string().c_str());
                if (ImGui::SmallButton("Toggle debug output"))
                    app->ToggleDebugWindow();
                ImGui::SameLine();
                static std::filesystem::path lastIconDumpPath;
                if (ImGui::SmallButton("Dump current icons")) {
                    lastIconDumpPath = DumpCurrentIcons();
                    if (!lastIconDumpPath.empty())
                        ShellExecuteW(nullptr, L"open", lastIconDumpPath.wstring().c_str(),
                                      nullptr, nullptr, SW_SHOWNORMAL);
                }
                if (!lastIconDumpPath.empty())
                    ImGui::TextWrapped("Icon dump: %s", lastIconDumpPath.string().c_str());
            }
            EndSettingsCard();

            if (BeginSettingsCard("##developer_live_telemetry", "Live telemetry",
                                  "Rolling memory, database, render, and clipboard-event measurements.")) {
                const RuntimeTelemetry telemetry = app->GetRuntimeTelemetry();
                const auto mib = [](uint64_t bytes) {
                    return static_cast<double>(bytes) / (1024.0 * 1024.0);
                };
                if (BeginSettingsTable("##runtime_telemetry_table", 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Measurement", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                    ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableHeadersRow();
                    const auto row = [](const char* name, const char* format, auto value) {
                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(name); ImGui::TableSetColumnIndex(1);
                        ImGui::Text(format, value);
                    };
                    row("Active-history memory", "%.3f MiB", mib(telemetry.historyBytes));
                    row("Preserved format payloads", "%.3f MiB", mib(telemetry.formatBytes));
                    row("Popup thumbnail textures", "%.3f MiB", mib(telemetry.thumbnailBytes));
                    row("Most recent database query", "%.3f ms", telemetry.databaseQueryMs);
                    row("Render-frame moving average", "%.3f ms", telemetry.renderFrameMs);
                    row("Clipboard events (rolling minute)", "%zu", telemetry.clipboardEventsLastMinute);
                    EndSettingsTable();
                }
                if (ImGui::SmallButton("Copy live telemetry")) {
                    std::ostringstream text;
                    text << std::fixed << std::setprecision(3)
                         << "history_bytes\t" << telemetry.historyBytes << '\n'
                         << "format_bytes\t" << telemetry.formatBytes << '\n'
                         << "thumbnail_bytes\t" << telemetry.thumbnailBytes << '\n'
                         << "database_query_ms\t" << telemetry.databaseQueryMs << '\n'
                         << "render_frame_ms\t" << telemetry.renderFrameMs << '\n'
                         << "clipboard_events_last_minute\t"
                         << telemetry.clipboardEventsLastMinute << '\n';
                    ImGui::SetClipboardText(text.str().c_str());
                }
            }
            EndSettingsCard();

            if (BeginSettingsCard("##developer_startup_profile", "Startup profile",
                                  "Timestamped initialization stages through the first rendered frame.")) {
                const auto& timings = app->GetStartupTimings();
                const auto& metrics = app->GetStartupMetrics();
                const std::filesystem::path profilePath =
                    ConfigStore::Directory() / "startup_profile.log";
                ImGui::TextWrapped("Report: %s", profilePath.string().c_str());
                if (ImGui::SmallButton("Copy startup timings")) {
                    std::ostringstream text;
                    text << "Stage\tDuration (ms)\tCompleted at (ms)\n";
                    text << std::fixed << std::setprecision(3);
                    for (const StartupTiming& timing : timings)
                        text << timing.name << '\t' << timing.durationMs << '\t'
                             << timing.completedAtMs << '\n';
                    text << "\nMetric\tValue\n";
                    for (const StartupMetric& metric : metrics)
                        text << metric.name << '\t' << metric.value << '\n';
                    ImGui::SetClipboardText(text.str().c_str());
                }
                if (timings.empty()) {
                    EmptyState("Startup timing data is not available yet.");
                } else if (BeginSettingsTable("##startup_timing_table", 3,
                                             ImGuiTableFlags_Borders |
                                             ImGuiTableFlags_RowBg |
                                             ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                    ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                    ImGui::TableSetupColumn("Completed", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                    ImGui::TableHeadersRow();
                    for (const StartupTiming& timing : timings) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(timing.name.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.3f ms", timing.durationMs);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.3f ms", timing.completedAtMs);
                    }
                    EndSettingsTable();
                }
                if (!metrics.empty() && BeginSettingsTable(
                        "##startup_metric_table", 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableHeadersRow();
                    for (const StartupMetric& metric : metrics) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(metric.name.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(metric.value.c_str());
                    }
                    EndSettingsTable();
                }
            }
            EndSettingsCard();

            if (BeginSettingsCard("##developer_events", "Developer event log",
                                  "Recent internal events collected by the Debug build.")) {
                if (ImGui::SmallButton("Copy log")) {
                    std::string text;
                    for (const std::string& line : app->GetDeveloperEvents())
                        text += line + "\n";
                    ImGui::SetClipboardText(text.c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear log"))
                    app->ClearDeveloperEvents();
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
                ImGuiWindowFlags childFlags = app->GetAppearance().showScrollbars
                    ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar;
                childFlags |= ImGuiWindowFlags_NoScrollWithMouse;
                if (ImGui::BeginChild("##dev_event_log", {-1.0f, 180.0f},
                                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                                      childFlags)) {
                    nestedScrollerHovered |= ImGui::IsWindowHovered(
                        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                    const auto& events = app->GetDeveloperEvents();
                    if (!dev.eventLogEnabled)
                        ImGui::TextDisabled("Enable developer event log to collect new events.");
                    else if (events.empty())
                        EmptyState("No developer events yet.");
                    else {
                        for (const std::string& line : events)
                            ImGui::Selectable(line.c_str(), false);
                        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                            ImGui::SetScrollHereY(1.0f);
                    }
                    SmoothScrollCurrentWindow("dev_event_log_entries", 72.0f);
                }
                ImGui::EndChild();
                ImGui::PopStyleVar();
            }
            EndSettingsCard();
        }

        if (developerTab == TAB_INSPECTORS) {
            static uint64_t inspectedItemId = 0;
            static uint64_t formatSelectionItemId = 0;
            static uint32_t inspectedFormatOrder = UINT32_MAX;
            if (BeginSettingsCard("##developer_item_inspector", "Clipboard item inspector",
                                  "Review identifiers, tags, sources, and timestamps for active-history items.")) {
                if (!app->GetLastGeneratedPasteSource().empty()) {
                    ImGui::Text("Last generated paste");
                    ImGui::SameLine();
                    ImGui::TextDisabled("Source: %s", app->GetLastGeneratedPasteSource().c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("Destination: %s",
                                        app->GetLastGeneratedPasteDestination().empty()
                                            ? "(unknown)"
                                            : app->GetLastGeneratedPasteDestination().c_str());
                    ImGui::Separator();
                }
                if (!hist || hist->Size() == 0) {
                    ImGui::TextDisabled("The active clipboard profile has no history items.");
                } else {
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
                    ImGuiWindowFlags childFlags = app->GetAppearance().showScrollbars
                        ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar;
                    childFlags |= ImGuiWindowFlags_NoScrollWithMouse;
                    if (ImGui::BeginChild("##dev_item_inspector", {-1.0f, 250.0f},
                                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                                          childFlags)) {
                        nestedScrollerHovered |= ImGui::IsWindowHovered(
                            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                        const size_t limit = std::min<size_t>(hist->Size(), 50);
                        for (size_t i = 0; i < limit; ++i) {
                            ClipboardItem item;
                            if (!hist->GetCopy(i, item)) continue;
                            if (inspectedItemId == 0)
                                inspectedItemId = item.id;
                            ImGui::PushID(static_cast<int>(i));
                            const std::string label = std::to_string(i + 1) + "  " + item.Preview(64);
                            ImGuiTreeNodeFlags nodeFlags = item.id == inspectedItemId
                                ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None;
                            const bool open = ImGui::TreeNodeEx(label.c_str(), nodeFlags);
                            if (ImGui::IsItemClicked()) {
                                inspectedItemId = item.id;
                                formatSelectionItemId = 0;
                                inspectedFormatOrder = UINT32_MAX;
                            }
                            if (open) {
                                ImGui::Text("ID: %llu", static_cast<unsigned long long>(item.id));
                                ImGui::Text("Hash: %llu", static_cast<unsigned long long>(item.contentHash));
                                ImGui::Text("Type: %s", ContentTypeName(item.type));
                                ImGui::TextWrapped("Tags: %s", TagList(item.tags).c_str());
                                ImGui::Text("Pinned: %s", item.pinned ? "yes" : "no");
                                ImGui::Text("Source: %s", item.sourceProcess.empty()
                                            ? "(unknown)" : item.sourceProcess.c_str());
                                ImGui::Text("Captured: %s", TimeLabel(item.timestamp).c_str());
                                ImGui::Text("Created: %s", TimeLabel(item.createdAt).c_str());
                                ImGui::Text("Updated: %s", TimeLabel(item.updatedAt).c_str());
                                ImGui::Text("Last used: %s", TimeLabel(item.lastUsedAt).c_str());
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        SmoothScrollCurrentWindow("dev_item_inspector_entries", 72.0f);
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                }
            }
            EndSettingsCard();

            ClipboardItem inspected;
            const bool hasInspectedItem = hist && inspectedItemId != 0 &&
                                          hist->GetByIdCopy(inspectedItemId, inspected);
            if (hasInspectedItem && formatSelectionItemId != inspected.id) {
                formatSelectionItemId = inspected.id;
                inspectedFormatOrder = UINT32_MAX;
                for (const ClipboardFormatRecord& format : inspected.formats) {
                    if (!format.data.empty()) {
                        inspectedFormatOrder = format.order;
                        break;
                    }
                }
            }

            if (BeginSettingsCard("##developer_format_inspector", "Format inspector",
                                  "Inspect the ordered Win32 format manifest and exact bytes retained for the selected item.")) {
                if (!hasInspectedItem) {
                    ImGui::TextDisabled("Select a clipboard item above.");
                } else if (inspected.formats.empty()) {
                    ImGui::TextDisabled("This item predates format-bundle capture or has no recorded formats.");
                } else {
                    ImGui::TextWrapped("%s", inspected.Preview(100).c_str());
                    ImGui::TextDisabled("%zu format%s recorded", inspected.formats.size(),
                                        inspected.formats.size() == 1 ? "" : "s");
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
                    ImGuiWindowFlags childFlags = app->GetAppearance().showScrollbars
                        ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar;
                    childFlags |= ImGuiWindowFlags_NoScrollWithMouse;
                    if (ImGui::BeginChild("##format_manifest", {-1.0f, 260.0f},
                                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                                          childFlags)) {
                        nestedScrollerHovered |= ImGui::IsWindowHovered(
                            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                        for (const ClipboardFormatRecord& format : inspected.formats) {
                            ImGui::PushID(static_cast<int>(format.order));
                            std::ostringstream label;
                            label << format.order + 1 << "  " << format.name
                                  << "  [" << ClipboardFormatStatusName(format.status) << "]";
                            ImGuiTreeNodeFlags flags = format.order == inspectedFormatOrder
                                ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None;
                            const bool open = ImGui::TreeNodeEx(label.str().c_str(), flags);
                            if (ImGui::IsItemClicked())
                                inspectedFormatOrder = format.order;
                            if (open) {
                                ImGui::Text("Capture ID: %u (0x%04X)", format.formatId, format.formatId);
                                ImGui::Text("Reported size: %llu bytes",
                                            static_cast<unsigned long long>(format.byteSize));
                                ImGui::Text("Safe to replay: %s", format.replaySafe ? "yes" : "no");
                                ImGui::Text("Stored bytes: %zu", format.data.size());
                                if (!format.data.empty()) {
                                    constexpr size_t previewLimit = 128;
                                    const size_t count = std::min(previewLimit, format.data.size());
                                    std::ostringstream hex;
                                    hex << std::hex << std::setfill('0');
                                    for (size_t i = 0; i < count; ++i) {
                                        if (i > 0) hex << ' ';
                                        hex << std::setw(2) << static_cast<unsigned>(format.data[i]);
                                    }
                                    if (count < format.data.size())
                                        hex << " ...";
                                    ImGui::TextWrapped("%s", hex.str().c_str());
                                }
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        SmoothScrollCurrentWindow("developer_format_manifest_entries", 72.0f);
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                }
            }
            EndSettingsCard();

            if (BeginSettingsCard("##developer_hex_viewer", "Hex viewer",
                                  "Browse the complete selected payload as offsets, hexadecimal bytes, and ASCII.")) {
                if (!hasInspectedItem) {
                    ImGui::TextDisabled("Select a clipboard item above.");
                } else {
                    const std::vector<uint8_t>* bytes = nullptr;
                    std::string byteSource;
                    for (const ClipboardFormatRecord& format : inspected.formats) {
                        if (format.order == inspectedFormatOrder && !format.data.empty()) {
                            bytes = &format.data;
                            byteSource = format.name;
                            break;
                        }
                    }

                    std::vector<uint8_t> fallbackBytes;
                    if (!bytes && inspected.IsText()) {
                        fallbackBytes.assign(inspected.text.begin(), inspected.text.end());
                        bytes = &fallbackBytes;
                        byteSource = "Normalized UTF-8 text";
                    }

                    static std::string cachedImageId;
                    static std::vector<uint8_t> cachedImageBytes;
                    static StoredFormat cachedImageFormat{StoredFormat::Png};
                    if (!bytes && inspected.IsImage() && !inspected.imageStoreId.empty()) {
                        if (cachedImageId != inspected.imageStoreId) {
                            cachedImageId = inspected.imageStoreId;
                            cachedImageBytes.clear();
                            if (ImageStore* store = app->GetImageStore())
                                store->GetStoredBytes(cachedImageId, cachedImageBytes, cachedImageFormat);
                        }
                        if (!cachedImageBytes.empty()) {
                            bytes = &cachedImageBytes;
                            byteSource = cachedImageFormat == StoredFormat::RawDib ? "Stored image (DIB)"
                                : cachedImageFormat == StoredFormat::Jpeg ? "Stored image (JPEG)"
                                : "Stored image (PNG)";
                        }
                    }

                    if (!bytes || bytes->empty()) {
                        EmptyState("No raw or normalized bytes are stored for this item or format.");
                    } else {
                        ImGui::Text("%s", byteSource.c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled("%zu bytes", bytes->size());
                        ImGui::TextDisabled("Offset     Hex bytes                                         ASCII");
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
                        ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollWithMouse |
                                                       ImGuiWindowFlags_HorizontalScrollbar;
                        if (!app->GetAppearance().showScrollbars)
                            childFlags |= ImGuiWindowFlags_NoScrollbar;
                        if (ImGui::BeginChild("##full_hex_view", {-1.0f, 300.0f},
                                              ImGuiChildFlags_Borders |
                                                  ImGuiChildFlags_AlwaysUseWindowPadding,
                                              childFlags)) {
                            nestedScrollerHovered |= ImGui::IsWindowHovered(
                                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                            constexpr size_t bytesPerRow = 16;
                            const size_t rowCount = (bytes->size() + bytesPerRow - 1) / bytesPerRow;
                            ImGuiListClipper clipper;
                            clipper.Begin(static_cast<int>(std::min<size_t>(rowCount, INT_MAX)));
                            while (clipper.Step()) {
                                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                                    const size_t offset = static_cast<size_t>(row) * bytesPerRow;
                                    std::ostringstream line;
                                    line << std::hex << std::uppercase << std::setfill('0')
                                         << std::setw(8) << offset << "  ";
                                    for (size_t column = 0; column < bytesPerRow; ++column) {
                                        if (offset + column < bytes->size())
                                            line << std::setw(2)
                                                 << static_cast<unsigned>((*bytes)[offset + column]) << ' ';
                                        else
                                            line << "   ";
                                    }
                                    line << " ";
                                    for (size_t column = 0; column < bytesPerRow &&
                                                             offset + column < bytes->size(); ++column) {
                                        const uint8_t value = (*bytes)[offset + column];
                                        line << (value >= 32 && value <= 126
                                                    ? static_cast<char>(value) : '.');
                                    }
                                    ImGui::TextUnformatted(line.str().c_str());
                                }
                            }
                            SmoothScrollCurrentWindow("developer_full_hex_rows", 96.0f);
                        }
                        ImGui::EndChild();
                        ImGui::PopStyleVar();
                    }
                }
            }
            EndSettingsCard();
        }


        const char* scrollId = developerTab == TAB_GENERAL ? "developer_general_scroll"
            : developerTab == TAB_DIAGNOSTICS ? "developer_diagnostics_scroll"
            : "developer_inspectors_scroll";
        // The nearest scrollable pane owns the wheel gesture. This keeps the
        // Developer page and the global settings page from moving behind an
        // inspector/event-log list, while cards still scroll their subpage.
        SmoothScrollCurrentWindow(scrollId, 82.0f, 0.32f,
                                  !nestedScrollerHovered, nestedScrollerHovered);
    }
    ImGui::EndChild();
}
#endif
