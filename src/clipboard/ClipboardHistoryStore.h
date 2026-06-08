#pragma once

#include "ClipboardHistory.h"

#include <filesystem>
#include <string>

namespace ClipboardHistoryStore {
    std::filesystem::path Directory();
    std::filesystem::path PathForProfile(const std::string& profileId);
    bool Load(const std::string& profileId, ClipboardHistory& history);
    bool Save(const std::string& profileId, const ClipboardHistory& history);
}
