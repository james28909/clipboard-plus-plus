#include "PopupWindow.h"
#include "../app/Application.h"
#include "../filters/CustomFilter.h"
#include "../hotkeys/HotkeyManager.h"
#include "ImGuiWidgets.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>

using ImGuiWidgets::SmoothScrollCurrentWindow;

bool PopupWindow::ItemPassesFilter(const ClipboardItem& item) const {
    if (!m_activeCustomFilterId.empty()) {
        if (Application* app = Application::Get()) {
            const auto& filters = app->GetCustomFilters();
            auto it = std::find_if(filters.begin(), filters.end(),
                [&](const CustomFilter& filter) { return filter.id == m_activeCustomFilterId; });
            if (it != filters.end())
                return CustomFilterMatches(*it, item);
        }
        return true;
    }

    switch (m_filterMode) {
    case 1: return item.IsText();
    case 2: return item.IsImage();
    case 3: return (item.tags & TAG_URL) != 0;
    case 4: return item.type == ContentType::FilePaths || (item.tags & (TAG_FILE | TAG_FOLDER | TAG_PATH)) != 0;
    case 5: return (item.tags & TAG_CODE) != 0;
    case 6: return (item.tags & TAG_SECRET) != 0;
    case 7: return (item.tags & TAG_JSON) != 0;
    case 8: return (item.tags & TAG_EMAIL) != 0;
    case 9:  return (item.tags & TAG_HEX) != 0;
    case 10: return (item.tags & TAG_COMMAND) != 0;
    default: return true;
    }
}

std::vector<size_t> PopupWindow::BuildVisibleHistoryIndices(bool pinnedOnly) const {
    EnsureVisibleHistoryCache();
    return pinnedOnly ? m_visiblePinnedIndices : m_visibleRegularIndices;
}

void PopupWindow::InvalidateVisibleHistoryCache() const {
    m_visibleHistoryCacheValid = false;
}

void PopupWindow::EnsureVisibleHistoryCache() const {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) {
        m_visiblePinnedIndices.clear();
        m_visibleRegularIndices.clear();
        m_visibleItemIds.clear();
        m_visibleHistoryCacheValid = true;
        m_visibleHistoryCacheHistory = nullptr;
        m_visibleHistoryCacheVersion = 0;
        m_visibleHistoryCacheFilterMode = m_filterMode;
        m_visibleHistoryCacheCustomFilterId = m_activeCustomFilterId;
        m_visibleHistoryCacheSearch = m_searchBuf;
        return;
    }

    const std::string query(m_searchBuf);
    const uint64_t version = hist->Version();
    if (m_visibleHistoryCacheValid &&
        m_visibleHistoryCacheHistory == hist &&
        m_visibleHistoryCacheVersion == version &&
        m_visibleHistoryCacheFilterMode == m_filterMode &&
        m_visibleHistoryCacheCustomFilterId == m_activeCustomFilterId &&
        m_visibleHistoryCacheSearch == query) {
        return;
    }

    m_visiblePinnedIndices.clear();
    m_visibleRegularIndices.clear();
    m_visibleItemIds.clear();
    m_visibleHistoryCacheHistory = hist;
    m_visibleHistoryCacheVersion = version;
    m_visibleHistoryCacheFilterMode = m_filterMode;
    m_visibleHistoryCacheCustomFilterId = m_activeCustomFilterId;
    m_visibleHistoryCacheSearch = query;
    m_visibleHistoryCacheValid = true;

    std::string lquery = query;
    std::transform(lquery.begin(), lquery.end(), lquery.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });

    for (size_t i = 0; i < hist->Size(); ++i) {
        const ClipboardItem* item = hist->Get(i);
        if (!item || !ItemPassesFilter(*item)) continue;
        if (!lquery.empty()) {
            std::string lt = item->text;
            std::transform(lt.begin(), lt.end(), lt.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });
            if (lt.find(lquery) == std::string::npos) continue;
        }

        if (item->pinned)
            m_visiblePinnedIndices.push_back(i);
        else
            m_visibleRegularIndices.push_back(i);
        m_visibleItemIds.push_back(item->id);
    }
}

std::vector<uint64_t> PopupWindow::BuildVisibleItemIds() const {
    EnsureVisibleHistoryCache();
    return m_visibleItemIds;
}

bool PopupWindow::IsItemSelected(uint64_t itemId) const {
    return m_itemSelection.Contains(itemId);
}

std::vector<uint64_t> PopupWindow::ContextSelectionFor(uint64_t itemId) const {
    return m_itemSelection.ContextFor(itemId);
}

void PopupWindow::ClearItemSelection() {
    m_itemSelection.Clear();
}

void PopupWindow::SelectOnlyItem(uint64_t itemId) {
    m_itemSelection.SelectOnly(itemId);
}

void PopupWindow::ToggleItemSelection(uint64_t itemId) {
    m_itemSelection.Toggle(itemId);
}

void PopupWindow::SelectRangeTo(uint64_t itemId) {
    m_itemSelection.SelectRangeTo(itemId, BuildVisibleItemIds());
}

void PopupWindow::DrawItemList() {
    // Image filter mode gets its own dedicated browser UI
    if (m_filterMode == 2) {
        DrawImageBrowser();
        return;
    }

    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGuiWindowFlags itemFlags = m_appearance.showScrollbars
        ? ImGuiWindowFlags_None
        : ImGuiWindowFlags_NoScrollbar;
    itemFlags |= ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##items", {0.f, 0.f}, ImGuiChildFlags_None, itemFlags);

    const std::vector<size_t> pinned = BuildVisibleHistoryIndices(true);
    const std::vector<size_t> regular = BuildVisibleHistoryIndices(false);

    auto drawSection = [&](const char* title,
                           const std::vector<size_t>& indices,
                           bool pinnedSection) -> bool {
        if (indices.empty())
            return false;

        if (pinnedSection) {
            char header[128]{};
            std::snprintf(header, sizeof(header), "%s (%zu)", title, indices.size());
            if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Spacing();
                return false;
            }
        } else {
            ImGui::TextDisabled("%s (%zu)", title, indices.size());
            ImGui::Separator();
        }

        for (size_t sectionSlot = 0; sectionSlot < indices.size(); ++sectionSlot) {
            const size_t i = indices[sectionSlot];
            const ClipboardItem* item = hist->Get(i);
            if (!item) continue;

            const std::string key = HotkeyManager::SlotLabelText(static_cast<int>(sectionSlot));

            const int selectionPos = HasMultipleSelectedItems()
                ? m_itemSelection.PositionOf(item->id)
                : -1;

            const bool isSecret = (item->tags & TAG_SECRET) != 0;
            if (isSecret)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.42f, 0.42f, 1.f));

            const float rowWidth = ImGui::GetContentRegionAvail().x;
            const int previewChars = std::max(40, static_cast<int>((rowWidth - 92.0f) / 7.0f));
            const std::string preview = item->Preview(static_cast<size_t>(previewChars));

            char label[1024]{};
            const char* pin = pinnedSection ? "[P] " : "";
            if (selectionPos >= 0)
                std::snprintf(label, sizeof(label), " %s [%d]  %s%s##r%zu",
                              key.c_str(), selectionPos, pin, preview.c_str(), i);
            else
                std::snprintf(label, sizeof(label), " %s   %s%s##r%zu",
                              key.c_str(), pin, preview.c_str(), i);

            const bool selected = IsItemSelected(item->id);
            if (ImGui::Selectable(label, selected,
                                   ImGuiSelectableFlags_SpanAllColumns |
                                   ImGuiSelectableFlags_AllowDoubleClick)) {
                ActivateKeyboardCapture();
                ReleaseSearchCapture();

                HotkeyManager* hotkeys = Application::Get() ? Application::Get()->GetHotkeys() : nullptr;
                const bool ctrlHeld = hotkeys ? hotkeys->IsCtrlDown()
                                              : ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
                const bool shiftHeld = hotkeys ? hotkeys->IsShiftDown()
                                               : ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
                const bool doubleClick = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

                if (shiftHeld) {
                    SelectRangeTo(item->id);
                } else if (ctrlHeld) {
                    ToggleItemSelection(item->id);
                } else {
                    const bool keepMultiSelection = HasMultipleSelectedItems();
                    if (doubleClick) {
                        if (isSecret) ImGui::PopStyleColor();
                        const uint64_t itemId = item->id;
                        if (!keepMultiSelection)
                            SelectOnlyItem(itemId);
                        PasteItemKeepOpen(*item);
                        hist->MoveItemById(itemId, m_pasteMoveTarget);
                        return true;
                    }
                    if (!keepMultiSelection)
                        SelectOnlyItem(item->id);
                }
            }

            if (pinnedSection) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                dl->AddCircleFilled({a.x + 7.0f, (a.y + b.y) * 0.5f},
                                    3.0f, IM_COL32(255, 196, 64, 255), 12);
            }

            if (DrawItemContextMenu(*item)) {
                if (isSecret) ImGui::PopStyleColor();
                return true;
            }
            DrawItemDragDrop(item->id);
            if (isSecret) ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        return false;
    };

    if (drawSection("Pinned entries", pinned, true) ||
        drawSection("History", regular, false)) {
        SmoothScrollCurrentWindow("popup_items", 112.0f, 0.22f);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    if (!pinned.empty() || !regular.empty()) {
        ImGui::InvisibleButton("##drop_end", {ImGui::GetContentRegionAvail().x, 8.0f});
        if (ImGui::BeginDragDropTarget()) {
            if (ImGui::AcceptDragDropPayload("CPP_HISTORY_IDS")) {
                hist->MoveItemsByIdBefore(m_dragIds, 0);
            }
            ImGui::EndDragDropTarget();
        }
    }

    if (pinned.empty() && regular.empty())
        ImGui::TextDisabled("  No items match.");

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ActivateKeyboardCapture();
        ReleaseSearchCapture();
        if (HasMultipleSelectedItems() && !ImGui::IsAnyItemHovered())
            ClearSelectedItems();
    }

    SmoothScrollCurrentWindow("popup_items", 112.0f, 0.22f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

bool PopupWindow::DrawItemContextMenu(const ClipboardItem& item) {
    Application* app = Application::Get();
    ClipboardHistory* hist = app ? app->GetHistory() : nullptr;
    if (!hist)
        return false;

    bool changed = false;
    const uint64_t itemId = item.id;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsItemSelected(itemId))
        SelectOnlyItem(itemId);

    if (ImGui::BeginPopupContextItem()) {
        ActivateKeyboardCapture();
        ReleaseSearchCapture();

        const std::vector<uint64_t> ids = ContextSelectionFor(itemId);
        const bool multi = ids.size() > 1;
        if (multi)
            ImGui::TextDisabled("%zu selected items", ids.size());
        else
            ImGui::TextDisabled("Item %llu", static_cast<unsigned long long>(itemId));
        ImGui::Separator();

        if (multi) {
            if (ImGui::MenuItem("Paste selected")) {
                PasteSelectedItemsInOrder();
                changed = true;
            }
            if (ImGui::MenuItem("Clear selection")) {
                ClearItemSelection();
                changed = true;
            }
        } else {
            if (ImGui::MenuItem("Paste")) {
                PasteItemKeepOpen(item);
                hist->MoveItemById(itemId, m_pasteMoveTarget);
                changed = true;
            }
        }
        if (!multi && ImGui::MenuItem("Copy to clipboard")) {
            WriteToClipboard(item);
        }
        if (ImGui::MenuItem(multi ? "Send selected to Android clipboard" : "Send to Android clipboard")) {
            std::vector<std::string> texts;
            for (uint64_t id : ids) {
                ClipboardItem selected;
                if (hist->GetByIdCopy(id, selected) && selected.IsText() && !selected.text.empty())
                    texts.push_back(selected.text);
            }
            std::string error;
            if (!app || !app->SendTextItemsToAndroid(texts, &error)) {
                // Keep this lightweight for the POC; the Android panel shows where to set the endpoint.
            }
        }

        if (ImGui::MenuItem(multi ? "Move selected to top" : "Move to top")) {
            hist->MoveItemsByIdToTop(ids);
            changed = true;
        }
        if (ImGui::MenuItem(multi ? "Move selected to bottom" : "Move to bottom")) {
            hist->MoveItemsByIdToBottom(ids);
            changed = true;
        }
        if (ImGui::MenuItem(multi ? "Pin selected" : (item.pinned ? "Unpin" : "Pin"))) {
            hist->SetPinnedByIdMany(ids, multi ? true : !item.pinned);
            changed = true;
        }
        if (multi && ImGui::MenuItem("Unpin selected")) {
            hist->SetPinnedByIdMany(ids, false);
            changed = true;
        }

        ImGui::Separator();
        if (ImGui::MenuItem(multi ? "Delete selected" : "Delete")) {
            hist->RemoveItemsById(ids);
            ClearItemSelection();
            changed = true;
        }

        ImGui::EndPopup();
    }

    return changed;
}

void PopupWindow::DrawItemDragDrop(uint64_t itemId) {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        if (IsItemSelected(itemId))
            m_dragIds = ContextSelectionFor(itemId);
        else
            m_dragIds = { itemId };

        const int count = static_cast<int>(m_dragIds.size());
        ImGui::SetDragDropPayload("CPP_HISTORY_IDS", &count, sizeof(count));
        ImGui::Text("%d item%s", count, count == 1 ? "" : "s");
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("CPP_HISTORY_IDS")) {
            hist->MoveItemsByIdBefore(m_dragIds, itemId);
        }
        ImGui::EndDragDropTarget();
    }
}
