#pragma once

#include "../clipboard/ClipboardDatabase.h"

#include <filesystem>
#include <string>
#include <vector>

namespace cli_storage {

struct ProfileContext {
    ClipboardDatabase database;
    std::vector<ClipboardProfileConfig> profiles;
    ClipboardProfileConfig profile;
};

// Debug builds may redirect this with CLIPBOARDPP_TEST_DATA_DIR so CLI
// integration tests never touch the signed-in user's real clipboard data.
std::filesystem::path DataDirectory();

bool OpenProfile(const std::string& requestedProfile,
                 ProfileContext& context,
                 std::wstring& error);

} // namespace cli_storage
