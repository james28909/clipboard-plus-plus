#include "CustomFilter.h"
#include "../clipboard/ContentDetector.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace {

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool StartsWith(const std::string& text, const std::string& prefix) {
    return prefix.size() <= text.size() &&
           std::equal(prefix.begin(), prefix.end(), text.begin());
}

bool WildcardMatch(const char* text, const char* pat) {
    const char* star = nullptr;
    const char* retry = nullptr;
    while (*text) {
        if (*pat == '?' || *pat == *text) {
            ++text;
            ++pat;
        } else if (*pat == '*') {
            star = pat++;
            retry = text;
        } else if (star) {
            pat = star + 1;
            text = ++retry;
        } else {
            return false;
        }
    }
    while (*pat == '*')
        ++pat;
    return *pat == '\0';
}

std::string TargetText(const CustomFilter& filter, const ClipboardItem& item) {
    switch (filter.target) {
    case CustomFilterTarget::Preview:
        return item.Preview(4096);
    case CustomFilterTarget::Path:
        if (!item.sourceFilePath.empty())
            return item.sourceFilePath;
        return item.text;
    case CustomFilterTarget::Tags: {
        std::string tags;
        for (int bit = 0; bit < 31; ++bit) {
            const auto tag = static_cast<ContentTag>(1u << bit);
            if ((item.tags & tag) != 0) {
                if (!tags.empty())
                    tags += ' ';
                tags += ContentDetector::TagName(tag);
            }
        }
        return tags;
    }
    case CustomFilterTarget::SourceApp:
        return item.sourceProcess;
    case CustomFilterTarget::Text:
    default:
        return item.text;
    }
}

std::string RegexKey(const CustomFilter& filter) {
    std::ostringstream out;
    out << filter.pattern << '\n'
        << filter.caseSensitive << filter.multiline << filter.dotMatchesNewline;
    return out.str();
}

struct RegexCodeDeleter {
    void operator()(pcre2_code* code) const {
        if (code)
            pcre2_code_free(code);
    }
};

using RegexCodePtr = std::unique_ptr<pcre2_code, RegexCodeDeleter>;

uint32_t RegexOptions(const CustomFilter& filter) {
    uint32_t options = PCRE2_UTF;
    if (!filter.caseSensitive)
        options |= PCRE2_CASELESS;
    if (filter.multiline)
        options |= PCRE2_MULTILINE;
    if (filter.dotMatchesNewline)
        options |= PCRE2_DOTALL;
    return options;
}

RegexCodePtr CompileRegex(const CustomFilter& filter, std::string* error) {
    int errorNumber = 0;
    PCRE2_SIZE errorOffset = 0;
    pcre2_code* code = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(filter.pattern.c_str()),
        PCRE2_ZERO_TERMINATED,
        RegexOptions(filter),
        &errorNumber,
        &errorOffset,
        nullptr);

    if (!code) {
        PCRE2_UCHAR buffer[256]{};
        pcre2_get_error_message(errorNumber, buffer, sizeof(buffer));
        if (error) {
            *error = "PCRE2 error at offset " + std::to_string(static_cast<size_t>(errorOffset)) +
                     ": " + reinterpret_cast<const char*>(buffer);
        }
        return nullptr;
    }

    pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
    return RegexCodePtr(code);
}

bool RegexMatches(const CustomFilter& filter, const std::string& text) {
    static std::unordered_map<std::string, RegexCodePtr> cache;
    const std::string key = RegexKey(filter);

    auto it = cache.find(key);
    if (it == cache.end()) {
        auto compiled = CompileRegex(filter, nullptr);
        if (!compiled)
            return false;
        it = cache.emplace(key, std::move(compiled)).first;
    }

    pcre2_match_data* matchData = pcre2_match_data_create_from_pattern(it->second.get(), nullptr);
    if (!matchData)
        return false;

    int result = pcre2_jit_match(
        it->second.get(),
        reinterpret_cast<PCRE2_SPTR>(text.c_str()),
        text.size(),
        0,
        0,
        matchData,
        nullptr);
    if (result == PCRE2_ERROR_JIT_BADOPTION) {
        result = pcre2_match(
            it->second.get(),
            reinterpret_cast<PCRE2_SPTR>(text.c_str()),
            text.size(),
            0,
            0,
            matchData,
            nullptr);
    }
    pcre2_match_data_free(matchData);
    return result >= 0;
}

} // namespace

const char* CustomFilterModeName(CustomFilterMode mode) {
    switch (mode) {
    case CustomFilterMode::Contains:   return "Contains";
    case CustomFilterMode::StartsWith: return "Starts with";
    case CustomFilterMode::Wildcard:   return "Wildcard";
    case CustomFilterMode::Regex:      return "Regex (PCRE2)";
    default:                           return "Contains";
    }
}

const char* CustomFilterTargetName(CustomFilterTarget target) {
    switch (target) {
    case CustomFilterTarget::Text:      return "Text";
    case CustomFilterTarget::Preview:   return "Preview";
    case CustomFilterTarget::Path:      return "Path";
    case CustomFilterTarget::Tags:      return "Tags";
    case CustomFilterTarget::SourceApp: return "Source app";
    default:                            return "Text";
    }
}

std::string NewCustomFilterId() {
    static std::atomic<unsigned> seq{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return "filter-" + std::to_string(now) + "-" + std::to_string(++seq);
}

CustomFilterValidation ValidateCustomFilter(const CustomFilter& filter) {
    if (filter.name.empty())
        return {false, "Filter name is required."};
    if (filter.pattern.empty())
        return {false, "Pattern is required."};
    if (filter.mode == CustomFilterMode::Regex) {
        std::string error;
        auto compiled = CompileRegex(filter, &error);
        if (!compiled)
            return {false, error};
    }
    return {true, {}};
}

bool CustomFilterMatches(const CustomFilter& filter, const ClipboardItem& item) {
    if (!filter.enabled || filter.pattern.empty())
        return false;

    std::string text = TargetText(filter, item);
    std::string pattern = filter.pattern;
    if (!filter.caseSensitive && filter.mode != CustomFilterMode::Regex) {
        text = LowerAscii(std::move(text));
        pattern = LowerAscii(std::move(pattern));
    }

    switch (filter.mode) {
    case CustomFilterMode::Contains:
        return text.find(pattern) != std::string::npos;
    case CustomFilterMode::StartsWith:
        return StartsWith(text, pattern);
    case CustomFilterMode::Wildcard:
        return WildcardMatch(text.c_str(), pattern.c_str());
    case CustomFilterMode::Regex:
        return RegexMatches(filter, text);
    default:
        return false;
    }
}

void ClearCustomFilterRegexCache() {
    // The cache is intentionally function-local in RegexMatches. Recompile on process
    // restart; changed patterns use a different cache key during the same run.
}
