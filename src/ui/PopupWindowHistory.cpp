#include "PopupWindow.h"
#include "MainWindow.h"
#include "../app/Application.h"
#include "../filters/CustomFilter.h"
#include "../hotkeys/HotkeyManager.h"
#include "../transforms/RegexTransform.h"
#include "../templates/PasteTemplate.h"
#include "../formatting/StructuredFormatter.h"
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

std::vector<uint64_t> PopupWindow::BuildVisibleHistoryItemIds(bool pinnedOnly) const {
    EnsureVisibleHistoryCache();
    return pinnedOnly ? m_visiblePinnedItemIds : m_visibleRegularItemIds;
}

void PopupWindow::InvalidateVisibleHistoryCache() const {
    m_visibleHistoryCacheValid = false;
}

void PopupWindow::EnsureVisibleHistoryCache() const {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) {
        m_visiblePinnedItemIds.clear();
        m_visibleRegularItemIds.clear();
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

    m_visiblePinnedItemIds.clear();
    m_visibleRegularItemIds.clear();
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

    // Filter one immutable history snapshot. Never retain vector indices or raw
    // pointers across calls: a paste, capture, or context action may reorder the
    // live history while the current ImGui frame is still being assembled.
    const std::vector<ClipboardItem> snapshot = hist->Snapshot();
    for (const ClipboardItem& item : snapshot) {
        if (!ItemPassesFilter(item)) continue;
        if (!lquery.empty()) {
            std::string lt = item.text;
            std::transform(lt.begin(), lt.end(), lt.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });
            if (lt.find(lquery) == std::string::npos) continue;
        }

        if (item.pinned)
            m_visiblePinnedItemIds.push_back(item.id);
        else
            m_visibleRegularItemIds.push_back(item.id);
        m_visibleItemIds.push_back(item.id);
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

    // These ID vectors are the stable frame snapshot. Mutations may reorder or
    // delete live items, but they cannot invalidate the remaining row keys.
    const std::vector<uint64_t> pinned = BuildVisibleHistoryItemIds(true);
    const std::vector<uint64_t> regular = BuildVisibleHistoryItemIds(false);

    auto drawSection = [&](const char* title,
                           const std::vector<uint64_t>& itemIds,
                           bool pinnedSection) {
        if (itemIds.empty())
            return;

        if (pinnedSection) {
            char header[128]{};
            std::snprintf(header, sizeof(header), "%s (%zu)", title, itemIds.size());
            if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Spacing();
                return;
            }
        } else {
            ImGui::TextDisabled("%s (%zu)", title, itemIds.size());
            ImGui::Separator();
        }

        for (size_t sectionSlot = 0; sectionSlot < itemIds.size(); ++sectionSlot) {
            const uint64_t itemId = itemIds[sectionSlot];
            const ClipboardItem* item = hist->GetById(itemId);
            if (!item)
                continue;

            const std::string key = HotkeyManager::SlotLabelText(static_cast<int>(sectionSlot));

            const int selectionPos = HasMultipleSelectedItems()
                ? m_itemSelection.PositionOf(itemId)
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
                std::snprintf(label, sizeof(label), " %s [%d]  %s%s##r%llu",
                              key.c_str(), selectionPos, pin, preview.c_str(),
                              static_cast<unsigned long long>(itemId));
            else
                std::snprintf(label, sizeof(label), " %s   %s%s##r%llu",
                              key.c_str(), pin, preview.c_str(),
                              static_cast<unsigned long long>(itemId));

            const bool selected = IsItemSelected(itemId);
            bool rowMutated = false;
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
                    SelectRangeTo(itemId);
                } else if (ctrlHeld) {
                    ToggleItemSelection(itemId);
                } else {
                    const bool keepMultiSelection = HasMultipleSelectedItems();
                    if (doubleClick) {
                        if (!keepMultiSelection)
                            SelectOnlyItem(itemId);
                        ClipboardItem pasteItem;
                        if (hist->GetByIdCopy(itemId, pasteItem))
                            PasteItemKeepOpen(pasteItem);
                        hist->MoveItemById(itemId, m_pasteMoveTarget);
                        rowMutated = true;
                    } else if (!keepMultiSelection) {
                        SelectOnlyItem(itemId);
                    }
                }
            }

            // Moving the current item can relocate its backing vector storage.
            // Finish this row from cached state, then continue the stable ID list.
            if (rowMutated) {
                if (isSecret) ImGui::PopStyleColor();
                continue;
            }

            if (pinnedSection) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                dl->AddCircleFilled({a.x + 7.0f, (a.y + b.y) * 0.5f},
                                    3.0f, IM_COL32(255, 196, 64, 255), 12);
            }

            const bool changed = DrawItemContextMenu(*item);
            if (!changed)
                DrawItemDragDrop(itemId);
            if (isSecret) ImGui::PopStyleColor();
        }

        ImGui::Spacing();
    };

    drawSection("Pinned entries", pinned, true);
    drawSection("History", regular, false);

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

void PopupWindow::DrawNamedSlots() {
    Application* app = Application::Get();
    if (!app) return;
    const std::vector<NamedClipboardSlot> slots = app->GetNamedSlots();
    std::string query = m_searchBuf;
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGuiWindowFlags flags = m_appearance.showScrollbars
        ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar;
    flags |= ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##named_slots", {0.0f, 0.0f}, ImGuiChildFlags_None, flags);
    ImGui::TextDisabled("Named slots (%zu)", slots.size());
    ImGui::Separator();

    size_t visible = 0;
    for (const NamedClipboardSlot& slot : slots) {
        std::string haystack = slot.name + "\n" + slot.text;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!query.empty() && haystack.find(query) == std::string::npos)
            continue;
        ++visible;
        ImGui::PushID(static_cast<int>(slot.slotId));
        ImGui::TextUnformatted(slot.name.c_str());

        const auto& bindings = app->GetHotkeySettings().bindings;
        auto binding = std::find_if(bindings.begin(), bindings.end(),
            [&](const KeyBinding& value) {
                return value.action == HotkeyAction::PasteNamedSlot &&
                       value.data == static_cast<int>(slot.slotId);
            });
        if (binding != bindings.end()) {
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", HotkeyManager::BindingText(*binding).c_str());
        }

        std::string preview = slot.text;
        std::replace(preview.begin(), preview.end(), '\r', ' ');
        std::replace(preview.begin(), preview.end(), '\n', ' ');
        if (preview.size() > 120)
            preview = preview.substr(0, 117) + "...";
        ImGui::TextDisabled("%s", preview.empty() ? "(empty)" : preview.c_str());
        if (ImGui::SmallButton("Paste"))
            PasteNamedSlot(slot.slotId);
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy"))
            app->CopyTextToClipboard(slot.text);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (slots.empty())
        ImGui::TextDisabled("Create a named slot in Settings > Hotkeys.");
    else if (visible == 0)
        ImGui::TextDisabled("No named slots match the search.");
    SmoothScrollCurrentWindow("popup_named_slots_scroll", 78.0f);
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
            if (ids.size() == 2 && ImGui::MenuItem("Compare selected items")) {
                ClipboardItem left;
                ClipboardItem right;
                if (hist->GetByIdCopy(ids[0], left) && hist->GetByIdCopy(ids[1], right))
                    MainWindow::OpenDiffView(left, right);
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
            const std::vector<RegexTransformDefinition> transforms =
                app->GetRegexTransforms();
            if (ImGui::BeginMenu("Paste with transform...",
                                 item.IsText() && !transforms.empty())) {
                for (const RegexTransformDefinition& transform : transforms) {
                    ImGui::PushID(static_cast<int>(transform.transformId));
                    if (ImGui::MenuItem(transform.name.c_str())) {
                        PasteItemWithTransformKeepOpen(item, transform);
                        hist->MoveItemById(itemId, m_pasteMoveTarget);
                        changed = true;
                    }
                    ImGui::PopID();
                    if (changed)
                        break;
                }
                ImGui::EndMenu();
            }
            const bool canFormat = (item.tags & (TAG_JSON | TAG_XML | TAG_SQL)) != 0;
            if (ImGui::BeginMenu("Paste formatted...", canFormat)) {
                const auto pasteFormatted = [&](const char* label,
                                                StructuredFormat format) {
                    if (ImGui::MenuItem(label) &&
                        PasteItemFormattedKeepOpen(item, format)) {
                        hist->MoveItemById(itemId, m_pasteMoveTarget);
                        changed = true;
                    }
                };
                if ((item.tags & TAG_JSON) != 0)
                    pasteFormatted("JSON", StructuredFormat::Json);
                if ((item.tags & TAG_XML) != 0)
                    pasteFormatted("XML", StructuredFormat::Xml);
                if ((item.tags & TAG_SQL) != 0)
                    pasteFormatted("SQL", StructuredFormat::Sql);
                ImGui::EndMenu();
            }
            const bool hasPasteAs = std::any_of(item.formats.begin(), item.formats.end(),
                [](const ClipboardFormatRecord& format) {
                    return format.replaySafe &&
                           format.status == ClipboardFormatStatus::Preserved &&
                           !format.data.empty() && format.formatId != CF_LOCALE;
                });
            if (ImGui::BeginMenu("Paste as...", hasPasteAs)) {
                for (const ClipboardFormatRecord& format : item.formats) {
                    if (!format.replaySafe ||
                        format.status != ClipboardFormatStatus::Preserved ||
                        format.data.empty() || format.formatId == CF_LOCALE)
                        continue;
                    ImGui::PushID(static_cast<int>(format.order));
                    std::string label = format.name + "  (" +
                        std::to_string(format.data.size()) + " bytes)";
                    if (ImGui::MenuItem(label.c_str())) {
                        PasteItemAsFormatKeepOpen(item, format);
                        hist->MoveItemById(itemId, m_pasteMoveTarget);
                        changed = true;
                    }
                    ImGui::PopID();
                    if (changed)
                        break;
                }
                ImGui::EndMenu();
            }
        }
        const std::vector<PasteTemplateDefinition> pasteTemplates =
            app->GetPasteTemplates();
        const bool templateInputsAreText = std::all_of(ids.begin(), ids.end(),
            [&](uint64_t id) {
                ClipboardItem selected;
                return hist->GetByIdCopy(id, selected) && selected.IsText();
            });
        const std::string templateMenuLabel = multi
            ? "Paste " + std::to_string(ids.size()) + " selected with template..."
            : "Paste with template...";
        if (ImGui::BeginMenu(templateMenuLabel.c_str(),
                             !pasteTemplates.empty() && templateInputsAreText)) {
            if (multi) {
                ImGui::TextDisabled("{{1}}, {{2}}, ... use the displayed selection order");
                ImGui::Separator();
            }
            for (const PasteTemplateDefinition& pasteTemplate : pasteTemplates) {
                ImGui::PushID(static_cast<int>(pasteTemplate.templateId));
                if (ImGui::MenuItem(pasteTemplate.name.c_str()) &&
                    PasteSelectionWithTemplateKeepOpen(ids, pasteTemplate)) {
                    hist->MoveItemsById(ids, m_pasteMoveTarget);
                    changed = true;
                }
                ImGui::PopID();
                if (changed)
                    break;
            }
            ImGui::EndMenu();
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
