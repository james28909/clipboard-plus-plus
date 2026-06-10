#include "ClipboardHistory.h"
#include "../util/Win32Util.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <utility>

ClipboardHistory::ClipboardHistory(int maxItems)
    : m_maxItems(maxItems)
{}

bool ClipboardHistory::Push(ClipboardItem item) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    const auto now = std::chrono::system_clock::now();
    item.EnsureContentHash();

    // -- Deduplication ---------------------------------------------------------
    // For text items: if identical text already exists, move it rather than add
    if (item.contentHash != 0) {
        auto it = std::find_if(m_items.begin(), m_items.end(),
            [&item](const ClipboardItem& e) {
                return e.contentHash == item.contentHash;
            });

        if (it != m_items.end()) {
            ClipboardItem existing = std::move(*it);
            existing.type = item.type;
            existing.tags = item.tags;
            existing.text = std::move(item.text);
            existing.imageData = std::move(item.imageData);
            existing.imageW = item.imageW;
            existing.imageH = item.imageH;
            existing.sourceProcess = std::move(item.sourceProcess);
            existing.timestamp = now;
            existing.updatedAt = now;
            m_items.erase(it);
            if (m_newAtTop)
                m_items.insert(m_items.begin(), std::move(existing));
            else
                m_items.push_back(std::move(existing));
            NormalizePinnedOrderLocked();
            if (m_changedCb) m_changedCb();
            return false; // was a duplicate
        }
    }

    // -- New item --------------------------------------------------------------
    item.id = m_nextId++;
    item.timestamp = now;
    item.createdAt = now;
    item.updatedAt = now;

    if (m_newAtTop)
        m_items.insert(m_items.begin(), std::move(item));
    else
        m_items.push_back(std::move(item));

    NormalizePinnedOrderLocked();
    TrimToLimit();
    if (m_changedCb) m_changedCb();
    return true;
}

bool ClipboardHistory::Insert(ClipboardItem item, size_t index) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    const auto now = std::chrono::system_clock::now();
    item.EnsureContentHash();

    if (item.contentHash != 0) {
        auto it = std::find_if(m_items.begin(), m_items.end(),
            [&item](const ClipboardItem& e) {
                return e.contentHash == item.contentHash;
            });
        if (it != m_items.end()) {
            item.id = it->id;
            item.createdAt = it->createdAt;
            item.pinned = it->pinned;
            m_items.erase(it);
        }
    }

    if (item.id == 0)
        item.id = m_nextId++;
    if (item.createdAt.time_since_epoch().count() == 0)
        item.createdAt = now;
    item.timestamp = now;
    item.updatedAt = now;

    index = std::min(index, m_items.size());
    m_items.insert(m_items.begin() + static_cast<std::ptrdiff_t>(index), std::move(item));
    NormalizePinnedOrderLocked();
    TrimToLimit();
    if (m_changedCb) m_changedCb();
    return true;
}

size_t ClipboardHistory::Size() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_items.size();
}

size_t ClipboardHistory::PinnedSize() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return CountPinnedLocked();
}

const ClipboardItem* ClipboardHistory::Get(size_t index) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (index >= m_items.size()) return nullptr;
    return &m_items[index];
}

const ClipboardItem* ClipboardHistory::GetById(uint64_t id) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = std::find_if(m_items.begin(), m_items.end(),
        [id](const ClipboardItem& e) { return e.id == id; });
    return it != m_items.end() ? &(*it) : nullptr;
}

bool ClipboardHistory::GetCopy(size_t index, ClipboardItem& out) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (index >= m_items.size()) return false;
    out = m_items[index];
    return true;
}

bool ClipboardHistory::GetByIdCopy(uint64_t id, ClipboardItem& out) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = std::find_if(m_items.begin(), m_items.end(),
        [id](const ClipboardItem& e) { return e.id == id; });
    if (it == m_items.end()) return false;
    out = *it;
    return true;
}

bool ClipboardHistory::GetPinnedCopy(size_t slot, ClipboardItem& out) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    size_t seen = 0;
    for (const ClipboardItem& item : m_items) {
        if (!item.pinned)
            continue;
        if (seen == slot) {
            out = item;
            return true;
        }
        ++seen;
    }
    return false;
}

bool ClipboardHistory::GetRegularCopy(size_t slot, ClipboardItem& out) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    size_t seen = 0;
    for (const ClipboardItem& item : m_items) {
        if (item.pinned)
            continue;
        if (seen == slot) {
            out = item;
            return true;
        }
        ++seen;
    }
    return false;
}

std::vector<ClipboardItem> ClipboardHistory::Snapshot() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_items;
}

void ClipboardHistory::LoadSnapshot(std::vector<ClipboardItem> items, uint64_t nextId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_items = std::move(items);
    m_nextId = std::max<uint64_t>(nextId, 1);
    NormalizePinnedOrderLocked();
    TrimToLimit();
}

std::vector<size_t> ClipboardHistory::Search(const std::string& query) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<size_t> results;
    if (query.empty()) return results;

    const std::string lq = win32util::ToLower(query);

    for (size_t i = 0; i < m_items.size(); ++i) {
        if (win32util::ToLower(m_items[i].text).find(lq) != std::string::npos)
            results.push_back(i);
    }
    return results;
}

void ClipboardHistory::Clear() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_items.clear();
    if (m_changedCb) m_changedCb();
}

bool ClipboardHistory::RemoveItemById(uint64_t id) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = std::find_if(m_items.begin(), m_items.end(),
        [id](const ClipboardItem& e) { return e.id == id; });
    if (it == m_items.end())
        return false;

    m_items.erase(it);
    if (m_changedCb) m_changedCb();
    return true;
}

bool ClipboardHistory::MoveItem(size_t index, MoveTarget target) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (index >= m_items.size())
        return false;

    const auto now = std::chrono::system_clock::now();
    m_items[index].lastUsedAt = now;
    m_items[index].updatedAt = now;

    if (target == MoveTarget::None)
        return index < m_items.size();

    if ((target == MoveTarget::Top && index == 0) ||
        (target == MoveTarget::Bottom && index + 1 == m_items.size()))
        return true;

    ClipboardItem item = std::move(m_items[index]);
    m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(index));

    if (target == MoveTarget::Top)
        m_items.insert(m_items.begin(), std::move(item));
    else
        m_items.push_back(std::move(item));

    NormalizePinnedOrderLocked();
    if (m_changedCb) m_changedCb();
    return true;
}

bool ClipboardHistory::MoveItemById(uint64_t id, MoveTarget target) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = std::find_if(m_items.begin(), m_items.end(),
        [id](const ClipboardItem& e) { return e.id == id; });
    if (it == m_items.end()) return false;

    const auto now = std::chrono::system_clock::now();
    it->lastUsedAt = now;
    it->updatedAt = now;

    if (target == MoveTarget::None) {
        if (m_changedCb) m_changedCb();
        return true;
    }

    const size_t index = static_cast<size_t>(std::distance(m_items.begin(), it));
    if ((target == MoveTarget::Top && index == 0) ||
        (target == MoveTarget::Bottom && index + 1 == m_items.size()))
        return true;

    ClipboardItem item = std::move(*it);
    m_items.erase(it);

    if (target == MoveTarget::Top)
        m_items.insert(m_items.begin(), std::move(item));
    else
        m_items.push_back(std::move(item));

    NormalizePinnedOrderLocked();
    if (m_changedCb) m_changedCb();
    return true;
}

bool ClipboardHistory::MoveItemsByIdBefore(const std::vector<uint64_t>& ids, uint64_t beforeId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (ids.empty()) return true;
    if (std::find(ids.begin(), ids.end(), beforeId) != ids.end())
        return true;

    std::vector<ClipboardItem> moving;
    moving.reserve(ids.size());

    for (uint64_t id : ids) {
        auto it = std::find_if(m_items.begin(), m_items.end(),
            [id](const ClipboardItem& e) { return e.id == id; });
        if (it == m_items.end()) continue;

        moving.push_back(std::move(*it));
        m_items.erase(it);
    }

    if (moving.empty()) return false;

    auto before = std::find_if(m_items.begin(), m_items.end(),
        [beforeId](const ClipboardItem& e) { return e.id == beforeId; });
    auto insertAt = (before == m_items.end()) ? m_items.end() : before;

    m_items.insert(insertAt,
                   std::make_move_iterator(moving.begin()),
                   std::make_move_iterator(moving.end()));
    NormalizePinnedOrderLocked();
    if (m_changedCb) m_changedCb();
    return true;
}

bool ClipboardHistory::SetPinnedById(uint64_t id, bool pinned) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = std::find_if(m_items.begin(), m_items.end(),
        [id](const ClipboardItem& e) { return e.id == id; });
    if (it == m_items.end())
        return false;

    it->pinned = pinned;
    it->updatedAt = std::chrono::system_clock::now();
    NormalizePinnedOrderLocked();
    if (m_changedCb) m_changedCb();
    return true;
}

void ClipboardHistory::SetMaxItems(int n) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_maxItems = (n > 0) ? n : 1;
    TrimToLimit();
}

void ClipboardHistory::SetNewItemsAtTop(bool top) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_newAtTop = top;
}

void ClipboardHistory::SetOverflowCallback(OverflowCb cb) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_overflowCb = std::move(cb);
}

void ClipboardHistory::SetChangedCallback(ChangedCb cb) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_changedCb = std::move(cb);
}

size_t ClipboardHistory::CountPinnedLocked() const {
    return static_cast<size_t>(std::count_if(m_items.begin(), m_items.end(),
        [](const ClipboardItem& item) { return item.pinned; }));
}

void ClipboardHistory::NormalizePinnedOrderLocked() {
    std::stable_partition(m_items.begin(), m_items.end(),
        [](const ClipboardItem& item) { return item.pinned; });
}

// Called with lock already held
void ClipboardHistory::TrimToLimit() {
    while (static_cast<int>(m_items.size()) > m_maxItems) {
        auto it = std::find_if(m_items.rbegin(), m_items.rend(),
            [](const ClipboardItem& item) { return !item.pinned; });
        if (it != m_items.rend()) {
            auto eraseIt = std::next(it).base();
            if (m_overflowCb) m_overflowCb(*eraseIt);
            m_items.erase(eraseIt);
            continue;
        }

        if (m_overflowCb) m_overflowCb(m_items.back());
        m_items.pop_back();
    }
}
