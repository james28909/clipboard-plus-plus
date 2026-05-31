#include "ContentDetector.h"
#include <regex>
#include <cctype>
#include <algorithm>

// ── Helpers ───────────────────────────────────────────────────────────────────

std::string ContentDetector::Trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\n\r\f\v");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\n\r\f\v");
    return s.substr(b, e - b + 1);
}

// ── Public ────────────────────────────────────────────────────────────────────

uint32_t ContentDetector::DetectTags(const std::string& text) {
    if (text.empty() || text.size() > 200'000) return TAG_NONE;

    uint32_t tags = TAG_NONE;
    if (IsURL(text))           tags |= TAG_URL;
    if (IsEmail(text))         tags |= TAG_EMAIL;
    if (IsJSON(text))          tags |= TAG_JSON;
    if (IsXML(text))           tags |= TAG_XML;
    if (IsColorHex(text))      tags |= TAG_HEX;
    if (IsFilePath(text))      tags |= TAG_PATH;
    if (IsSecretPattern(text)) tags |= TAG_SECRET;
    return tags;
}

bool ContentDetector::IsURL(const std::string& text) {
    std::string t = Trim(text);
    // Must be single-line to count as a clean URL
    if (t.find('\n') != std::string::npos) return false;
    return t.rfind("http://",  0) == 0
        || t.rfind("https://", 0) == 0
        || t.rfind("ftp://",   0) == 0
        || t.rfind("www.",     0) == 0;
}

bool ContentDetector::IsEmail(const std::string& text) {
    std::string t = Trim(text);
    if (t.find('\n') != std::string::npos) return false;
    static const std::regex re(
        R"([a-zA-Z0-9._%+\-]+@[a-zA-Z0-9.\-]+\.[a-zA-Z]{2,})",
        std::regex::optimize);
    return std::regex_search(t, re);
}

bool ContentDetector::IsJSON(const std::string& text) {
    std::string t = Trim(text);
    return !t.empty() && (t.front() == '{' || t.front() == '[');
}

bool ContentDetector::IsXML(const std::string& text) {
    std::string t = Trim(text);
    return t.rfind("<?xml", 0) == 0
        || t.rfind("<!DOCTYPE", 0) == 0
        || (!t.empty() && t.front() == '<' && t.find('>') != std::string::npos);
}

bool ContentDetector::IsColorHex(const std::string& text) {
    std::string t = Trim(text);
    static const std::regex re(
        R"(^#([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$)",
        std::regex::optimize);
    return std::regex_match(t, re);
}

bool ContentDetector::IsFilePath(const std::string& text) {
    std::string t = Trim(text);
    if (t.size() < 3) return false;
    // Windows absolute: C:\ or C:/
    bool winAbs = std::isalpha((unsigned char)t[0]) && t[1] == ':'
               && (t[2] == '\\' || t[2] == '/');
    // UNC: \\server\share
    bool unc = t.rfind("\\\\", 0) == 0;
    return winAbs || unc;
}

bool ContentDetector::IsSecretPattern(const std::string& text) {
    // AWS access key ID
    static const std::regex aws(R"(AKIA[0-9A-Z]{16})", std::regex::optimize);
    if (std::regex_search(text, aws)) return true;

    // GitHub personal / fine-grained / OAuth tokens
    static const std::regex gh(R"(gh[pousr]_[A-Za-z0-9]{36,})", std::regex::optimize);
    if (std::regex_search(text, gh)) return true;

    // PEM key block
    if (text.find("-----BEGIN") != std::string::npos) return true;

    // JWT: ey... . ey... . <sig>
    static const std::regex jwt(
        R"(ey[A-Za-z0-9_\-]{10,}\.ey[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,})",
        std::regex::optimize);
    if (std::regex_search(text, jwt)) return true;

    // Slack bot / user tokens
    static const std::regex slack(R"(xox[bpoa]-[0-9A-Za-z\-]{10,})", std::regex::optimize);
    if (std::regex_search(text, slack)) return true;

    return false;
}

const char* ContentDetector::TagName(ContentTag tag) {
    switch (tag) {
    case TAG_URL:    return "URL";
    case TAG_EMAIL:  return "Email";
    case TAG_CODE:   return "Code";
    case TAG_JSON:   return "JSON";
    case TAG_XML:    return "XML";
    case TAG_SQL:    return "SQL";
    case TAG_HEX:    return "Color";
    case TAG_PATH:   return "Path";
    case TAG_PHONE:  return "Phone";
    case TAG_SECRET: return "SECRET";
    default:         return "";
    }
}
