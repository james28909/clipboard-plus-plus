#include "StructuredFormatter.h"

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace {

std::string Trim(std::string value) {
    auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && space(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && space(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

StructuredFormatResult FormatJson(const std::string& input) {
    try {
        return {true, nlohmann::json::parse(input).dump(2), {}};
    } catch (const std::exception& error) {
        return {false, {}, std::string("JSON parse error: ") + error.what()};
    }
}

StructuredFormatResult FormatXml(const std::string& input) {
    std::string output;
    int depth = 0;
    size_t cursor = 0;
    while (cursor < input.size()) {
        const size_t open = input.find('<', cursor);
        if (open == std::string::npos) {
            const std::string text = Trim(input.substr(cursor));
            if (!text.empty()) output += std::string(depth * 2, ' ') + text + "\n";
            break;
        }
        const std::string text = Trim(input.substr(cursor, open - cursor));
        if (!text.empty()) output += std::string(depth * 2, ' ') + text + "\n";
        const size_t close = input.find('>', open + 1);
        if (close == std::string::npos)
            return {false, {}, "XML contains an unclosed tag."};
        const std::string tag = input.substr(open, close - open + 1);
        const bool closing = tag.size() > 1 && tag[1] == '/';
        const bool declaration = tag.size() > 1 && (tag[1] == '?' || tag[1] == '!');
        const bool selfClosing = tag.size() > 1 && tag[tag.size() - 2] == '/';
        if (closing) depth = std::max(0, depth - 1);
        output += std::string(depth * 2, ' ') + tag + "\n";
        if (!closing && !declaration && !selfClosing) ++depth;
        cursor = close + 1;
    }
    if (depth != 0)
        return {false, {}, "XML tags are not balanced."};
    if (!output.empty()) output.pop_back();
    return {true, std::move(output), {}};
}

std::string Upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

StructuredFormatResult FormatSql(const std::string& input) {
    const std::unordered_set<std::string> lineKeywords = {
        "SELECT", "FROM", "WHERE", "HAVING", "LIMIT", "VALUES", "SET",
        "INSERT", "UPDATE", "DELETE", "JOIN", "LEFT", "RIGHT", "INNER",
        "OUTER", "GROUP", "ORDER", "UNION"
    };
    std::istringstream stream(input);
    std::string token;
    std::string output;
    bool first = true;
    while (stream >> token) {
        std::string bare = token;
        while (!bare.empty() && (bare.back() == ',' || bare.back() == ';'))
            bare.pop_back();
        const std::string upper = Upper(bare);
        if (!first && lineKeywords.find(upper) != lineKeywords.end()) {
            while (!output.empty() && output.back() == ' ') output.pop_back();
            output += '\n';
        }
        output += token;
        output += ' ';
        first = false;
    }
    output = Trim(output);
    if (output.empty()) return {false, {}, "SQL text is empty."};
    return {true, std::move(output), {}};
}

} // namespace

StructuredFormatResult FormatStructuredText(const std::string& input,
                                             StructuredFormat format) {
    switch (format) {
    case StructuredFormat::Json: return FormatJson(input);
    case StructuredFormat::Xml: return FormatXml(input);
    case StructuredFormat::Sql: return FormatSql(input);
    }
    return {false, {}, "Unknown structured format."};
}
