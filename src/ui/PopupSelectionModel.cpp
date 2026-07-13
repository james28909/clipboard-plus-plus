#include "PopupSelectionModel.h"

#include <algorithm>

bool PopupSelectionModel::Contains(uint64_t itemId) const {
    return m_idSet.find(itemId) != m_idSet.end();
}

std::vector<uint64_t> PopupSelectionModel::ContextFor(uint64_t itemId) const {
    if (Contains(itemId) && !m_ids.empty())
        return m_ids;
    return {itemId};
}

int PopupSelectionModel::PositionOf(uint64_t itemId) const {
    auto it = std::find(m_ids.begin(), m_ids.end(), itemId);
    if (it == m_ids.end())
        return -1;
    return static_cast<int>(std::distance(m_ids.begin(), it)) + 1;
}

void PopupSelectionModel::Clear() {
    m_ids.clear();
    m_idSet.clear();
    m_anchorId = 0;
}

void PopupSelectionModel::SelectOnly(uint64_t itemId) {
    m_ids = {itemId};
    m_idSet.clear();
    m_idSet.insert(itemId);
    m_anchorId = itemId;
}

void PopupSelectionModel::Toggle(uint64_t itemId) {
    auto setIt = m_idSet.find(itemId);
    if (setIt == m_idSet.end()) {
        m_ids.push_back(itemId);
        m_idSet.insert(itemId);
    } else {
        auto it = std::find(m_ids.begin(), m_ids.end(), itemId);
        if (it != m_ids.end())
            m_ids.erase(it);
        m_idSet.erase(setIt);
    }
    m_anchorId = itemId;
}

void PopupSelectionModel::SelectRangeTo(uint64_t itemId,
                                        const std::vector<uint64_t>& visibleIds) {
    if (m_anchorId == 0) {
        SelectOnly(itemId);
        return;
    }

    auto anchor = std::find(visibleIds.begin(), visibleIds.end(), m_anchorId);
    auto target = std::find(visibleIds.begin(), visibleIds.end(), itemId);
    if (anchor == visibleIds.end() || target == visibleIds.end()) {
        SelectOnly(itemId);
        return;
    }

    const int anchorIndex = static_cast<int>(std::distance(visibleIds.begin(), anchor));
    const int targetIndex = static_cast<int>(std::distance(visibleIds.begin(), target));
    const int step = anchorIndex <= targetIndex ? 1 : -1;

    for (int i = anchorIndex; ; i += step) {
        if (!Contains(visibleIds[i])) {
            m_ids.push_back(visibleIds[i]);
            m_idSet.insert(visibleIds[i]);
        }
        if (i == targetIndex)
            break;
    }
}
