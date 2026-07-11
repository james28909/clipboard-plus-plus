#include "../src/filters/CustomFilter.h"
#include "../src/clipboard/ContentDetector.h"

#include <iostream>

namespace {

ClipboardItem TextItem(const char* text) {
    ClipboardItem item;
    item.type = ContentType::Text;
    item.text = text;
    item.tags = ContentDetector::DetectTags(item.text);
    return item;
}

bool Expect(bool value, const char* name) {
    if (value)
        return true;
    std::cerr << "FAILED: " << name << '\n';
    return false;
}

} // namespace

int main() {
    const ClipboardItem item = TextItem("Error: apple pie failed\npath=C:/tmp/report.json");
    bool ok = true;

    CustomFilter contains;
    contains.name = "Contains";
    contains.pattern = "APPLE PIE";
    contains.mode = CustomFilterMode::Contains;
    ok &= Expect(CustomFilterMatches(contains, item), "contains case-insensitive");

    CustomFilter starts;
    starts.name = "Starts";
    starts.pattern = "Error:";
    starts.mode = CustomFilterMode::StartsWith;
    starts.caseSensitive = true;
    ok &= Expect(CustomFilterMatches(starts, item), "starts-with");

    CustomFilter wildcard;
    wildcard.name = "Wildcard";
    wildcard.pattern = "*report.?son";
    wildcard.mode = CustomFilterMode::Wildcard;
    ok &= Expect(CustomFilterMatches(wildcard, item), "wildcard");

    CustomFilter regex;
    regex.name = "Regex";
    regex.pattern = R"((?i)error:\s+\w+\s+pie)";
    regex.mode = CustomFilterMode::Regex;
    ok &= Expect(ValidateCustomFilter(regex).ok, "regex validation");
    ok &= Expect(CustomFilterMatches(regex, item), "regex match");

    CustomFilter badRegex;
    badRegex.name = "Bad";
    badRegex.pattern = "(";
    badRegex.mode = CustomFilterMode::Regex;
    ok &= Expect(!ValidateCustomFilter(badRegex).ok, "invalid regex rejection");

    if (!ok)
        return 1;
    std::cout << "filter matcher tests passed\n";
    return 0;
}

