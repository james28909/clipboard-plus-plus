#include "PopupWindow.h"
#include "../app/Application.h"
#include "../filters/CustomFilter.h"
#include "../util/Win32Util.h"
#include "Appearance.h"

#include <imgui.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

struct FilterEntry {
    std::string token;
    std::string label;
    int mode{};
    const CustomFilter* custom{};
};

static constexpr struct {
    const char* token;
    const char* label;
    int mode;
} kBuiltInFilters[] = {
    {"filter:all", "All", 0},       {"filter:text", "Text", 1},
    {"filter:url", "URL", 3},       {"filter:file", "File", 4},
    {"filter:code", "Code", 5},     {"filter:secret", "Secret", 6},
    {"filter:json", "JSON", 7},     {"filter:email", "Email", 8},
    {"filter:color", "Color", 9},   {"filter:cmd", "CMD", 10},
    {"filter:image", "Image", 2},
};

const char* MoveLabel(ClipboardHistory::MoveTarget target) {
    switch (target) {
    case ClipboardHistory::MoveTarget::Top: return "Move: Top";
    case ClipboardHistory::MoveTarget::Bottom: return "Move: Bottom";
    default: return "Move: Keep";
    }
}

const char* MoveTooltip(ClipboardHistory::MoveTarget target) {
    switch (target) {
    case ClipboardHistory::MoveTarget::Top:
        return "After paste, move the item to the top of history";
    case ClipboardHistory::MoveTarget::Bottom:
        return "After paste, move the item to the bottom of history";
    default:
        return "Keep pasted items in their current history position";
    }
}

ClipboardHistory::MoveTarget NextMoveTarget(ClipboardHistory::MoveTarget target) {
    switch (target) {
    case ClipboardHistory::MoveTarget::None: return ClipboardHistory::MoveTarget::Top;
    case ClipboardHistory::MoveTarget::Top: return ClipboardHistory::MoveTarget::Bottom;
    default: return ClipboardHistory::MoveTarget::None;
    }
}

class CommandSection {
public:
    CommandSection(const char* label, ImVec4 lineColor, float scale, float rounding,
                   bool centerLabelInFirstRow = false)
        : m_label(label), m_lineColor(lineColor), m_scale(scale),
          m_rounding(rounding), m_centerLabelInFirstRow(centerLabelInFirstRow) {
        m_left = ImGui::GetCursorScreenPos().x;
        m_right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        m_labelSize = ImGui::CalcTextSize(label);
        m_labelSize.x *= 0.78f;
        m_labelSize.y *= 0.78f;
        m_top = ImGui::GetCursorScreenPos().y;
        m_contentLeft = m_left + 3.0f * m_scale;
        const float buttonTop = m_top + 2.0f * m_scale;
        m_firstRowBottom = buttonTop + ImGui::GetFrameHeight() + 2.0f * m_scale;
        m_labelLeft = m_contentLeft + 2.0f * m_scale;
        ImGui::SetCursorScreenPos(
            {m_labelLeft + m_labelSize.x + 5.0f * m_scale, buttonTop});
    }

    float ContentRight() const { return m_right - 3.0f * m_scale; }
    float ContentLeft() const { return m_contentLeft; }

    void End() {
        const float bottom = ImGui::GetItemRectMax().y + 2.0f * m_scale;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec4 labelColor = m_lineColor;
        labelColor.w *= 0.48f;
        const float labelBottom = m_centerLabelInFirstRow ? m_firstRowBottom : bottom;
        const float baselineOffset =
            (m_centerLabelInFirstRow ? -1.0f : 1.0f) * m_scale;
        const float labelTop = m_top +
            (labelBottom - m_top - m_labelSize.y) * 0.5f + baselineOffset;
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.78f,
                      {m_labelLeft, labelTop}, ImGui::GetColorU32(labelColor),
                      m_label.c_str());
        draw->AddRect({m_left, m_top}, {m_right, bottom},
                      ImGui::GetColorU32(m_lineColor), m_rounding * m_scale,
                      0, std::max(1.0f, 1.0f * m_scale));

        ImGui::SetCursorScreenPos({m_left, bottom + std::max(1.0f, m_scale)});
        ImGui::Dummy({1.0f, 0.0f});
    }

private:
    std::string m_label;
    ImVec4 m_lineColor{};
    ImVec2 m_labelSize{};
    float m_scale{1.0f};
    float m_rounding{3.0f};
    float m_left{};
    float m_right{};
    float m_top{};
    float m_contentLeft{};
    float m_labelLeft{};
    float m_firstRowBottom{};
    bool m_centerLabelInFirstRow{false};
};

std::string ToolbarButtonText(const char* label, int badge) {
    if (badge < 0)
        return label;
    return std::string(label) + " " + std::to_string(badge);
}

float ToolbarButtonWidth(const char* label, int badge = -1) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const std::string text = ToolbarButtonText(label, badge);
    return ImGui::CalcTextSize(text.c_str()).x + style.FramePadding.x * 2.0f;
}

bool ToolbarButton(const char* id, const char* label, bool active,
                   const PopupToggleColors& colors, const char* tooltip,
                   int badge = -1) {
    const std::string text = ToolbarButtonText(label, badge);

    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, active ? colors.on : colors.off);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          active ? colors.onHovered : colors.offHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          active ? colors.onActive : colors.offActive);
    const bool clicked = ImGui::SmallButton(text.c_str());
    ImGui::PopStyleColor(3);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return clicked;
}

class WrappedButtonRow {
public:
    WrappedButtonRow(float columnGap, float rowGap, float left, float right)
        : m_columnGap(columnGap), m_rowGap(rowGap), m_left(left), m_right(right) {}

    void PlaceNext(float nextWidth) {
        if (m_first) {
            if (ImGui::GetCursorScreenPos().x + nextWidth > m_right) {
                ImGui::SetCursorScreenPos(
                    {m_left, ImGui::GetCursorScreenPos().y +
                              ImGui::GetTextLineHeight() + m_rowGap});
            }
            return;
        }
        if (ImGui::GetItemRectMax().x + m_columnGap + nextWidth <= m_right)
            ImGui::SameLine(0.0f, m_columnGap);
        else
            ImGui::SetCursorScreenPos({m_left, ImGui::GetItemRectMax().y + m_rowGap});
    }

    void Added() { m_first = false; }

private:
    float m_columnGap{};
    float m_rowGap{};
    float m_left{};
    float m_right{};
    bool m_first{true};
};

} // namespace

void PopupWindow::DrawFilterStrip() {
    Application* app = Application::Get();
    if (!app)
        return;

    const PopupToggleColors colors = GetPopupToggleColors(m_appearance);
    const AppearanceSettings effective = m_appearance.customColors
        ? m_appearance : ThemeDefaults(m_appearance.theme);
    const float scale = std::max(0.5f, EffectiveUiScale(m_appearance));
    const float gap = std::max(3.0f * scale,
                               std::clamp(m_appearance.popupButtonColumnPadding,
                                          0.0f, 16.0f));
    const float rowGap = std::max(1.0f * scale,
        std::clamp(m_appearance.popupButtonRowPadding, 0.0f, 12.0f));
    const bool customLines = m_appearance.customCommandBarColors;
    const ImVec4 filterLine = customLines ? m_appearance.popupFilterBorder
                                          : effective.popupFilterBorder;
    const ImVec4 actionLine = customLines ? m_appearance.popupActionBorder
                                          : effective.popupActionBorder;
    const ImVec4 destinationLine = customLines ? m_appearance.popupDestinationBorder
                                               : effective.popupDestinationBorder;
    const PopupSettings popupSettings = app->GetPopupSettings();
    const std::vector<CustomActionDefinition> customActions = app->GetCustomActions();
    const CustomActionContext customActionContext =
        BuildCustomActionContext(nullptr, false);

    std::vector<FilterEntry> filters;
    filters.reserve(std::size(kBuiltInFilters) + app->GetCustomFilters().size());
    for (const auto& filter : kBuiltInFilters)
        filters.push_back({filter.token, filter.label, filter.mode, nullptr});
    for (const CustomFilter& filter : app->GetCustomFilters()) {
        if (filter.enabled && !filter.pattern.empty())
            filters.push_back({"custom:" + filter.id, filter.name, 0, &filter});
    }

    auto findFilter = [&](const std::string& token) -> const FilterEntry* {
        const auto it = std::find_if(filters.begin(), filters.end(),
            [&](const FilterEntry& entry) { return entry.token == token; });
        return it == filters.end() ? nullptr : &*it;
    };
    auto activeToken = [&]() -> std::string {
        if (!m_activeCustomFilterId.empty())
            return "custom:" + m_activeCustomFilterId;
        for (const FilterEntry& filter : filters)
            if (!filter.custom && filter.mode == m_filterMode)
                return filter.token;
        return "filter:all";
    };
    auto selectFilter = [&](const FilterEntry& filter) {
        ActivateKeyboardCapture();
        m_androidPanelOpen = false;
        m_namedSlotsPanelOpen = false;
        if (filter.custom) {
            m_filterMode = 0;
            m_activeCustomFilterId = filter.custom->id;
        } else {
            m_filterMode = filter.mode;
            m_activeCustomFilterId.clear();
        }
        InvalidateVisibleHistoryCache();
    };

    if (!findFilter(activeToken())) {
        m_filterMode = 0;
        m_activeCustomFilterId.clear();
        InvalidateVisibleHistoryCache();
    }

    std::vector<const FilterEntry*> orderedFilters;
    for (const std::string& token : app->GetPopupButtonOrder()) {
        if (token == "filter:image")
            continue;
        if (const FilterEntry* filter = findFilter(token)) {
            if (std::find(orderedFilters.begin(), orderedFilters.end(), filter) ==
                orderedFilters.end()) {
                orderedFilters.push_back(filter);
            }
        }
    }
    for (const FilterEntry& filter : filters) {
        if (filter.token == "filter:image")
            continue;
        if (std::find(orderedFilters.begin(), orderedFilters.end(), &filter) ==
            orderedFilters.end()) {
            orderedFilters.push_back(&filter);
        }
    }

    const float sectionRounding =
        std::clamp(m_appearance.popupCommandBarRounding, 0.0f, 12.0f);
    CommandSection filterSection("Filters", filterLine, scale, sectionRounding, true);
    WrappedButtonRow filterRow(gap, rowGap, filterSection.ContentLeft(),
                               filterSection.ContentRight());
    const std::string selectedToken = activeToken();
    const int resultCount = static_cast<int>(BuildVisibleItemIds().size());
    for (const FilterEntry* filter : orderedFilters) {
        const bool active = filter->token == selectedToken;
        const int badge = active ? resultCount : -1;
        filterRow.PlaceNext(ToolbarButtonWidth(filter->label.c_str(), badge));
        const std::string tooltip = active
            ? filter->label + " filter is active; " + std::to_string(resultCount) +
                  " matching items"
            : "Show " + filter->label + " clipboard items";
        if (ToolbarButton(filter->token.c_str(), filter->label.c_str(), active,
                          colors, tooltip.c_str(), badge)) {
            selectFilter(*filter);
        }
        filterRow.Added();
    }
    filterSection.End();

    CommandSection actionSection("Actions", actionLine, scale, sectionRounding);
    WrappedButtonRow actionRow(gap, rowGap, actionSection.ContentLeft(),
                               actionSection.ContentRight());
    auto drawAction = [&](const char* id, const char* label, bool active,
                          const char* tooltip, int badge, auto&& callback) {
        actionRow.PlaceNext(ToolbarButtonWidth(label, badge));
        if (ToolbarButton(id, label, active, colors, tooltip, badge))
            callback();
        actionRow.Added();
    };

    drawAction("action_newline", "Newline", m_appendNewlineAfterPaste,
               "Append a newline after text paste", -1, [&]() {
        ActivateKeyboardCapture();
        app->SetAppendNewlineAfterPaste(!m_appendNewlineAfterPaste);
    });
    drawAction("action_move", MoveLabel(m_pasteMoveTarget),
               m_pasteMoveTarget != ClipboardHistory::MoveTarget::None,
               MoveTooltip(m_pasteMoveTarget), -1, [&]() {
        ActivateKeyboardCapture();
        app->SetPasteMoveTarget(NextMoveTarget(m_pasteMoveTarget));
    });

    const int selectedCount = static_cast<int>(m_itemSelection.Ids().size());
    if (selectedCount > 1) {
        drawAction("action_paste_selected", "Paste selected", true,
                   "Paste selected history items in their displayed order",
                   selectedCount, [&]() { PasteSelectedItems(); });
        drawAction("action_clear_selected", "Clear selection", false,
                   "Clear the current multi-selection", -1,
                   [&]() { ClearSelectedItems(); });
    }
    drawAction("action_settings", "Settings", false,
               "Open Clipboard++ settings", -1, [&]() { OpenSettingsWindow(); });
    if (popupSettings.programLauncherEnabled && !popupSettings.programLauncherPath.empty()) {
        const std::string label = popupSettings.programLauncherLabel.empty()
            ? "Launch" : popupSettings.programLauncherLabel;
        drawAction("action_program_launcher", label.c_str(), false,
                   "Launch the configured program", -1, [&]() {
            const std::wstring executable =
                win32util::Utf8ToWide(popupSettings.programLauncherPath);
            const HINSTANCE launched = ShellExecuteW(
                nullptr, L"open", executable.c_str(),
                nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(launched) > 32) {
                Hide();
            } else {
                app->AddDeveloperEvent("program launcher failed: error=" +
                    std::to_string(reinterpret_cast<INT_PTR>(launched)));
            }
        });
    }
    for (const CustomActionDefinition& action : customActions) {
        if (action.placement != CustomActionPlacement::Actions ||
            !CustomActionMatches(action, customActionContext))
            continue;
        const std::string id = "custom_action_" + std::to_string(action.actionId);
        const std::string label = action.icon.empty()
            ? action.label : action.icon + " " + action.label;
        const std::string tooltip = std::string(CustomActionInputName(action.input)) +
            " -> " + CustomActionOutputName(action.output);
        drawAction(id.c_str(), label.c_str(), false, tooltip.c_str(), -1,
                   [&, actionId = action.actionId]() {
                       RunCustomAction(actionId);
                   });
    }
    actionSection.End();

    auto toggleAndroid = [&]() {
        ActivateKeyboardCapture();
        m_androidPanelOpen = !m_androidPanelOpen;
        if (m_androidPanelOpen)
            m_namedSlotsPanelOpen = false;
    };
    auto toggleSlots = [&]() {
        ActivateKeyboardCapture();
        m_namedSlotsPanelOpen = !m_namedSlotsPanelOpen;
        if (m_namedSlotsPanelOpen)
            m_androidPanelOpen = false;
    };

    CommandSection destinationSection("Destinations", destinationLine, scale,
                                      sectionRounding);
    WrappedButtonRow destinationRow(gap, rowGap, destinationSection.ContentLeft(),
                                    destinationSection.ContentRight());

    if (const FilterEntry* imageFilter = findFilter("filter:image")) {
        const bool imageActive = activeToken() == imageFilter->token;
        destinationRow.PlaceNext(ToolbarButtonWidth("Image"));
        if (ToolbarButton("destination_image", "Image", imageActive, colors,
                          "Show captured images")) {
            selectFilter(*imageFilter);
        }
        destinationRow.Added();
    }

    const int androidCount = popupSettings.showDestinationCounts
        ? static_cast<int>(app->GetAndroidClipboardEntries().size()) : -1;
    destinationRow.PlaceNext(ToolbarButtonWidth("Android", androidCount));
    if (ToolbarButton("destination_android", "Android", m_androidPanelOpen,
                      colors, "Open the Android clipboard", androidCount)) {
        toggleAndroid();
    }
    destinationRow.Added();

    const int slotCount = popupSettings.showDestinationCounts
        ? static_cast<int>(app->GetNamedSlots().size()) : -1;
    destinationRow.PlaceNext(ToolbarButtonWidth("Slots", slotCount));
    if (ToolbarButton("destination_slots", "Slots", m_namedSlotsPanelOpen,
                      colors, "Open named clipboard slots", slotCount)) {
        toggleSlots();
    }
    destinationRow.Added();
    for (const CustomActionDefinition& action : customActions) {
        if (action.placement != CustomActionPlacement::Destinations ||
            !CustomActionMatches(action, customActionContext))
            continue;
        const std::string id = "custom_destination_" +
            std::to_string(action.actionId);
        const std::string label = action.icon.empty()
            ? action.label : action.icon + " " + action.label;
        const std::string tooltip = std::string(CustomActionInputName(action.input)) +
            " -> " + CustomActionOutputName(action.output);
        destinationRow.PlaceNext(ToolbarButtonWidth(label.c_str()));
        if (ToolbarButton(id.c_str(), label.c_str(), false, colors,
                          tooltip.c_str()))
            RunCustomAction(action.actionId);
        destinationRow.Added();
    }
    destinationSection.End();
    ImGui::Spacing();
}
