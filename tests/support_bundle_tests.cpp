#include "support/SupportBundle.h"
#include "util/Win32Util.h"

#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void Write(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value;
}

} // namespace

int main() {
    const std::string sensitive =
        "C:\\Users\\James\\secret\\file.txt app=chatgpt.exe "
        "id=123e4567-e89b-12d3-a456-426614174000 host=192.168.1.22 text=my password";
    const std::string sanitized = support::SanitizeDiagnosticText(sensitive, "James");
    Require(sanitized.find("James") == std::string::npos, "username must be redacted");
    Require(sanitized.find("chatgpt.exe") == std::string::npos, "process must be redacted");
    Require(sanitized.find("123e4567") == std::string::npos, "identifier must be redacted");
    Require(sanitized.find("192.168.1.22") == std::string::npos, "address must be redacted");
    Require(sanitized.find("my password") == std::string::npos, "clipboard preview must be redacted");
    Require(sanitized.find("secret\\file.txt") == std::string::npos,
            "unnecessary file paths must be redacted");

    const auto payload = win32util::BuildFileDropPayload(
        {L"C:\\Temp\\support.zip", L"D:\\Other\\report.zip"});
    Require(payload.size() > sizeof(DROPFILES), "file-drop payload must contain paths");
    const auto* drop = reinterpret_cast<const DROPFILES*>(payload.data());
    Require(drop->pFiles == sizeof(DROPFILES) && drop->fWide,
            "file-drop payload must be a Unicode DROPFILES block");
    const wchar_t* paths = reinterpret_cast<const wchar_t*>(payload.data() + drop->pFiles);
    Require(std::wstring(paths) == L"C:\\Temp\\support.zip", "first path must round-trip");
    paths += std::wcslen(paths) + 1;
    Require(std::wstring(paths) == L"D:\\Other\\report.zip", "second path must round-trip");

    support::Snapshot snapshot;
    snapshot.appVersion = "0.1.0-beta.7";
    snapshot.buildConfiguration = "Debug";
    snapshot.windowsVersion = "Windows test build";
    snapshot.profileCount = 3;
    snapshot.startupMetrics.emplace_back("profile count", "3");
    snapshot.startupTimingsMs.emplace_back("first frame", 12.5);

    support::IssueDraft issue;
    issue.category = "Bug report";
    issue.title = "Popup freezes";
    issue.actualBehavior = "The popup freezes.";
    issue.expectedBehavior = "The popup remains responsive.";
    issue.reproductionSteps = "1. Open popup\n2. Paste";
    issue.githubUsername = "tester";
    issue.labels = {"ui", "performance"};
    const std::string markdown = support::BuildIssueMarkdown(issue, snapshot);
    Require(markdown.find("Popup freezes") == std::string::npos,
            "title is supplied separately and should not be duplicated in Markdown");
    Require(markdown.find("0.1.0-beta.7") != std::string::npos,
            "issue Markdown must contain the version");
    Require(markdown == support::BuildIssueMarkdown(issue, snapshot),
            "issue Markdown must be deterministic");
    const std::string url = support::BuildGitHubIssueUrl(issue, snapshot);
    Require(url.find("title=Popup%20freezes") != std::string::npos,
            "issue URL must encode its title");
    issue.actualBehavior.assign(20000, 'x');
    Require(support::BuildGitHubIssueUrl(issue, snapshot, 900).size() <= 900,
            "oversized issue URLs must use a safe fallback");

    const auto root = std::filesystem::temp_directory_path() /
        ("clipboardpp-support-tests-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    snapshot.dataDirectory = root;
    Write(root / "startup_profile.log", sensitive);
    Write(root / "paste_debug.log", "text=TOP SECRET CLIPBOARD CONTENT");
    Write(root / "clipboard.db", "RAW DATABASE SECRET");
    Write(root / "clipboard.db.key", "RAW KEY SECRET");
    snapshot.developerEvents = {sensitive};
    support::BundleOptions options;
    options.sanitizedLogs = true;
    const support::BundleResult bundle = support::CreateBundle(snapshot, options);
    Require(bundle.ok && std::filesystem::exists(bundle.path), "support ZIP must be created");
    const std::string zip = ReadAll(bundle.path);
    Require(zip.find("manifest.json") != std::string::npos, "ZIP must contain a manifest");
    Require(zip.find("paste_debug.log") != std::string::npos,
            "manifest must disclose paste-debug exclusion");
    Require(zip.find("custom-action bodies") != std::string::npos,
            "manifest must disclose custom-action secret-field exclusion");
    Require(zip.find("TOP SECRET CLIPBOARD CONTENT") == std::string::npos,
            "paste-debug contents must never enter the ZIP");
    Require(zip.find("RAW DATABASE SECRET") == std::string::npos,
            "database pages must never enter the ZIP");
    Require(zip.find("RAW KEY SECRET") == std::string::npos,
            "key blobs must never enter the ZIP");
    Require(zip.find("my password") == std::string::npos,
            "sanitized logs must not retain clipboard previews");

    const auto oldBundle = root / "support" / "old.zip";
    Write(oldBundle, "old");
    std::filesystem::last_write_time(oldBundle,
        std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * 30), ec);
    Require(support::CleanupOldBundles(root / "support", std::chrono::hours(24 * 14)) >= 1,
            "expired support ZIPs must be removed");

    const auto lockedLog = root / "iconpatch.log";
    Write(lockedLog, sensitive);
    HANDLE lock = CreateFileW(lockedLog.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE databaseLock = CreateFileW((root / "clipboard.db").c_str(), GENERIC_READ, 0,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const support::BundleResult lockedBundle = support::CreateBundle(snapshot, options);
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    if (databaseLock != INVALID_HANDLE_VALUE) CloseHandle(databaseLock);
    Require(lockedBundle.ok, "an unavailable log must not prevent bundle creation");

    std::filesystem::remove_all(root, ec);
    std::cout << "support bundle tests passed\n";
    return 0;
}
