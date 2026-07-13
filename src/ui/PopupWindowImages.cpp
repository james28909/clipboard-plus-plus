#include "PopupWindow.h"
#include "../app/Application.h"
#include "../clipboard/ImageStore.h"
#include "../hotkeys/HotkeyManager.h"
#include "ImGuiWidgets.h"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

using ImGuiWidgets::SmoothScrollCurrentWindow;

// -- Image browser -------------------------------------------------------------

void PopupWindow::ClearThumbCache() {
    for (auto& kv : m_thumbCache)
        if (kv.second.srv) kv.second.srv->Release();
    m_thumbCache.clear();
}

void PopupWindow::DrawImageBrowser() {
    Application* app = Application::Get();
    ImageStore* store = app ? app->GetImageStore() : nullptr;

    if (!store || !store->IsOpen()) {
        ImGui::Spacing();
        ImGui::TextDisabled("  Image store not available.");
        return;
    }

    // -- Sub-filter bar -------------------------------------------------------
    const PopupToggleColors tc = GetPopupToggleColors(m_appearance);
    static const char* kSortLabels[] = {"Newest", "Oldest", "Largest", "Smallest"};
    static const char* kDateLabels[] = {"All time", "Today", "This week", "This month"};
    static const char* kSizeLabels[] = {"Any size", "> 100 KB", "> 500 KB", "> 1 MB"};

    ImGui::SetNextItemWidth(88.0f);
    ImGui::Combo("##img_sort", &m_imgSort, kSortLabels, 4);
    ImGui::SameLine(0, 6.0f);
    ImGui::SetNextItemWidth(88.0f);
    ImGui::Combo("##img_date", &m_imgDateFilter, kDateLabels, 4);
    ImGui::SameLine(0, 6.0f);
    ImGui::SetNextItemWidth(84.0f);
    ImGui::Combo("##img_size", &m_imgSizeFilter, kSizeLabels, 4);
    ImGui::SameLine(0, 6.0f);
    if (ImGui::SmallButton("Refresh")) {
        m_imageListDirty = true;
        m_selectedImageIds.clear();
        m_imgSelectionAnchorId.clear();
        ClearThumbCache();
    }
    ImGui::Separator();

    // -- Load or refresh image list -------------------------------------------
    if (m_imageListDirty) {
        m_cachedImageList = store->ListAll();
        m_imageListDirty = false;
        m_selectedImageIds.erase(
            std::remove_if(m_selectedImageIds.begin(), m_selectedImageIds.end(),
                [&](const std::string& id) {
                    return std::none_of(m_cachedImageList.begin(), m_cachedImageList.end(),
                        [&](const ImageRecord& record) { return record.id == id; });
                }),
            m_selectedImageIds.end());
        if (!m_imgSelectionAnchorId.empty() &&
            std::none_of(m_cachedImageList.begin(), m_cachedImageList.end(),
                [&](const ImageRecord& record) { return record.id == m_imgSelectionAnchorId; })) {
            m_imgSelectionAnchorId.clear();
        }
    }

    // -- Filter + sort --------------------------------------------------------
    using namespace std::chrono;
    const int64_t nowMs = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    const int64_t kDayMs   = 24LL * 3600 * 1000;
    const int64_t kWeekMs  = 7  * kDayMs;
    const int64_t kMonthMs = 30 * kDayMs;

    std::vector<const ImageRecord*> rows;
    rows.reserve(m_cachedImageList.size());
    for (const auto& r : m_cachedImageList) {
        const int64_t age = nowMs - r.capturedAt;
        if (m_imgDateFilter == 1 && age > kDayMs)   continue;
        if (m_imgDateFilter == 2 && age > kWeekMs)  continue;
        if (m_imgDateFilter == 3 && age > kMonthMs) continue;
        if (m_imgSizeFilter == 1 && r.byteSize < 100  * 1024) continue;
        if (m_imgSizeFilter == 2 && r.byteSize < 500  * 1024) continue;
        if (m_imgSizeFilter == 3 && r.byteSize < 1024 * 1024) continue;

        // Search text: match dimensions or source process
        const std::string query(m_searchBuf);
        if (!query.empty()) {
            const std::string dim = std::to_string(r.width) + "x" + std::to_string(r.height);
            auto lcMatch = [&](const std::string& s) {
                std::string ls = s, lq = query;
                auto lc = [](unsigned char c){ return (char)std::tolower(c); };
                std::transform(ls.begin(), ls.end(), ls.begin(), lc);
                std::transform(lq.begin(), lq.end(), lq.begin(), lc);
                return ls.find(lq) != std::string::npos;
            };
            if (!lcMatch(dim) && !lcMatch(r.sourceProc) && !lcMatch(r.profileId)) continue;
        }
        rows.push_back(&r);
    }

    std::sort(rows.begin(), rows.end(), [&](const ImageRecord* a, const ImageRecord* b) {
        switch (m_imgSort) {
        case 1: return a->capturedAt < b->capturedAt;
        case 2: return a->byteSize   > b->byteSize;
        case 3: return a->byteSize   < b->byteSize;
        default: return a->capturedAt > b->capturedAt;
        }
    });

    ImGui::TextDisabled("  %zu image%s", rows.size(), rows.size() == 1 ? "" : "s");

    // -- Scrollable image list ------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGuiWindowFlags listFlags = m_appearance.showScrollbars
        ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar;
    listFlags |= ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::BeginChild("##imglist", {0.f, 0.f}, ImGuiChildFlags_None, listFlags)) {
        if (rows.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("  No images match the current filters.");
        }

        bool openCtxMenu = false;
        auto releaseImageThumb = [&](const std::string& id) {
            auto thumbIt = m_thumbCache.find(id);
            if (thumbIt == m_thumbCache.end())
                return;
            if (thumbIt->second.srv)
                thumbIt->second.srv->Release();
            m_thumbCache.erase(thumbIt);
        };
        auto isImgSelected = [&](const std::string& id) {
            return std::find(m_selectedImageIds.begin(), m_selectedImageIds.end(), id) !=
                   m_selectedImageIds.end();
        };
        auto selectOnlyImage = [&](const std::string& id) {
            m_selectedImageIds = {id};
            m_imgSelectionAnchorId = id;
        };
        auto toggleImage = [&](const std::string& id) {
            auto it = std::find(m_selectedImageIds.begin(), m_selectedImageIds.end(), id);
            if (it == m_selectedImageIds.end())
                m_selectedImageIds.push_back(id);
            else
                m_selectedImageIds.erase(it);
            m_imgSelectionAnchorId = id;
        };
        auto selectImageRange = [&](const std::string& id) {
            if (m_imgSelectionAnchorId.empty()) {
                selectOnlyImage(id);
                return;
            }
            size_t a = rows.size(), b = rows.size();
            for (size_t idx = 0; idx < rows.size(); ++idx) {
                if (rows[idx]->id == m_imgSelectionAnchorId) a = idx;
                if (rows[idx]->id == id) b = idx;
            }
            if (a == rows.size() || b == rows.size()) {
                selectOnlyImage(id);
                return;
            }
            const size_t first = std::min(a, b);
            const size_t last = std::max(a, b);
            for (size_t idx = first; idx <= last; ++idx) {
                if (!isImgSelected(rows[idx]->id))
                    m_selectedImageIds.push_back(rows[idx]->id);
            }
        };
        auto contextImageIds = [&]() {
            if (!m_imgCtxMenuId.empty() && isImgSelected(m_imgCtxMenuId) && !m_selectedImageIds.empty())
                return m_selectedImageIds;
            return std::vector<std::string>{m_imgCtxMenuId};
        };

        for (const ImageRecord* r : rows) {
            ImGui::PushID(r->id.c_str());

            // -- Lazy-load thumbnail ------------------------------------------
            ThumbEntry& entry = m_thumbCache[r->id];
            if (!entry.srv && entry.w == 0) {
                int tw = 0, th = 0;
                entry.srv = store->CreateThumbnailSRV(r->id, m_device, 128, tw, th);
                entry.w = tw ? tw : 1;
                entry.h = th ? th : 1;
            }

            // -- Row start ----------------------------------------------------
            const float kThumbH = 78.0f;
            const float kThumbMaxW = 120.0f;
            const float rowPadY = 5.0f;
            ImVec2 rowTL = ImGui::GetCursorScreenPos();

            // Thumbnail display dimensions
            float dispH = kThumbH;
            float dispW = (entry.h > 0)
                ? std::min(kThumbMaxW, dispH * (float)entry.w / (float)entry.h)
                : kThumbH;

            ImGui::Dummy({0.f, rowPadY}); // top padding

            if (entry.srv) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
                ImGui::Image((ImTextureID)(intptr_t)entry.srv, {dispW, dispH});
            } else {
                // Placeholder box while thumb loads (or failed)
                ImVec2 boxTL = ImGui::GetCursorScreenPos();
                boxTL.x += 4.0f;
                ImGui::GetWindowDrawList()->AddRectFilled(
                    boxTL, {boxTL.x + 64.0f, boxTL.y + kThumbH},
                    ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
                ImGui::Dummy({68.0f, kThumbH});
            }
            ImGui::SameLine(0, 10.0f);

            // -- Metadata -----------------------------------------------------
            ImGui::BeginGroup();

            // Line 1: dimensions + format badge
            const char* fmtTag =
                r->storedFormat == StoredFormat::Jpeg   ? "JPEG" :
                r->storedFormat == StoredFormat::RawDib ? "RAW"  : "PNG";
            ImGui::Text("%d \xc3\x97 %d", r->width, r->height);  // × (UTF-8 multiplication sign)
            ImGui::SameLine(0, 6.0f);
            ImGui::TextDisabled("[%s]", fmtTag);

            // Line 2: file size
            char sizeStr[32];
            if (r->byteSize >= 1024 * 1024)
                snprintf(sizeStr, sizeof(sizeStr), "%.2f MB", (double)r->byteSize / (1024.0 * 1024.0));
            else
                snprintf(sizeStr, sizeof(sizeStr), "%.1f KB", (double)r->byteSize / 1024.0);
            ImGui::TextDisabled("%s", sizeStr);

            // Line 3: source process (trimmed to 28 chars)
            if (!r->sourceProc.empty()) {
                const std::string& sp = r->sourceProc;
                if (sp.size() <= 28)
                    ImGui::TextDisabled("%s", sp.c_str());
                else
                    ImGui::TextDisabled("%.28s\xe2\x80\xa6", sp.c_str());  // …
            }

            // Line 4: captured date/time + relative age
            {
                const int64_t ageMs = nowMs - r->capturedAt;
                char relStr[24];
                if (ageMs < 60000)         snprintf(relStr, sizeof(relStr), "%llds ago", (long long)(ageMs/1000));
                else if (ageMs < 3600000)  snprintf(relStr, sizeof(relStr), "%lldm ago", (long long)(ageMs/60000));
                else if (ageMs < 86400000) snprintf(relStr, sizeof(relStr), "%lldh ago", (long long)(ageMs/3600000));
                else                       snprintf(relStr, sizeof(relStr), "%lldd ago", (long long)(ageMs/86400000));

                const time_t t = static_cast<time_t>(r->capturedAt / 1000);
                std::tm tm{};
#ifdef _WIN32
                localtime_s(&tm, &t);
#else
                localtime_r(&t, &tm);
#endif
                char dtStr[32];
                strftime(dtStr, sizeof(dtStr), "%b %d, %I:%M %p", &tm);
                ImGui::TextDisabled("%s  (%s)", dtStr, relStr);
            }

            ImGui::EndGroup();

            ImGui::Dummy({0.f, rowPadY}); // bottom padding

            // -- Click detection over the full row ----------------------------
            ImVec2 rowBR = ImGui::GetCursorScreenPos();
            rowBR.x = rowTL.x + ImGui::GetContentRegionAvail().x + ImGui::GetScrollX();

            const bool hovered = ImGui::IsMouseHoveringRect(
                rowTL, {rowBR.x, rowBR.y > rowTL.y ? rowBR.y : rowTL.y + kThumbH + rowPadY * 2});

            const bool selected = isImgSelected(r->id);
            if (selected || hovered) {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    rowTL, {rowBR.x, ImGui::GetCursorScreenPos().y},
                    ImGui::GetColorU32(selected ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered,
                                       selected ? 0.55f : 0.4f));
            }

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ActivateKeyboardCapture();
                ReleaseSearchCapture();
                HotkeyManager* hotkeys = app ? app->GetHotkeys() : nullptr;
                const bool ctrlHeld = hotkeys ? hotkeys->IsCtrlDown()
                                              : ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0);
                const bool shiftHeld = hotkeys ? hotkeys->IsShiftDown()
                                               : ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
                if (shiftHeld) {
                    selectImageRange(r->id);
                } else if (ctrlHeld) {
                    toggleImage(r->id);
                } else {
                    selectOnlyImage(r->id);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        ClipboardItem fake;
                        fake.type         = ContentType::Image;
                        fake.imageStoreId = r->id;
                        fake.imageW       = r->width;
                        fake.imageH       = r->height;
                        PasteItemKeepOpen(fake);
                    }
                }
            }

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                if (!isImgSelected(r->id))
                    selectOnlyImage(r->id);
                m_imgCtxMenuId = r->id;
                openCtxMenu = true;
            }

            ImGui::Separator();
            ImGui::PopID();
        }

        if (openCtxMenu)
            ImGui::OpenPopup("##img_ctx");

        if (ImGui::BeginPopup("##img_ctx")) {
            std::vector<std::string> ids = contextImageIds();
            const bool multi = ids.size() > 1;
            if (multi)
                ImGui::TextDisabled("%zu selected images", ids.size());
            else
                ImGui::TextDisabled("%s", m_imgCtxMenuId.substr(0, 8).c_str());
            ImGui::Separator();
            if (!multi && ImGui::MenuItem("Paste image")) {
                ClipboardItem fake;
                fake.type         = ContentType::Image;
                fake.imageStoreId = m_imgCtxMenuId;
                PasteItemKeepOpen(fake);
            }
            if (!multi && ImGui::MenuItem("Copy to clipboard")) {
                ClipboardItem fake;
                fake.type         = ContentType::Image;
                fake.imageStoreId = m_imgCtxMenuId;
                WriteToClipboard(fake);
            }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.35f, 0.35f, 1.f));
            if (ImGui::MenuItem(multi ? "Delete selected from database" : "Delete from database")) {
                int deleted = 0;
                std::vector<std::string> failedIds;
                for (const std::string& id : ids) {
                    if (id.empty())
                        continue;
                    if (store->Delete(id)) {
                        ++deleted;
                        releaseImageThumb(id);
                    } else {
                        failedIds.push_back(id);
                    }
                }
                if (deleted > 0) {
                    m_cachedImageList.erase(
                        std::remove_if(m_cachedImageList.begin(), m_cachedImageList.end(),
                            [&](const ImageRecord& record) {
                                return std::find(ids.begin(), ids.end(), record.id) != ids.end();
                            }),
                        m_cachedImageList.end());
                    m_imageListDirty = true;
                }
                if (Application* app = Application::Get()) {
                    std::ostringstream out;
                    out << "image delete requested=" << ids.size()
                        << " deleted=" << deleted
                        << " failed=" << failedIds.size();
                    if (!failedIds.empty())
                        out << " firstFailed=" << failedIds.front();
                    app->AddDeveloperEvent(out.str());
                }
                m_selectedImageIds.clear();
                m_imgSelectionAnchorId.clear();
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        SmoothScrollCurrentWindow("popup_images", 112.0f, 0.22f);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
