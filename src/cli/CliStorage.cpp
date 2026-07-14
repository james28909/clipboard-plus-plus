#include "CliStorage.h"

#include "../app/ConfigStore.h"
#include "../security/EncryptedSqliteVfs.h"
#include "../util/Win32Util.h"

#include <algorithm>

namespace cli_storage {

std::filesystem::path DataDirectory() {
    return ConfigStore::Directory();
}

bool OpenProfile(const std::string& requestedProfile,
                 ProfileContext& context,
                 std::wstring& error) {
    const auto path = DataDirectory() / "clipboard.db";
    if (!EncryptedSqliteVfs::HasKey(path)) {
        error = L"Encrypted clipboard.db was not found. Start Clipboard++ once first.";
        return false;
    }

    std::string databaseError;
    if (!context.database.Open(path, &databaseError)) {
        error = L"Could not open encrypted clipboard.db: " +
                win32util::Utf8ToWide(databaseError);
        return false;
    }
    if (!context.database.LoadProfiles(context.profiles) || context.profiles.empty()) {
        error = L"The encrypted clipboard database contains no profiles.";
        return false;
    }

    std::string selected = requestedProfile;
    if (selected.empty())
        context.database.GetActiveProfileId(selected);
    if (selected.empty())
        selected = context.profiles.front().id;

    const auto match = std::find_if(
        context.profiles.begin(), context.profiles.end(),
        [&](const ClipboardProfileConfig& profile) {
            return _stricmp(profile.id.c_str(), selected.c_str()) == 0 ||
                   _stricmp(profile.name.c_str(), selected.c_str()) == 0;
        });
    if (match == context.profiles.end()) {
        error = L"Unknown clipboard profile: " + win32util::Utf8ToWide(selected);
        return false;
    }
    context.profile = *match;
    return true;
}

} // namespace cli_storage
