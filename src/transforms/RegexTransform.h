#pragma once

#include <cstdint>
#include <string>

struct RegexTransformDefinition {
    int64_t transformId{};
    std::string name;
    std::string pattern;
    std::string replacement;
    bool caseSensitive{true};
    bool multiline{false};
    bool dotMatchesNewline{false};
    bool replaceAll{true};
    int64_t createdAtMs{};
    int64_t updatedAtMs{};
};

struct RegexTransformResult {
    bool ok{false};
    std::string output;
    std::string error;
    int replacements{};
};

std::string ValidateRegexTransform(const RegexTransformDefinition& transform);
RegexTransformResult ApplyRegexTransform(const RegexTransformDefinition& transform,
                                         const std::string& input);
