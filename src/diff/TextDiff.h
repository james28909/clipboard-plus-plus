#pragma once

#include <string>
#include <vector>

enum class DiffRowKind { Equal, Changed, LeftOnly, RightOnly };

struct TextDiffRow {
    std::string left;
    std::string right;
    DiffRowKind kind{DiffRowKind::Equal};
};

std::vector<TextDiffRow> BuildTextDiff(const std::string& left,
                                       const std::string& right);
