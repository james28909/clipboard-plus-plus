#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

class PopupSelectionModel {
public:
    bool Contains(uint64_t itemId) const;
    bool Empty() const { return m_ids.empty(); }
    bool HasMultiple() const { return m_ids.size() > 1; }
    const std::vector<uint64_t>& Ids() const { return m_ids; }
    std::vector<uint64_t> ContextFor(uint64_t itemId) const;
    int PositionOf(uint64_t itemId) const;

    void Clear();
    void SelectOnly(uint64_t itemId);
    void Toggle(uint64_t itemId);
    void SelectRangeTo(uint64_t itemId, const std::vector<uint64_t>& visibleIds);

private:
    std::vector<uint64_t> m_ids;
    std::unordered_set<uint64_t> m_idSet;
    uint64_t m_anchorId{};
};
