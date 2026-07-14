#pragma once

#include "ClipboardHistory.h"

#include <filesystem>
#include <string>

namespace ClipboardHistoryStore {
    enum class LoadResult {
        Loaded,
        NotFound,
        Migrated,
        LoadedLegacy,
        DecryptionFailed,
        InvalidFormat,
        IoError,
    };

    std::filesystem::path Directory();
#ifdef CLIPBOARDPP_TESTING
    void SetDirectoryForTesting(const std::filesystem::path& directory);
#endif
    std::filesystem::path PathForProfile(const std::string& profileId);
    std::filesystem::path LegacyPathForProfile(const std::string& profileId);
    LoadResult Load(const std::string& profileId, ClipboardHistory& history);
    const char* LoadResultName(LoadResult result);
    bool AllowsPersistence(LoadResult result);
    bool Save(const std::string& profileId, const ClipboardHistory& history);
    std::string Serialize(const std::string& profileId,
                          const ClipboardHistory& history);
    bool Deserialize(const std::string& profileId, const std::string& payload,
                     ClipboardHistory& history);
}
