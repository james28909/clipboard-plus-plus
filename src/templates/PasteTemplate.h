#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct PasteTemplateDefinition {
    int64_t templateId{};
    std::string name;
    std::string body;
    int64_t createdAtMs{};
    int64_t updatedAtMs{};
};

struct PasteTemplateResult {
    bool ok{false};
    std::string output;
    std::string error;
};

std::string ValidatePasteTemplate(const PasteTemplateDefinition& value);
PasteTemplateResult ApplyPasteTemplate(
    const PasteTemplateDefinition& value,
    const std::vector<std::string>& numberedValues,
    const std::vector<std::pair<std::string, std::string>>& namedValues);
