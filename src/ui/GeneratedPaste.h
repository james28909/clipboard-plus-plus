#pragma once

#include "../clipboard/ClipboardItem.h"

#include <string>

struct GeneratedPasteProvenance {
    std::string sourceProcess;
    std::string destinationProcess;
};

ClipboardItem MakeGeneratedTextPaste(std::string text);
bool IsClipboardPlusPlusGeneratedPaste(const ClipboardItem& item);
GeneratedPasteProvenance DescribeGeneratedPaste(
    const ClipboardItem& item,
    std::string destinationProcess);
