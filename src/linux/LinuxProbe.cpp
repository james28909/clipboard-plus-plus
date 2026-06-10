#include "../app/ConfigStore.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ClipboardHistoryStore.h"
#include "../clipboard/ClipboardItem.h"
#include "../clipboard/ContentDetector.h"

#include <chrono>
#include <iostream>

int main() {
    AppConfig config = ConfigStore::Load();

    ClipboardHistory history;
    ClipboardHistoryStore::Load(config.activeClipboardId, history);

    ClipboardItem item;
    item.type = ContentType::Text;
    item.text = "Clipboard++ Linux probe item";
    item.sourceProcess = "clipboardpp-linux-probe";
    item.timestamp = std::chrono::system_clock::now();
    item.createdAt = item.timestamp;
    item.updatedAt = item.timestamp;
    item.tags = ContentDetector::DetectTags(item.text);
    item.EnsureContentHash();

    history.Push(std::move(item));
    const bool savedConfig = ConfigStore::Save(config);
    const bool savedHistory = ClipboardHistoryStore::Save(config.activeClipboardId, history);

    std::cout << "Clipboard++ Linux probe\n";
    std::cout << "Config: " << ConfigStore::Path() << "\n";
    std::cout << "History: " << ClipboardHistoryStore::PathForProfile(config.activeClipboardId) << "\n";
    std::cout << "Items: " << history.Size() << "\n";
    std::cout << "Saved config: " << (savedConfig ? "yes" : "no") << "\n";
    std::cout << "Saved history: " << (savedHistory ? "yes" : "no") << "\n";

    return savedConfig && savedHistory ? 0 : 1;
}
