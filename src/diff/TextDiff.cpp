#include "TextDiff.h"

#include <algorithm>
#include <sstream>

namespace {

std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    if (lines.empty() || (!text.empty() && text.back() == '\n'))
        lines.emplace_back();
    return lines;
}

} // namespace

std::vector<TextDiffRow> BuildTextDiff(const std::string& left,
                                       const std::string& right) {
    const std::vector<std::string> a = Lines(left);
    const std::vector<std::string> b = Lines(right);
    if (a.size() * b.size() > 1000000) {
        std::vector<TextDiffRow> rows;
        const size_t count = std::max(a.size(), b.size());
        rows.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const bool hasA = i < a.size(), hasB = i < b.size();
            rows.push_back({hasA ? a[i] : std::string{}, hasB ? b[i] : std::string{},
                !hasA ? DiffRowKind::RightOnly : !hasB ? DiffRowKind::LeftOnly :
                a[i] == b[i] ? DiffRowKind::Equal : DiffRowKind::Changed});
        }
        return rows;
    }

    std::vector<std::vector<int>> lcs(a.size() + 1,
                                      std::vector<int>(b.size() + 1));
    for (size_t i = a.size(); i-- > 0;) {
        for (size_t j = b.size(); j-- > 0;)
            lcs[i][j] = a[i] == b[j] ? lcs[i + 1][j + 1] + 1 :
                        std::max(lcs[i + 1][j], lcs[i][j + 1]);
    }

    std::vector<TextDiffRow> rows;
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        if (i < a.size() && j < b.size() && a[i] == b[j]) {
            rows.push_back({a[i++], b[j++], DiffRowKind::Equal});
        } else if (i < a.size() && j < b.size() &&
                   lcs[i + 1][j] == lcs[i][j + 1]) {
            rows.push_back({a[i++], b[j++], DiffRowKind::Changed});
        } else if (j >= b.size() ||
                   (i < a.size() && lcs[i + 1][j] > lcs[i][j + 1])) {
            rows.push_back({a[i++], {}, DiffRowKind::LeftOnly});
        } else {
            rows.push_back({{}, b[j++], DiffRowKind::RightOnly});
        }
    }
    return rows;
}
