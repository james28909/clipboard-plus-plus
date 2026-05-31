#include "ClipboardHistory.h"
#include <algorithm>
#include <cctype>

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
