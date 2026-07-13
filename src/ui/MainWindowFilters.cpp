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

    ImGui::TextDisabled("Filters");
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<CustomFilter> filters = app->GetCustomFilters();
    static std::string selectedId;
    static CustomFilter draft;
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
        strncpy_s(nameBuf, filter.name.c_str(), _TRUNCATE);
        strncpy_s(patternBuf, filter.pattern.c_str(), _TRUNCATE);
    };
    auto saveFilters = [&]() {
        app->SetCustomFilters(filters);
    };

    if (selectedId.empty() && !filters.empty())
        loadDraft(filters.front());
    if (!selectedId.empty() && findFilter(selectedId) == filters.end()) {
        selectedId.clear();
        if (!filters.empty())
            loadDraft(filters.front());
    }

    SectionHeader("Filter Buttons");
    if (filters.empty()) {
        ImGui::TextDisabled("No custom filters yet.");
    } else {
        ImGui::TextDisabled("Drag filters to reorder popup buttons.");
        ImGui::Spacing();
        for (int i = 0; i < static_cast<int>(filters.size()); ++i) {
            ImGui::PushID(filters[i].id.c_str());
            const bool selected = filters[i].id == selectedId;
            std::string label = filters[i].name;
            if (!filters[i].enabled)
                label += " (off)";
            if (ImGui::Selectable(label.c_str(), selected))
                loadDraft(filters[i]);

            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("CPP_CUSTOM_FILTER", &i, sizeof(i));
                ImGui::TextUnformatted(filters[i].name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CPP_CUSTOM_FILTER")) {
                    int from = *static_cast<const int*>(payload->Data);
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

    SectionHeader("Editor");
    if (BlueButton("New filter", 120.0f)) {
        CustomFilter filter;
        filter.id = NewCustomFilterId();
        filter.name = "New filter";
        filter.pattern = "text";
        filters.push_back(filter);
        saveFilters();
        loadDraft(filters.back());
    }

    if (selectedId.empty()) {
        ImGui::TextDisabled("Create a filter to edit its button and matching rule.");
        return;
    }

    draft.name = TrimAscii(nameBuf);
    draft.pattern = patternBuf;

    ImGui::Checkbox("Enabled", &draft.enabled);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Name", nameBuf, sizeof(nameBuf));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("Pattern", patternBuf, sizeof(patternBuf), {0.0f, 88.0f});
    draft.name = TrimAscii(nameBuf);
    draft.pattern = patternBuf;

    int mode = static_cast<int>(draft.mode);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Mode", CustomFilterModeName(draft.mode))) {
        for (int i = 0; i <= 3; ++i) {
            const auto value = static_cast<CustomFilterMode>(i);
            if (ImGui::Selectable(CustomFilterModeName(value), mode == i))
                mode = i;
        }
        ImGui::EndCombo();
    }
    draft.mode = static_cast<CustomFilterMode>(std::clamp(mode, 0, 3));

    int target = static_cast<int>(draft.target);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Target", CustomFilterTargetName(draft.target))) {
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

    SectionHeader("Routing");
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
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo("Destination profile", selectedProfileName)) {
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

    CustomFilterValidation validation = ValidateCustomFilter(draft);
    const bool routingOk = !draft.routeToProfile || !draft.routeProfileId.empty();
    if (validation.ok)
        ImGui::TextDisabled("Pattern is valid.");
    else
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", validation.message.c_str());
    if (!routingOk)
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Choose a destination profile for routing.");

    SectionHeader("Test");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("Sample text", testBuf, sizeof(testBuf), {0.0f, 72.0f});
    ClipboardItem sample;
    sample.type = ContentType::Text;
    sample.text = testBuf;
    sample.tags = ContentDetector::DetectTags(sample.text);
    const bool sampleMatches = validation.ok && CustomFilterMatches(draft, sample);
    ImGui::TextDisabled("Result: %s", sampleMatches ? "match" : "no match");

    SectionHeader("Actions");
    if (!validation.ok || !routingOk)
        ImGui::BeginDisabled();
    if (BlueButton("Save filter", 120.0f)) {
        draft.name = TrimAscii(nameBuf);
        draft.pattern = patternBuf;
        auto it = findFilter(selectedId);
        if (it != filters.end()) {
            *it = draft;
            saveFilters();
            loadDraft(*it);
        }
    }
    if (!validation.ok || !routingOk)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (DangerButton("Delete filter", 120.0f))
        ImGui::OpenPopup("Confirm filter delete");

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
            if (!filters.empty())
                loadDraft(filters.front());
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
