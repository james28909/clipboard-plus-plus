#include "../src/filters/CustomFilter.h"
#include "../src/clipboard/ContentDetector.h"
#include "../src/transforms/RegexTransform.h"
#include "../src/templates/PasteTemplate.h"
#include "../src/formatting/StructuredFormatter.h"
#include "../src/diff/TextDiff.h"

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

    RegexTransformDefinition reorder;
    reorder.name = "Last, first to first last";
    reorder.pattern = R"(^([^,]+),\s*(.+)$)";
    reorder.replacement = "$2 $1";
    ok &= Expect(ValidateRegexTransform(reorder).empty(),
                 "regex transform validates");
    RegexTransformResult reordered = ApplyRegexTransform(reorder, "Doe, Jane");
    ok &= Expect(reordered.ok && reordered.replacements == 1 &&
                 reordered.output == "Jane Doe",
                 "capture-group replacement transforms text");

    RegexTransformDefinition digits;
    digits.name = "Digits only";
    digits.pattern = "[^0-9]+";
    digits.replacement = "";
    digits.replaceAll = true;
    RegexTransformResult phone = ApplyRegexTransform(digits, "+1 (555) 123-4567");
    ok &= Expect(phone.ok && phone.output == "15551234567" &&
                 phone.replacements > 1,
                 "global replacement removes every match");

    digits.replaceAll = false;
    RegexTransformResult firstOnly = ApplyRegexTransform(digits, "a1-b2");
    ok &= Expect(firstOnly.ok && firstOnly.output == "1-b2" &&
                 firstOnly.replacements == 1,
                 "single replacement stops after first match");

    RegexTransformResult unchanged = ApplyRegexTransform(reorder, "Jane Doe");
    ok &= Expect(unchanged.ok && unchanged.replacements == 0 &&
                 unchanged.output == "Jane Doe",
                 "no-match transform preserves input");

    PasteTemplateDefinition pasteTemplate;
    pasteTemplate.name = "Contact summary";
    pasteTemplate.body = "Name: {{1}}\nEmail: {{slot:Email}}\nPhone: {{2}}";
    ok &= Expect(ValidatePasteTemplate(pasteTemplate).empty(),
                 "paste template validates");
    const PasteTemplateResult templated = ApplyPasteTemplate(
        pasteTemplate, {"Jane Doe", "555-0100"}, {{"email", "jane@example.com"}});
    ok &= Expect(templated.ok && templated.output ==
                 "Name: Jane Doe\nEmail: jane@example.com\nPhone: 555-0100",
                 "numbered and case-insensitive named-slot placeholders interpolate");
    const PasteTemplateResult missingItem = ApplyPasteTemplate(
        pasteTemplate, {"Jane Doe"}, {{"Email", "jane@example.com"}});
    ok &= Expect(!missingItem.ok && !missingItem.error.empty(),
                 "missing numbered template input is rejected");
    pasteTemplate.body = "{{unknown}}";
    ok &= Expect(!ValidatePasteTemplate(pasteTemplate).empty(),
                 "unknown template placeholder is rejected");

    const StructuredFormatResult prettyJson = FormatStructuredText(
        R"({"name":"Jane","items":[1,2]})", StructuredFormat::Json);
    ok &= Expect(prettyJson.ok && prettyJson.output.find("\n  \"name\"") !=
                     std::string::npos,
                 "JSON pretty-print indents parsed content");
    const StructuredFormatResult prettyXml = FormatStructuredText(
        "<root><item>value</item></root>", StructuredFormat::Xml);
    ok &= Expect(prettyXml.ok && prettyXml.output.find("  <item>") !=
                     std::string::npos,
                 "XML pretty-print indents nested tags");
    const StructuredFormatResult prettySql = FormatStructuredText(
        "select a from table where id = 1", StructuredFormat::Sql);
    ok &= Expect(prettySql.ok && prettySql.output.find("\nfrom") !=
                     std::string::npos && prettySql.output.find("\nwhere") !=
                     std::string::npos,
                 "SQL pretty-print separates major clauses");

    const std::vector<TextDiffRow> diff = BuildTextDiff(
        "same\nold\ntail", "same\nnew\ntail\nadded");
    ok &= Expect(diff.size() == 4 && diff[0].kind == DiffRowKind::Equal &&
                 diff[1].kind == DiffRowKind::Changed &&
                 diff[3].kind == DiffRowKind::RightOnly,
                 "line diff aligns equal, changed, and added rows");

    if (!ok)
        return 1;
    std::cout << "filter matcher tests passed\n";
    return 0;
}
