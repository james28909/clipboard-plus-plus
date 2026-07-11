#pragma once

#include <string>
#include <vector>

namespace androidsync {

bool SendItemsToAndroid(const std::string& endpoint,
                        const std::vector<std::string>& texts,
                        std::string* error = nullptr);

bool RequestAndroidSyncToWindows(const std::string& endpoint,
                                 std::string* error = nullptr);

bool CheckAndroidHealth(const std::string& endpoint,
                        std::string* error = nullptr);

}
