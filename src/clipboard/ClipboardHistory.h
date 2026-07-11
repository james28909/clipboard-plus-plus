#pragma once
#include "ClipboardItem.h"
#include <vector>
#include <functional>
#include <mutex>

static constexpr int kMaxClipboardHistoryItems = 500;

class ClipboardHistory {
public:
    using OverflowCb = std::function<void(ClipboardItem)>;
    using ChangedCb = std::function<void()>;

    enum class MoveTarget {
        None,
        Top,
        Bottom,
    };

    explicit ClipboardHistory(int maxItems = 100);

    // Push a new item. Returns false if it was a duplicate (existing moved).
    bool Push(ClipboardItem item);
    bool Insert(ClipboardItem item, size_t index);

    // Read access - index 0 is the most-recently-added end
    size_t Size() const;
    size_t PinnedSize() const;
    const ClipboardItem* Get(size_t index) const;
    const ClipboardItem* GetById(uint64_t id) const;
    bool GetCopy(size_t index, ClipboardItem& out) const;
    bool GetByIdCopy(uint64_t id, ClipboardItem& out) const;
    bool GetPinnedCopy(size_t slot, ClipboardItem& out) const;
    bool GetRegularCopy(size_t slot, ClipboardItem& out) const;
    std::vector<ClipboardItem> Snapshot() const;
    void LoadSnapshot(std::vector<ClipboardItem> items, uint64_t nextId);

    // Returns indices of items whose preview matches query (case-insensitive)
    std::vector<size_t> Search(const std::string& query) const;

    void Clear();
    bool RemoveItemById(uint64_t id);
    bool MoveItem(size_t index, MoveTarget target);
    bool MoveItemById(uint64_t id, MoveTarget target);
    bool MoveItemsByIdBefore(const std::vector<uint64_t>& ids, uint64_t beforeId);
    bool SetPinnedById(uint64_t id, bool pinned);
    bool SetImageSourceFileByHash(uint64_t contentHash,
                                  const std::string& sourceFilePath,
                                  const std::string& description);
    void SetMaxItems(int n);
    void SetNewItemsAtTop(bool top);
    void SetOverflowCallback(OverflowCb cb);
    void SetChangedCallback(ChangedCb cb);

    uint64_t NextId() const { return m_nextId; }

private:
    size_t CountPinnedLocked() const;
    void NormalizePinnedOrderLocked();
    void TrimToLimit();

    mutable std::recursive_mutex m_mutex;
    std::vector<ClipboardItem> m_items;
    int                        m_maxItems{100};
    bool                       m_newAtTop{true};
    uint64_t                   m_nextId{1};
    OverflowCb                 m_overflowCb;
    ChangedCb                  m_changedCb;
};
