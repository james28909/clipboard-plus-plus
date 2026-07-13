#include "../src/clipboard/ClipboardHistoryStore.h"

#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool Expect(bool value, const char* name) {
    if (value)
        return true;
    std::cerr << "FAILED: " << name << '\n';
    return false;
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

bool WriteFile(const std::filesystem::path& path,
               const std::vector<uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

} // namespace

int main() {
    const std::filesystem::path testDirectory =
        std::filesystem::temp_directory_path() /
        ("clipboardpp-history-encryption-" + std::to_string(GetCurrentProcessId()));
    std::error_code cleanupError;
    std::filesystem::remove_all(testDirectory, cleanupError);
    std::filesystem::create_directories(testDirectory, cleanupError);
    ClipboardHistoryStore::SetDirectoryForTesting(testDirectory);

    const std::string profileId = "migration-test";
    const std::string secret = "unique plaintext clipboard secret 7d9f4c";
    const std::string legacyJson =
        "{\"version\":1,\"profileId\":\"migration-test\",\"nextId\":2,"
        "\"pinned-history\":[],\"regular-history\":[{\"id\":1,"
        "\"type\":\"text\",\"text\":\"" + secret + "\"}]}";

    {
        std::ofstream legacy(ClipboardHistoryStore::LegacyPathForProfile(profileId),
                             std::ios::binary | std::ios::trunc);
        legacy << legacyJson;
    }

    bool ok = true;
    ClipboardHistory migratedHistory;
    const auto migrationResult =
        ClipboardHistoryStore::Load(profileId, migratedHistory);
    ok &= Expect(migrationResult == ClipboardHistoryStore::LoadResult::Migrated,
                 "legacy history migrates");
    ok &= Expect(std::filesystem::exists(ClipboardHistoryStore::PathForProfile(profileId)),
                 "encrypted history exists");
    ok &= Expect(!std::filesystem::exists(
                    ClipboardHistoryStore::LegacyPathForProfile(profileId)),
                 "plaintext history is removed after verification");

    ClipboardItem migratedItem;
    ok &= Expect(migratedHistory.GetCopy(0, migratedItem) && migratedItem.text == secret,
                 "migration preserves clipboard text");

    std::vector<uint8_t> encrypted =
        ReadFile(ClipboardHistoryStore::PathForProfile(profileId));
    ok &= Expect(!encrypted.empty(), "encrypted file is non-empty");
    ok &= Expect(std::search(encrypted.begin(), encrypted.end(),
                            secret.begin(), secret.end()) == encrypted.end(),
                 "encrypted file does not contain plaintext");

    ClipboardHistory reloadedHistory;
    ok &= Expect(ClipboardHistoryStore::Load(profileId, reloadedHistory) ==
                     ClipboardHistoryStore::LoadResult::Loaded,
                 "encrypted history reloads");
    ClipboardItem reloadedItem;
    ok &= Expect(reloadedHistory.GetCopy(0, reloadedItem) && reloadedItem.text == secret,
                 "encrypted reload preserves clipboard text");

    const std::string otherProfileId = "wrong-profile";
    std::filesystem::copy_file(
        ClipboardHistoryStore::PathForProfile(profileId),
        ClipboardHistoryStore::PathForProfile(otherProfileId),
        std::filesystem::copy_options::overwrite_existing, cleanupError);
    ClipboardHistory wrongProfileHistory;
    ok &= Expect(ClipboardHistoryStore::Load(otherProfileId, wrongProfileHistory) ==
                     ClipboardHistoryStore::LoadResult::InvalidFormat,
                 "encrypted history is bound to its profile ID");

    if (!encrypted.empty())
        encrypted.back() ^= 0x5a;
    ok &= Expect(WriteFile(ClipboardHistoryStore::PathForProfile(profileId), encrypted),
                 "corrupt test file is written");
    ClipboardHistory rejectedHistory;
    ok &= Expect(ClipboardHistoryStore::Load(profileId, rejectedHistory) ==
                     ClipboardHistoryStore::LoadResult::DecryptionFailed,
                 "tampered encrypted history is rejected");
    ok &= Expect(std::filesystem::exists(ClipboardHistoryStore::PathForProfile(profileId)),
                 "failed encrypted history is preserved");

    std::filesystem::remove_all(testDirectory, cleanupError);
    if (!ok)
        return 1;
    std::cout << "history encryption tests passed\n";
    return 0;
}
