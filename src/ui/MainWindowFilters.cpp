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

// -- Section: Filters ---------------------------------------------------------

void MainWindow::DrawFilters() {
    Application* app = Application::Get();
    if (!app) return;

    PageHeader("Filters", "Create custom popup buttons that show only matching clipboard items.");

    std::vector<CustomFilter> filters = app->GetCustomFilters();
    static std::string selectedId;
    static CustomFilter draft;
    static bool editorOpen = false;
    static bool creatingFilter = false;
    static char nameBuf[96]{};
    static char patternBuf[512]{};
    static char testBuf[512]{};

    auto findFilter = [&](const std::string& id) {
        return std::find_if(filters.begin(), filters.end(),
            [&](const CustomFilter& filter) { return filter.id == id; });
    };
    auto loadDraft = [&](const CustomFilter& filter) {
        draft = filter;
        selectedId = filter.id;
        editorOpen = true;
        creatingFilter = false;
        strncpy_s(nameBuf, filter.name.c_str(), _TRUNCATE);
        strncpy_s(patternBuf, filter.pattern.c_str(), _TRUNCATE);
    };
    auto saveFilters = [&]() {
        app->SetCustomFilters(filters);
    };

    if (!selectedId.empty() && findFilter(selectedId) == filters.end()) {
        selectedId.clear();
        editorOpen = false;
        creatingFilter = false;
    }

    if (BeginSettingsCard("##filters_saved", "Saved filters",
                          "Saved filters appear here and as buttons in the popup. Click one to edit it; drag to reorder.")) {
        if (filters.empty()) {
            ImGui::TextDisabled("No custom filters yet.");
        } else {
            for (int i = 0; i < static_cast<int>(filters.size()); ++i) {
                ImGui::PushID(filters[i].id.c_str());
                const bool selected = editorOpen && !creatingFilter && filters[i].id == selectedId;
                std::string label = filters[i].name;
                if (!filters[i].enabled)
                    label += "  (disabled)";
                if (ImGui::Selectable(label.c_str(), selected))
                    loadDraft(filters[i]);

                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("CPP_CUSTOM_FILTER", &i, sizeof(i));
                    ImGui::TextUnformatted(filters[i].name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CPP_CUSTOM_FILTER")) {
                        const int from = *static_cast<const int*>(payload->Data);
                        if (from >= 0 && from < static_cast<int>(filters.size()) && from != i) {
                            CustomFilter moved = filters[static_cast<size_t>(from)];
                            filters.erase(filters.begin() + from);
                            const int to = from < i ? i - 1 : i;
                            filters.insert(filters.begin() + to, std::move(moved));
                            saveFilters();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();
            }
        }
        ImGui::Spacing();
        if (BlueButton("New filter", 120.0f)) {
            draft = CustomFilter{};
            draft.id = NewCustomFilterId();
            selectedId.clear();
            nameBuf[0] = '\0';
            patternBuf[0] = '\0';
            testBuf[0] = '\0';
            editorOpen = true;
            creatingFilter = true;
        }
    }
    EndSettingsCard();

    if (!editorOpen)
        return;

    if (BeginSettingsCard("##filter_editor", creatingFilter ? "New filter" : "Edit filter",
                          creatingFilter
                              ? "Enter the required name and matching pattern. The filter is not added until you save."
                              : "Update this filter's popup label and matching behavior.")) {
        ImGui::Checkbox("Enabled", &draft.enabled);

        ImGui::TextUnformatted("Name");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##filter_name", nameBuf, sizeof(nameBuf));

        ImGui::TextUnformatted("Pattern");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextMultiline("##filter_pattern", patternBuf, sizeof(patternBuf), {0.0f, 88.0f});
        draft.name = TrimAscii(nameBuf);
        draft.pattern = patternBuf;

        ImGui::TextUnformatted("Match mode");
        int mode = static_cast<int>(draft.mode);
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo("##filter_mode", CustomFilterModeName(draft.mode))) {
            for (int i = 0; i <= 3; ++i) {
                const auto value = static_cast<CustomFilterMode>(i);
                if (ImGui::Selectable(CustomFilterModeName(value), mode == i))
                    mode = i;
            }
            ImGui::EndCombo();
        }
        draft.mode = static_cast<CustomFilterMode>(std::clamp(mode, 0, 3));

        ImGui::TextUnformatted("Match against");
        int target = static_cast<int>(draft.target);
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo("##filter_target", CustomFilterTargetName(draft.target))) {
            for (int i = 0; i <= 4; ++i) {
                const auto value = static_cast<CustomFilterTarget>(i);
                if (ImGui::Selectable(CustomFilterTargetName(value), target == i))
                    target = i;
            }
            ImGui::EndCombo();
        }
        draft.target = static_cast<CustomFilterTarget>(std::clamp(target, 0, 4));

        ImGui::Checkbox("Case sensitive", &draft.caseSensitive);
        if (draft.mode == CustomFilterMode::Regex) {
            ImGui::Checkbox("Multiline", &draft.multiline);
            ImGui::Checkbox("Dot matches newline", &draft.dotMatchesNewline);
        }
    }
    EndSettingsCard();

    if (BeginSettingsCard("##filter_routing", "Routing", "Optionally copy or move matching captures to another profile.")) {
        ImGui::Checkbox("Route matching copies to another clipboard", &draft.routeToProfile);
        if (draft.routeToProfile) {
            const std::vector<ClipboardProfileConfig>& profiles = app->GetClipboardProfiles();
            const char* selectedProfileName = "(select profile)";
            for (const ClipboardProfileConfig& profile : profiles) {
                if (profile.id == draft.routeProfileId) {
                    selectedProfileName = profile.name.c_str();
                    break;
                }
            }
            ImGui::TextUnformatted("Destination profile");
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::BeginCombo("##filter_destination", selectedProfileName)) {
                for (const ClipboardProfileConfig& profile : profiles) {
                    const bool selected = profile.id == draft.routeProfileId;
                    if (ImGui::Selectable(profile.name.c_str(), selected))
                        draft.routeProfileId = profile.id;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox("Move instead of copy", &draft.routeMove);
            ImGui::TextDisabled("Copy keeps the item in the active clipboard and also adds it to the destination.");
        }
    }
    EndSettingsCard();

    CustomFilterValidation validation = ValidateCustomFilter(draft);
    const bool routingOk = !draft.routeToProfile || !draft.routeProfileId.empty();
    if (BeginSettingsCard("##filter_test", "Test", "Try the draft against sample text before saving.")) {
        if (validation.ok)
            ImGui::TextDisabled("Pattern is valid.");
        else
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", validation.message.c_str());
        if (!routingOk)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Choose a destination profile for routing.");

        ImGui::TextUnformatted("Sample text");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextMultiline("##filter_sample", testBuf, sizeof(testBuf), {0.0f, 72.0f});
        ClipboardItem sample;
        sample.type = ContentType::Text;
        sample.text = testBuf;
        sample.tags = ContentDetector::DetectTags(sample.text);
        const bool sampleMatches = validation.ok && CustomFilterMatches(draft, sample);
        ImGui::TextDisabled("Result: %s", sampleMatches ? "match" : "no match");
    }
    EndSettingsCard();

    if (!validation.ok || !routingOk)
        ImGui::BeginDisabled();
    if (BlueButton(creatingFilter ? "Save new filter" : "Save changes", 150.0f)) {
        if (creatingFilter) {
            filters.push_back(draft);
        } else {
            auto it = findFilter(selectedId);
            if (it != filters.end())
                *it = draft;
        }
        saveFilters();
        selectedId.clear();
        editorOpen = false;
        creatingFilter = false;
    }
    if (!validation.ok || !routingOk)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (PaddedButton("Cancel", 100.0f)) {
        selectedId.clear();
        editorOpen = false;
        creatingFilter = false;
    }
    if (!creatingFilter) {
        ImGui::SameLine();
        if (DangerButton("Delete filter", 120.0f))
            ImGui::OpenPopup("Confirm filter delete");
    }

    if (ImGui::BeginPopupModal("Confirm filter delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this custom filter?");
        ImGui::TextDisabled("%s", draft.name.c_str());
        ImGui::Spacing();
        if (DangerButton("Delete", 90.0f)) {
            filters.erase(std::remove_if(filters.begin(), filters.end(),
                [&](const CustomFilter& filter) { return filter.id == selectedId; }),
                filters.end());
            selectedId.clear();
            saveFilters();
            editorOpen = false;
            creatingFilter = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
