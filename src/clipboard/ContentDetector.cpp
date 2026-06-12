#include "ContentDetector.h"
#include "../util/Win32Util.h"
#include <regex>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <unordered_set>
#include <vector>

// -- Helpers -------------------------------------------------------------------

std::string ContentDetector::Trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\n\r\f\v");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\n\r\f\v");
    return s.substr(b, e - b + 1);
}

std::string ContentDetector::Lower(std::string s) {
    return win32util::ToLower(std::move(s));
}

static std::string StripQuotes(std::string s) {
    auto b = s.find_first_not_of(" \t\n\r\f\v");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\n\r\f\v");
    s = s.substr(b, e - b + 1);
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                          (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

static std::vector<std::string> NonEmptyLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = StripQuotes(line);
        if (!line.empty())
            lines.push_back(line);
    }
    return lines;
}

// -- Public --------------------------------------------------------------------

uint32_t ContentDetector::DetectTags(const std::string& text) {
    if (text.empty() || text.size() > 200'000) return TAG_NONE;

    uint32_t tags = TAG_NONE;
    tags |= DetectPathTags(text);
    if (IsURL(text))           tags |= TAG_URL;
    if (IsEmail(text))         tags |= TAG_EMAIL;
    if (IsJSON(text))          tags |= TAG_JSON;
    if (IsXML(text))           tags |= TAG_XML;
    if ((tags & TAG_XML) && Lower(Trim(text)).find("<html") != std::string::npos)
        tags |= TAG_HTML;
    if (IsCSV(text))           tags |= TAG_CSV;
    if (IsMarkdown(text))      tags |= TAG_MARKDOWN;
    if (IsSQL(text))           tags |= TAG_SQL;
    if (IsCode(text))          tags |= TAG_CODE;
    if (IsCommand(text))       tags |= TAG_COMMAND;
    if (IsColorHex(text))      tags |= TAG_HEX;
    if (IsUUID(text))          tags |= TAG_UUID;
    if (IsIPAddress(text))     tags |= TAG_IP;
    if (IsPhone(text))         tags |= TAG_PHONE;
    if (IsDateTime(text))      tags |= TAG_DATE;
    if (IsBase64Like(text))    tags |= TAG_BASE64;
    if (IsLogOrStackTrace(text)) tags |= TAG_LOG;
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
    if (t.size() < 2) return false;
    const bool object = t.front() == '{' && t.back() == '}';
    const bool array = t.front() == '[' && t.back() == ']';
    if (!object && !array) return false;
    return t.find(':') != std::string::npos || array;
}

bool ContentDetector::IsXML(const std::string& text) {
    std::string t = Trim(text);
    if (t.size() < 7 || t.front() != '<') return false;
    if (t.rfind("<?xml", 0) == 0 || t.rfind("<!DOCTYPE", 0) == 0)
        return true;
    static const std::regex tagPair(R"(<([A-Za-z][A-Za-z0-9:_\-]*)\b[^>]*>[\s\S]*</\1>)",
                                    std::regex::optimize);
    return std::regex_search(t, tagPair);
}

bool ContentDetector::IsCSV(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    int rows = 0;
    int commaRows = 0;
    while (rows < 6 && std::getline(in, line)) {
        if (Trim(line).empty()) continue;
        ++rows;
        if (std::count(line.begin(), line.end(), ',') >= 1 ||
            std::count(line.begin(), line.end(), '\t') >= 1)
            ++commaRows;
    }
    return rows >= 2 && commaRows == rows;
}

bool ContentDetector::IsMarkdown(const std::string& text) {
    std::string t = Trim(text);
    static const std::regex md(R"((^|\n)(#{1,6}\s+|\s*[-*+]\s+|\s*\d+\.\s+|>\s+)|\[[^\]]+\]\([^)]+\)|`{1,3}[^`]+`{1,3})",
                               std::regex::optimize);
    return std::regex_search(t, md);
}

bool ContentDetector::IsSQL(const std::string& text) {
    std::string t = Lower(Trim(text));
    static const std::regex sql(R"(\b(select|insert\s+into|update|delete\s+from|create\s+table|alter\s+table|drop\s+table)\b[\s\S]*\b(from|where|values|set|table)\b)",
                                std::regex::optimize);
    return std::regex_search(t, sql);
}

bool ContentDetector::IsCode(const std::string& text) {
    std::string t = Trim(text);
    if (IsJSON(t) || IsXML(t) || IsSQL(t)) return false;
    static const std::regex code(R"((#include\s*<|using\s+namespace|class\s+\w+|struct\s+\w+|function\s+\w+|def\s+\w+\s*\(|=>|public\s+static|console\.log|std::|\w+\s*=\s*function|\{[\s\S]*;\s*\}))",
                                 std::regex::optimize);
    return std::regex_search(t, code);
}

bool ContentDetector::IsCommand(const std::string& text) {
    std::string t = Trim(text);

    // Single-line fast path
    static const std::regex singleCmd(
        R"(^(git|npm|npx|pnpm|yarn|python|python3|pip|pip3|cmake|ninja|make|msbuild|dotnet|cargo|rustc|go|node|docker|kubectl|helm|terraform|az|aws|gh|curl|wget|ssh|scp|rsync|winget|choco|scoop|powershell|pwsh|cmd|copy|xcopy|robocopy|reg|netsh|ipconfig|ping|tracert|nslookup|tasklist|taskkill|sc|net|wmic|certutil|cipher|attrib|icacls|bcdedit|diskpart|format|chkdsk|sfc|dism|wusa|msiexec|runas)\b)",
        std::regex::icase | std::regex::optimize);

    if (t.find('\n') == std::string::npos)
        return std::regex_search(t, singleCmd);

    // Multi-line: check that the majority of non-empty lines look like commands
    // (covers git command sequences, shell scripts, PS1 snippets, etc.)
    std::istringstream ss(t);
    std::string line;
    int total = 0, matching = 0;
    while (std::getline(ss, line)) {
        std::string lt = Trim(line);
        if (lt.empty() || lt.front() == '#') continue;  // skip blank / comment lines
        ++total;
        if (std::regex_search(lt, singleCmd)) ++matching;
    }
    // Require at least 2 lines, 60%+ matching command pattern
    return total >= 2 && matching * 10 >= total * 6;
}

bool ContentDetector::IsUUID(const std::string& text) {
    static const std::regex uuid(R"(\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}\b)",
                                 std::regex::optimize);
    return std::regex_search(text, uuid);
}

bool ContentDetector::IsIPAddress(const std::string& text) {
    static const std::regex ip(R"(\b((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)\b)",
                               std::regex::optimize);
    return std::regex_search(text, ip);
}

bool ContentDetector::IsPhone(const std::string& text) {
    std::string t = Trim(text);
    static const std::regex phone(R"(^(\+?1[\s.\-]?)?(\(?\d{3}\)?[\s.\-]?)\d{3}[\s.\-]?\d{4}$)",
                                  std::regex::optimize);
    return std::regex_match(t, phone);
}

bool ContentDetector::IsDateTime(const std::string& text) {
    std::string t = Trim(text);
    static const std::regex date(R"(\b(\d{4}-\d{2}-\d{2}|\d{1,2}/\d{1,2}/\d{2,4})([ T]\d{1,2}:\d{2}(:\d{2})?)?\b)",
                                 std::regex::optimize);
    return std::regex_search(t, date);
}

bool ContentDetector::IsBase64Like(const std::string& text) {
    std::string t = Trim(text);
    if (t.size() < 32 || t.size() > 8192 || t.size() % 4 != 0) return false;
    static const std::regex b64(R"(^[A-Za-z0-9+/]+={0,2}$)", std::regex::optimize);
    return std::regex_match(t, b64);
}

bool ContentDetector::IsLogOrStackTrace(const std::string& text) {
    static const std::regex stack(R"((Exception|Traceback|at\s+\w[\w.$<>]*\(|^\s*File\s+\".+\",\s+line\s+\d+|ERROR|WARN|FAILED:))",
                                  std::regex::icase | std::regex::optimize);
    return std::regex_search(text, stack);
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

uint32_t ContentDetector::TagsForExtension(const std::string& extension) {
    const std::string ext = Lower(extension);
    if (ext.empty()) return TAG_NONE;

    static const std::unordered_set<std::string> image{".png",".jpg",".jpeg",".gif",".bmp",".webp",".ico",".tif",".tiff",".svg"};
    static const std::unordered_set<std::string> doc{".txt",".pdf",".doc",".docx",".rtf",".odt"};
    static const std::unordered_set<std::string> archive{".zip",".7z",".rar",".tar",".gz",".bz2",".xz"};
    static const std::unordered_set<std::string> exe{".exe",".msi",".bat",".cmd",".com"};
    static const std::unordered_set<std::string> script{".ps1",".sh",".bash",".py",".js",".ts",".rb",".pl",".lua"};
    static const std::unordered_set<std::string> code{".c",".cc",".cpp",".cxx",".h",".hpp",".cs",".java",".go",".rs",".php",".html",".css",".scss",".jsx",".tsx"};
    static const std::unordered_set<std::string> config{".ini",".yaml",".yml",".toml",".json",".xml",".config",".conf",".env"};
    static const std::unordered_set<std::string> data{".csv",".tsv",".db",".sqlite",".sql",".xls",".xlsx"};
    static const std::unordered_set<std::string> audio{".mp3",".wav",".flac",".aac",".ogg",".m4a"};
    static const std::unordered_set<std::string> video{".mp4",".mov",".mkv",".avi",".wmv",".webm"};

    uint32_t tags = TAG_NONE;
    if (image.count(ext)) tags |= TAG_IMAGE_FILE;
    if (doc.count(ext)) tags |= TAG_DOCUMENT;
    if (archive.count(ext)) tags |= TAG_ARCHIVE;
    if (exe.count(ext)) tags |= TAG_EXECUTABLE;
    if (script.count(ext)) tags |= TAG_SCRIPT | TAG_CODE;
    if (code.count(ext)) tags |= TAG_CODE;
    if (config.count(ext)) tags |= TAG_CONFIG;
    if (data.count(ext)) tags |= TAG_DATA;
    if (audio.count(ext)) tags |= TAG_AUDIO;
    if (video.count(ext)) tags |= TAG_VIDEO;
    if (ext == ".md" || ext == ".markdown") tags |= TAG_MARKDOWN | TAG_DOCUMENT;
    if (ext == ".csv" || ext == ".tsv") tags |= TAG_CSV | TAG_DATA;
    if (ext == ".html" || ext == ".htm") tags |= TAG_HTML | TAG_CODE;
    if (ext == ".json") tags |= TAG_JSON | TAG_CONFIG;
    if (ext == ".xml") tags |= TAG_XML | TAG_CONFIG;
    if (ext == ".sql") tags |= TAG_SQL | TAG_DATA;
    return tags;
}

uint32_t ContentDetector::DetectPathTags(const std::string& text) {
    uint32_t tags = TAG_NONE;
    const std::vector<std::string> lines = NonEmptyLines(text);
    if (lines.empty()) return tags;

    bool allPaths = true;
    for (const std::string& line : lines) {
        if (!IsFilePath(line)) {
            allPaths = false;
            break;
        }
    }
    if (!allPaths) return tags;

    tags |= TAG_PATH;
    for (const std::string& line : lines) {
        std::error_code ec;
        const std::filesystem::path path = std::filesystem::u8path(line);
        if (std::filesystem::is_directory(path, ec)) {
            tags |= TAG_FOLDER;
        } else {
            tags |= TAG_FILE;
            tags |= TagsForExtension(path.extension().string());
        }
    }
    return tags;
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
    case TAG_FILE:       return "File";
    case TAG_FOLDER:     return "Folder";
    case TAG_IMAGE_FILE: return "Image File";
    case TAG_DOCUMENT:   return "Document";
    case TAG_ARCHIVE:    return "Archive";
    case TAG_EXECUTABLE: return "Executable";
    case TAG_SCRIPT:     return "Script";
    case TAG_CONFIG:     return "Config";
    case TAG_DATA:       return "Data";
    case TAG_AUDIO:      return "Audio";
    case TAG_VIDEO:      return "Video";
    case TAG_MARKDOWN:   return "Markdown";
    case TAG_CSV:        return "CSV";
    case TAG_HTML:       return "HTML";
    case TAG_UUID:       return "UUID";
    case TAG_IP:         return "IP";
    case TAG_DATE:       return "Date";
    case TAG_BASE64:     return "Base64";
    case TAG_COMMAND:    return "Command";
    case TAG_LOG:        return "Log";
    default:         return "";
    }
}
