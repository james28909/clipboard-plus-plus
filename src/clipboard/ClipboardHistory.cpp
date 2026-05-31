#include "ClipboardHistory.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <utility>

ClipboardHistory::ClipboardHistory(int maxItems)
    : m_maxItems(maxItems)
{}

bool ClipboardHistory::Push(ClipboardItem item) {
    std::lock_guard<std::mutex> lock(m_mutex);

    item.id        = m_nextId++;
    item.timestamp = std::chrono::system_clock::now();

    // ── Deduplication ─────────────────────────────────────────────────────────
    // For text items: if identical text already exists, move it rather than add
    if (!item.text.empty() && item.type != ContentType::Image) {
        auto it = std::find_if(m_items.begin(), m_items.end(),
            [&item](const ClipboardItem& e) {
                return e.type == item.type && e.text == item.text;
            });

        if (it != m_items.end()) {
            ClipboardItem existing = std::move(*it);
            m_items.erase(it);
            if (m_newAtTop)
                m_items.insert(m_items.begin(), std::move(existing));
            else
                m_items.push_back(std::move(existing));
            return false; // was a duplicate
        }
    }

    // ── New item ──────────────────────────────────────────────────────────────
    if (m_newAtTop)
        m_items.insert(m_items.begin(), std::move(item));
    else
        m_items.push_back(std::move(item));

    TrimToLimit();
    return true;
}

size_t ClipboardHistory::Size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_items.size();
}

const ClipboardItem* ClipboardHistory::Get(size_t index) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_items.size()) return nullptr;
    return &m_items[index];
}

const ClipboardItem* ClipboardHistory::GetById(uint64_t id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_items.begin(), m_items.end(),
        [id](const ClipboardItem& e) { return e.id == id; });
    return it != m_items.end() ? &(*it) : nullptr;
}

bool ClipboardHistory::GetCopy(size_t index, ClipboardItem& out) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_items.size()) return false;
    out = m_items[index];
    return true;
}

bool ClipboardHistory::GetByIdCopy(uint64_t id, ClipboardItem& out) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_items.begin(), m_items.end(),
        [id](const ClipboardItem& e) { return e.id == id; });
    if (it == m_items.end()) return false;
    out = *it;
    return true;
}

std::vector<size_t> ClipboardHistory::Search(const std::string& query) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<size_t> results;
    if (query.empty()) return results;

    // Lower-case the query once
    std::string lq = query;
    std::transform(lq.begin(), lq.end(), lq.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    for (size_t i = 0; i < m_items.size(); ++i) {
        std::string lt = m_items[i].text;
        std::transform(lt.begin(), lt.end(), lt.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (lt.find(lq) != std::string::npos)
            results.push_back(i);
    }
    return results;
}

void ClipboardHistory::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_items.clear();
}

bool ClipboardHistory::MoveItem(size_t index, MoveTarget target) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_items.size() || target == MoveTarget::None)
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

    return true;
}

bool ClipboardHistory::MoveItemById(uint64_t id, MoveTarget target) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (target == MoveTarget::None) return true;

    auto it = std::find_if(m_items.begin(), m_items.end(),
        [id](const ClipboardItem& e) { return e.id == id; });
    if (it == m_items.end()) return false;

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

    return true;
}

bool ClipboardHistory::MoveItemsByIdBefore(const std::vector<uint64_t>& ids, uint64_t beforeId) {
    std::lock_guard<std::mutex> lock(m_mutex);
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
    return true;
}

void ClipboardHistory::SetMaxItems(int n) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxItems = (n > 0) ? n : 1;
    TrimToLimit();
}

void ClipboardHistory::SetNewItemsAtTop(bool top) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_newAtTop = top;
}

void ClipboardHistory::SetOverflowCallback(OverflowCb cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_overflowCb = std::move(cb);
}

// Called with lock already held
void ClipboardHistory::TrimToLimit() {
    while (static_cast<int>(m_items.size()) > m_maxItems) {
        // Oldest item is at the opposite end from where new items land
        if (m_newAtTop) {
            if (m_overflowCb) m_overflowCb(m_items.back());
            m_items.pop_back();
        } else {
            if (m_overflowCb) m_overflowCb(m_items.front());
            m_items.erase(m_items.begin());
        }
    }
}
