#include "PasteTemplate.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

std::string Trim(std::string value) {
    auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && space(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && space(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ParseNumber(const std::string& token, size_t& index) {
    if (token.empty() || !std::all_of(token.begin(), token.end(),
        [](unsigned char c) { return std::isdigit(c) != 0; }))
        return false;
    const unsigned long long parsed = std::strtoull(token.c_str(), nullptr, 10);
    if (parsed == 0 || parsed > static_cast<unsigned long long>(SIZE_MAX))
        return false;
    index = static_cast<size_t>(parsed - 1);
    return true;
}

PasteTemplateResult Render(
    const PasteTemplateDefinition& value,
    const std::vector<std::string>* numberedValues,
    const std::vector<std::pair<std::string, std::string>>* namedValues) {
    PasteTemplateResult result;
    size_t cursor = 0;
    while (cursor < value.body.size()) {
        const size_t open = value.body.find("{{", cursor);
        if (open == std::string::npos) {
            result.output.append(value.body, cursor, std::string::npos);
            break;
        }
        result.output.append(value.body, cursor, open - cursor);
        const size_t close = value.body.find("}}", open + 2);
        if (close == std::string::npos) {
            result.error = "Unclosed template placeholder at offset " +
                           std::to_string(open) + ".";
            return result;
        }

        const std::string token = Trim(value.body.substr(open + 2, close - open - 2));
        size_t numberedIndex = 0;
        if (ParseNumber(token, numberedIndex)) {
            if (numberedValues) {
                if (numberedIndex >= numberedValues->size()) {
                    result.error = "Template requires item {{" + token + "}}.";
                    return result;
                }
                result.output += (*numberedValues)[numberedIndex];
            }
        } else if (token.size() > 5 && Lower(token.substr(0, 5)) == "slot:") {
            const std::string slotName = Trim(token.substr(5));
            if (slotName.empty()) {
                result.error = "Named-slot placeholder requires a name.";
                return result;
            }
            if (namedValues) {
                const std::string wanted = Lower(slotName);
                auto found = std::find_if(namedValues->begin(), namedValues->end(),
                    [&](const auto& entry) { return Lower(entry.first) == wanted; });
                if (found == namedValues->end()) {
                    result.error = "Named slot '" + slotName + "' does not exist.";
                    return result;
                }
                result.output += found->second;
            }
        } else {
            result.error = "Unknown template placeholder {{" + token + "}}.";
            return result;
        }
        cursor = close + 2;
    }
    result.ok = true;
    return result;
}

} // namespace

std::string ValidatePasteTemplate(const PasteTemplateDefinition& value) {
    if (Trim(value.name).empty()) return "Template name is required.";
    if (value.body.empty()) return "Template body is required.";
    return Render(value, nullptr, nullptr).error;
}

PasteTemplateResult ApplyPasteTemplate(
    const PasteTemplateDefinition& value,
    const std::vector<std::string>& numberedValues,
    const std::vector<std::pair<std::string, std::string>>& namedValues) {
    return Render(value, &numberedValues, &namedValues);
}
