#include "SupportBundle.h"

#include <windows.h>
#include <winternl.h>
#include <json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <system_error>

namespace support {
namespace {

using json = nlohmann::json;

struct ZipEntry {
    std::string name;
    std::string contents;
    uint32_t crc{};
    uint32_t offset{};
};

void Write16(std::ostream& out, uint16_t value) {
    const char bytes[] = {static_cast<char>(value & 0xff),
                          static_cast<char>((value >> 8) & 0xff)};
    out.write(bytes, sizeof(bytes));
}

void Write32(std::ostream& out, uint32_t value) {
    const char bytes[] = {static_cast<char>(value & 0xff),
                          static_cast<char>((value >> 8) & 0xff),
                          static_cast<char>((value >> 16) & 0xff),
                          static_cast<char>((value >> 24) & 0xff)};
    out.write(bytes, sizeof(bytes));
}

uint32_t Crc32(const std::string& value) {
    uint32_t crc = 0xffffffffu;
    for (unsigned char byte : value) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool WriteZip(const std::filesystem::path& path, std::vector<ZipEntry>& entries,
              std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Could not create the support ZIP.";
        return false;
    }
    for (ZipEntry& entry : entries) {
        entry.crc = Crc32(entry.contents);
        entry.offset = static_cast<uint32_t>(out.tellp());
        Write32(out, 0x04034b50u);
        Write16(out, 20); Write16(out, 0x0800); Write16(out, 0);
        Write16(out, 0); Write16(out, 0);
        Write32(out, entry.crc);
        Write32(out, static_cast<uint32_t>(entry.contents.size()));
        Write32(out, static_cast<uint32_t>(entry.contents.size()));
        Write16(out, static_cast<uint16_t>(entry.name.size()));
        Write16(out, 0);
        out.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        out.write(entry.contents.data(), static_cast<std::streamsize>(entry.contents.size()));
    }
    const uint32_t centralOffset = static_cast<uint32_t>(out.tellp());
    for (const ZipEntry& entry : entries) {
        Write32(out, 0x02014b50u);
        Write16(out, 20); Write16(out, 20); Write16(out, 0x0800); Write16(out, 0);
        Write16(out, 0); Write16(out, 0);
        Write32(out, entry.crc);
        Write32(out, static_cast<uint32_t>(entry.contents.size()));
        Write32(out, static_cast<uint32_t>(entry.contents.size()));
        Write16(out, static_cast<uint16_t>(entry.name.size()));
        Write16(out, 0); Write16(out, 0); Write16(out, 0); Write16(out, 0);
        Write32(out, 0); Write32(out, entry.offset);
        out.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    }
    const uint32_t centralSize = static_cast<uint32_t>(out.tellp()) - centralOffset;
    Write32(out, 0x06054b50u);
    Write16(out, 0); Write16(out, 0);
    Write16(out, static_cast<uint16_t>(entries.size()));
    Write16(out, static_cast<uint16_t>(entries.size()));
    Write32(out, centralSize); Write32(out, centralOffset); Write16(out, 0);
    if (!out.good()) {
        error = "The support ZIP could not be written completely.";
        return false;
    }
    return true;
}

std::string ReadTail(const std::filesystem::path& path, size_t maximumBytes,
                     std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "unavailable";
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    const std::streamoff start = std::max<std::streamoff>(0, size -
        static_cast<std::streamoff>(maximumBytes));
    input.seekg(start, std::ios::beg);
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

std::string Username() {
    std::array<char, 256> value{};
    DWORD size = static_cast<DWORD>(value.size());
    return GetUserNameA(value.data(), &size) ? std::string(value.data()) : std::string{};
}

std::string Timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d-%H%M%S");
    return out.str();
}

std::filesystem::path UniqueBundlePath(const std::filesystem::path& directory) {
    const std::string stem = "clipboardpp-support-" + Timestamp();
    for (int suffix = 0; suffix < 1000; ++suffix) {
        const std::string name = stem + (suffix == 0 ? "" : "-" + std::to_string(suffix)) + ".zip";
        const std::filesystem::path candidate = directory / name;
        if (!std::filesystem::exists(candidate))
            return candidate;
    }
    return directory / (stem + "-overflow.zip");
}

std::string UrlEncode(const std::string& value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << static_cast<char>(c);
        else
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return out.str();
}

void AddEntry(std::vector<ZipEntry>& entries, std::string name, std::string contents) {
    entries.push_back({std::move(name), std::move(contents)});
}

} // namespace

std::string WindowsVersionString() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
        ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : nullptr);
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (!rtlGetVersion || rtlGetVersion(&info) != 0)
        return "Windows (version unavailable)";
    std::ostringstream out;
    out << "Windows " << info.dwMajorVersion << '.' << info.dwMinorVersion
        << " build " << info.dwBuildNumber;
    return out.str();
}

std::string SanitizeDiagnosticText(std::string text, const std::string& windowsUsername) {
    text = std::regex_replace(text,
        std::regex(R"(([A-Za-z]:\\Users\\)[^\\\r\n]+)", std::regex::icase),
        "$1[user]");
    text = std::regex_replace(text,
        std::regex(R"([A-Za-z]:\\[^\r\n\t\"]+)"), "[path]");
    text = std::regex_replace(text,
        std::regex(R"(\b[A-Za-z0-9_.-]+\.exe\b)", std::regex::icase),
        "[process].exe");
    text = std::regex_replace(text,
        std::regex(R"(\b[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}\b)",
                   std::regex::icase), "[identifier]");
    text = std::regex_replace(text,
        std::regex(R"(\b(?:\d{1,3}\.){3}\d{1,3}\b)"), "[network-address]");
    text = std::regex_replace(text,
        std::regex(R"(((?:text|content|clipboard)=)[^\r\n]*)", std::regex::icase),
        "$1[redacted]");
    if (!windowsUsername.empty()) {
        std::string escaped;
        for (char c : windowsUsername) {
            if (std::strchr(R"(\.^$|()[]{}*+?)", c)) escaped += '\\';
            escaped += c;
        }
        text = std::regex_replace(text, std::regex(escaped, std::regex::icase), "[user]");
    }
    return text;
}

BundleResult CreateBundle(const Snapshot& snapshot, const BundleOptions& options) {
    BundleResult result;
    const std::filesystem::path directory = snapshot.dataDirectory / "support";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        result.error = "Could not create the support bundle folder.";
        return result;
    }
    CleanupOldBundles(directory, std::chrono::hours(24 * 14));

    std::vector<ZipEntry> entries;
    const std::string username = Username();
    if (options.environment) {
        json value{{"app_version", snapshot.appVersion},
                   {"build_configuration", snapshot.buildConfiguration},
                   {"windows", snapshot.windowsVersion},
                   {"architecture", sizeof(void*) == 8 ? "x64" : "x86"}};
        AddEntry(entries, "environment.json", value.dump(2));
    }
    if (options.sanitizedConfig) {
        json value{{"incognito", snapshot.incognito},
                   {"capture_images", snapshot.captureImages},
                   {"deduplicate_history", snapshot.deduplicateHistory},
                   {"start_with_windows", snapshot.startWithWindows},
                   {"auto_switch_profiles", snapshot.autoSwitchProfiles},
                   {"auto_create_profiles", snapshot.autoCreateProfiles},
                   {"developer_mode", snapshot.developerMode},
                   {"developer_event_log", snapshot.developerEventLog},
                   {"active_history_limit", snapshot.activeHistoryLimit},
                   {"vault_unlimited", snapshot.vaultUnlimited},
                   {"vault_limit_mb", snapshot.vaultLimitMB},
                   {"profile_count", snapshot.profileCount}};
        AddEntry(entries, "sanitized_config.json", value.dump(2));
    }
    if (options.databaseHealth) {
        json files = json::array();
        for (const char* name : {"clipboard.db", "clipboard.db-wal", "clipboard.db-shm",
                                 "images.db", "images.db-wal", "images.db-shm"}) {
            const std::filesystem::path path = snapshot.dataDirectory / name;
            std::error_code sizeError;
            const bool exists = std::filesystem::exists(path, sizeError);
            uintmax_t bytes = exists ? std::filesystem::file_size(path, sizeError) : 0;
            files.push_back({{"name", name}, {"exists", exists},
                             {"bytes", sizeError ? 0 : bytes}});
        }
        json value{{"vfs", "clipboardpp-encrypted"},
                   {"page_encryption", "AES-256-XTS"},
                   {"key_protection", "Windows DPAPI (current user)"},
                   {"files", files},
                   {"clipboard_schema_version", 5},
                   {"image_schema_version", 1},
                   {"clipboard_key_present", std::filesystem::exists(snapshot.dataDirectory / "clipboard.db.key")},
                   {"image_key_present", std::filesystem::exists(snapshot.dataDirectory / "images.db.key")},
                   {"persistence_error_count", snapshot.persistenceErrors.size()},
                   {"persistence_errors", snapshot.persistenceErrors}};
        AddEntry(entries, "database_health.json",
                 SanitizeDiagnosticText(value.dump(2), username));
    }
    if (options.performance) {
        json timings = json::array();
        for (const auto& [name, ms] : snapshot.startupTimingsMs)
            timings.push_back({{"stage", name}, {"duration_ms", ms}});
        json metrics = json::array();
        for (const auto& [name, value] : snapshot.startupMetrics)
            metrics.push_back({{"name", name}, {"value", value}});
        json value{{"startup_timings", timings}, {"startup_metrics", metrics},
                   {"active_history_items", snapshot.activeHistoryCount},
                   {"vault_items", snapshot.vaultCount}};
        AddEntry(entries, "performance.json",
                 SanitizeDiagnosticText(value.dump(2), username));
    }
    std::vector<std::string> skippedLogs;
    if (options.sanitizedLogs) {
        for (const char* name : {"startup_profile.log", "iconpatch.log"}) {
            const std::filesystem::path path = snapshot.dataDirectory / name;
            if (!std::filesystem::exists(path)) continue;
            std::string readError;
            std::string contents = ReadTail(path, 256 * 1024, readError);
            if (readError.empty())
                AddEntry(entries, std::string("logs/") + name,
                         SanitizeDiagnosticText(std::move(contents), username));
            else
                skippedLogs.push_back(name);
        }
        if (!snapshot.developerEvents.empty()) {
            std::ostringstream events;
            for (const std::string& event : snapshot.developerEvents)
                events << event << '\n';
            AddEntry(entries, "logs/developer_events.log",
                     SanitizeDiagnosticText(events.str(), username));
        }
    }
    if (options.crashSummary) {
        size_t crashCount = 0;
        for (std::filesystem::directory_iterator it(snapshot.dataDirectory, ec), end;
             !ec && it != end; it.increment(ec)) {
            const std::string extension = it->path().extension().string();
            if (extension == ".dmp" || extension == ".crash") ++crashCount;
        }
        json value{{"detected_crash_file_count", crashCount},
                   {"raw_crash_files_included", false}};
        AddEntry(entries, "crash_summary.json", value.dump(2));
    }

    json included = json::array();
    for (const ZipEntry& entry : entries)
        included.push_back(entry.name);
    included.push_back("manifest.json");
    json manifest{
        {"format_version", 1},
        {"privacy", "Clipboard content and secret-bearing storage are excluded by design."},
        {"included_files", included},
        {"skipped_unavailable_logs", skippedLogs},
        {"always_excluded", json::array({
            "clipboard and vault contents", "images and thumbnails", "database pages",
            "database key sidecars and DPAPI blobs", "raw config.json", "paste_debug.log",
            "named-slot values", "template bodies", "transform patterns/replacements",
            "custom-action bodies, arguments, templates, paths, and sensitive values",
            "Android endpoints", "external-editor and program-launcher paths",
            "tokens and credentials",
            "raw crash dumps", "usernames and unnecessary file paths"})}
    };
    AddEntry(entries, "manifest.json", manifest.dump(2));

    result.path = UniqueBundlePath(directory);
    const std::filesystem::path temporary = result.path.string() + ".tmp";
    if (!WriteZip(temporary, entries, result.error)) {
        std::filesystem::remove(temporary, ec);
        result.path.clear();
        return result;
    }
    std::filesystem::rename(temporary, result.path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        result.error = "Could not finalize the support ZIP.";
        result.path.clear();
        return result;
    }
    result.ok = true;
    for (const ZipEntry& entry : entries) result.files.push_back(entry.name);
    return result;
}

size_t CleanupOldBundles(const std::filesystem::path& directory,
                         std::chrono::hours maxAge,
                         std::chrono::system_clock::time_point now) {
    std::error_code ec;
    size_t removed = 0;
    if (!std::filesystem::exists(directory, ec)) return 0;
    for (std::filesystem::directory_iterator it(directory, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".zip") continue;
        const auto fileTime = it->last_write_time(ec);
        if (ec) { ec.clear(); continue; }
        const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            fileTime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
        if (now - systemTime > maxAge && std::filesystem::remove(it->path(), ec)) ++removed;
        ec.clear();
    }
    return removed;
}

std::string BuildIssueMarkdown(const IssueDraft& draft, const Snapshot& snapshot) {
    std::ostringstream out;
    out << "## Category\n" << draft.category << "\n\n"
        << "## Actual behavior\n" << draft.actualBehavior << "\n\n"
        << "## Expected behavior\n" << draft.expectedBehavior << "\n\n"
        << "## Steps to reproduce\n" << draft.reproductionSteps << "\n\n"
        << "## Environment\n"
        << "- Clipboard++: " << snapshot.appVersion << " (" << snapshot.buildConfiguration << ")\n"
        << "- Windows: " << snapshot.windowsVersion << "\n";
    if (!draft.githubUsername.empty()) out << "- Reporter: @" << draft.githubUsername << "\n";
    if (!draft.labels.empty()) {
        out << "- Suggested labels: ";
        for (size_t i = 0; i < draft.labels.size(); ++i)
            out << (i ? ", " : "") << draft.labels[i];
        out << "\n";
    }
    out << "\n## Support bundle\n"
        << "Attach the ZIP created in Settings > Support & diagnostics. "
           "Review its manifest before uploading.\n";
    return out.str();
}

std::string BuildGitHubIssueUrl(const IssueDraft& draft, const Snapshot& snapshot,
                               size_t maximumLength) {
    const std::string base = "https://github.com/james28909/clipboard-plus-plus/issues/new";
    std::string url = base + "?title=" + UrlEncode(draft.title) +
        "&body=" + UrlEncode(BuildIssueMarkdown(draft, snapshot));
    if (!draft.labels.empty()) {
        std::string labels;
        for (size_t i = 0; i < draft.labels.size(); ++i)
            labels += (i ? "," : "") + draft.labels[i];
        url += "&labels=" + UrlEncode(labels);
    }
    if (url.size() <= maximumLength) return url;
    const std::string shortBody = "Issue details were too long to safely place in the URL. "
        "Use Copy issue text in Clipboard++ and paste it here.\n\nVersion: " +
        snapshot.appVersion + "\nWindows: " + snapshot.windowsVersion;
    return base + "?title=" + UrlEncode(draft.title) + "&body=" + UrlEncode(shortBody);
}

} // namespace support
