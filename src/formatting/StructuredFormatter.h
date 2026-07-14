#pragma once

#include <string>

enum class StructuredFormat { Json, Xml, Sql };

struct StructuredFormatResult {
    bool ok{false};
    std::string output;
    std::string error;
};

StructuredFormatResult FormatStructuredText(const std::string& input,
                                             StructuredFormat format);
