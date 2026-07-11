#pragma once

#include "../clipboard/ClipboardItem.h"
#include <string>
#include <vector>

enum class CustomFilterMode {
    Contains = 0,
    StartsWith = 1,
    Wildcard = 2,
    Regex = 3,
};

enum class CustomFilterTarget {
    Text = 0,
    Preview = 1,
    Path = 2,
    Tags = 3,
    SourceApp = 4,
};

struct CustomFilter {
    std::string id;
    std::string name;
    bool enabled{true};
    CustomFilterMode mode{CustomFilterMode::Contains};
    CustomFilterTarget target{CustomFilterTarget::Text};
    std::string pattern;
    bool caseSensitive{false};
    bool multiline{false};
    bool dotMatchesNewline{false};
    bool routeToProfile{false};
    bool routeMove{false};
    std::string routeProfileId;
};

struct CustomFilterValidation {
    bool ok{true};
    std::string message;
};

const char* CustomFilterModeName(CustomFilterMode mode);
const char* CustomFilterTargetName(CustomFilterTarget target);
std::string NewCustomFilterId();
CustomFilterValidation ValidateCustomFilter(const CustomFilter& filter);
bool CustomFilterMatches(const CustomFilter& filter, const ClipboardItem& item);
void ClearCustomFilterRegexCache();
