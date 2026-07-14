#include "GeneratedPaste.h"

#include "../clipboard/ContentDetector.h"

#include <utility>

ClipboardItem MakeGeneratedTextPaste(std::string text) {
    ClipboardItem item;
    item.type = ContentType::Text;
    item.text = std::move(text);
    item.sourceProcess = "clipboardpp.exe";
    item.tags = ContentDetector::DetectTags(item.text);
    return item;
}

bool IsClipboardPlusPlusGeneratedPaste(const ClipboardItem& item) {
    return item.sourceProcess == "clipboardpp.exe";
}

GeneratedPasteProvenance DescribeGeneratedPaste(
    const ClipboardItem& item,
    std::string destinationProcess) {
    GeneratedPasteProvenance result;
    if (!IsClipboardPlusPlusGeneratedPaste(item))
        return result;
    result.sourceProcess = "clipboardpp.exe";
    result.destinationProcess = destinationProcess.empty()
        ? "unknown"
        : std::move(destinationProcess);
    return result;
}
