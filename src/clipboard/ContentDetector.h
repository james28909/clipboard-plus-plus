#pragma once
#include "ClipboardItem.h"
#include <string>

// All methods are static - no state.
class ContentDetector {
public:
    // Run all checks and return OR'd ContentTag bits.
    static uint32_t DetectTags(const std::string& text);

    static bool IsURL(const std::string& t);
    static bool IsEmail(const std::string& t);
    static bool IsJSON(const std::string& t);
    static bool IsXML(const std::string& t);
    static bool IsCSV(const std::string& t);
    static bool IsMarkdown(const std::string& t);
    static bool IsSQL(const std::string& t);
    static bool IsCode(const std::string& t);
    static bool IsCommand(const std::string& t);
    static bool IsUUID(const std::string& t);
    static bool IsIPAddress(const std::string& t);
    static bool IsPhone(const std::string& t);
    static bool IsDateTime(const std::string& t);
    static bool IsBase64Like(const std::string& t);
    static bool IsLogOrStackTrace(const std::string& t);
    static bool IsColorHex(const std::string& t);
    static bool IsFilePath(const std::string& t);
    static bool IsSecretPattern(const std::string& t);

    static const char* TagName(ContentTag tag);

private:
    // Trim leading + trailing ASCII whitespace in-place copy
    static std::string Trim(const std::string& s);
    static std::string Lower(std::string s);
    static uint32_t DetectPathTags(const std::string& text);
    static uint32_t TagsForExtension(const std::string& extension);
};
