#include "ClipboardItem.h"
#include <algorithm>

std::string ClipboardItem::Preview(size_t maxLen) const {
    if (type == ContentType::Image)
        return text; // already "[Image WxH]"

    std::string out;
    out.reserve(maxLen + 4);

    for (char c : text) {
        if (out.size() >= maxLen) { out += "..."; break; }
        out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    return out;
}
