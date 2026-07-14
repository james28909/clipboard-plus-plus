#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace support {

struct BundleOptions {
    bool environment{true};
    bool sanitizedConfig{true};
    bool databaseHealth{true};
    bool performance{true};
    bool sanitizedLogs{false};
    bool crashSummary{true};
};

struct Snapshot {
    std::string appVersion;
    std::string buildConfiguration;
    std::string windowsVersion;
    std::filesystem::path dataDirectory;
    bool incognito{};
    bool captureImages{};
    bool deduplicateHistory{};
    bool startWithWindows{};
    bool autoSwitchProfiles{};
    bool autoCreateProfiles{};
    bool developerMode{};
    bool developerEventLog{};
    int activeHistoryLimit{};
    int vaultLimitMB{};
    bool vaultUnlimited{};
    size_t profileCount{};
    size_t activeHistoryCount{};
    size_t vaultCount{};
    std::vector<std::pair<std::string, std::string>> startupMetrics;
    std::vector<std::pair<std::string, double>> startupTimingsMs;
    std::vector<std::string> persistenceErrors;
    std::vector<std::string> developerEvents;
};

struct BundleResult {
    bool ok{};
    std::filesystem::path path;
    std::string error;
    std::vector<std::string> files;
};

struct IssueDraft {
    std::string category{"Bug report"};
    std::string title;
    std::string actualBehavior;
    std::string expectedBehavior;
    std::string reproductionSteps;
    std::string githubUsername;
    std::vector<std::string> labels;
};

std::string WindowsVersionString();
std::string SanitizeDiagnosticText(std::string text,
                                   const std::string& windowsUsername = {});
BundleResult CreateBundle(const Snapshot& snapshot, const BundleOptions& options);
size_t CleanupOldBundles(const std::filesystem::path& directory,
                         std::chrono::hours maxAge,
                         std::chrono::system_clock::time_point now =
                             std::chrono::system_clock::now());
std::string BuildIssueMarkdown(const IssueDraft& draft, const Snapshot& snapshot);
std::string BuildGitHubIssueUrl(const IssueDraft& draft, const Snapshot& snapshot,
                               size_t maximumLength = 7600);

} // namespace support
