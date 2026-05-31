#pragma once
#include "ClipboardItem.h"
#include <string>

// All methods are static — no state.
class ContentDetector {
public:
    // Run all checks and return OR'd ContentTag bits.
    static uint32_t DetectTags(const std::string& text);

    static bool IsURL(const std::string& t);
    static bool IsEmail(const std::string& t);
    static bool IsJSON(const std::string& t);
    static bool IsXML(const std::string& t);
    static bool IsColorHex(const std::string& t);
    static bool IsFilePath(const std::string& t);
    static bool IsSecretPattern(const std::string& t);

    static const char* TagName(ContentTag tag);

private:
    // Trim leading + trailing ASCII whitespace in-place copy
    static std::string Trim(const std::string& s);
};
