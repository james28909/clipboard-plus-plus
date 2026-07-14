#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "../app/Application.h"
#include "../app/ConfigStore.h"
#include "../app/Version.h"
#include "../support/SupportBundle.h"
#include "../util/Win32Util.h"

#include <imgui.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

using namespace MainWindowInternal;

namespace {

support::Snapshot CaptureSupportSnapshot(Application* app) {
    support::Snapshot snapshot;
    snapshot.appVersion = kClipboardPlusPlusVersion;
#ifdef NDEBUG
    snapshot.buildConfiguration = "Release";
#else
    snapshot.buildConfiguration = "Debug";
#endif
    snapshot.windowsVersion = support::WindowsVersionString();
    snapshot.dataDirectory = ConfigStore::Directory();
    const AppConfig& config = app->GetConfig();
    snapshot.incognito = app->IsIncognito();
    snapshot.captureImages = config.images.captureImages;
    snapshot.deduplicateHistory = config.deduplicateHistory;
    snapshot.startWithWindows = app->IsStartWithWindowsEnabled();
    snapshot.autoSwitchProfiles = config.autoSwitchClipboardByProcess;
    snapshot.autoCreateProfiles = config.autoCreateClipboardByProcess;
    snapshot.developerMode = config.developer.enabled;
    snapshot.developerEventLog = config.developer.eventLogEnabled;
    snapshot.activeHistoryLimit = config.activeHistoryLimit;
    snapshot.vaultUnlimited = config.vaultUnlimited;
    snapshot.vaultLimitMB = config.vaultLimitMB;
    snapshot.profileCount = app->GetClipboardProfiles().size();
    snapshot.activeHistoryCount = app->GetHistory() ? app->GetHistory()->Size() : 0;
    snapshot.vaultCount = app->GetVaultCount();
    for (const StartupMetric& metric : app->GetStartupMetrics())
        snapshot.startupMetrics.emplace_back(metric.name, metric.value);
    for (const StartupTiming& timing : app->GetStartupTimings())
        snapshot.startupTimingsMs.emplace_back(timing.name, timing.durationMs);
    snapshot.persistenceErrors = app->GetHistoryPersistenceErrors();
    snapshot.developerEvents = app->GetDeveloperEvents();
    return snapshot;
}

} // namespace

void MainWindow::DrawSupport() {
    Application* app = Application::Get();
    if (!app) return;

    PageHeader("Support & diagnostics",
               "Create privacy-reviewed diagnostics and prepare a GitHub issue.");

    static support::BundleOptions options;
    static bool consent = false;
    static support::BundleResult lastBundle;
    static std::string bundleStatus;
    static bool bundleStatusError = false;
    static bool cleanupDone = false;
    if (!cleanupDone) {
        support::CleanupOldBundles(ConfigStore::Directory() / "support",
                                   std::chrono::hours(24 * 14));
        cleanupDone = true;
    }

    if (BeginSettingsCard("##support_bundle", "Create support bundle",
                          "Review exactly what will be collected before creating a private-by-default ZIP.")) {
        ImGui::Checkbox("App, build, and Windows version", &options.environment);
        ImGui::Checkbox("Sanitized feature configuration", &options.sanitizedConfig);
        ImGui::Checkbox("Database/VFS health and file sizes", &options.databaseHealth);
        ImGui::Checkbox("Startup performance counters", &options.performance);
        ImGui::Checkbox("Sanitized recent logs", &options.sanitizedLogs);
        ImGui::Checkbox("Crash-file count (never raw dumps)", &options.crashSummary);
        if (options.sanitizedLogs)
            StatusMessage(SettingsStatus::Warning,
                "Only startup, icon-patch, and in-memory developer events are eligible. paste_debug.log is always excluded because it can contain clipboard text.");

        ImGui::Separator();
        ImGui::TextWrapped("Always excluded: clipboard contents, images, database pages, key/DPAPI files, raw config, named-slot/template/transform values, custom-action bodies and arguments, endpoints, program-launcher paths, tokens, usernames, raw crash dumps, and unnecessary paths.");
        ImGui::Checkbox("I reviewed these categories and consent to creating the ZIP", &consent);
        const bool creationDisabled = !consent;
        if (creationDisabled) ImGui::BeginDisabled();
        if (BlueButton("Create support bundle", 190.0f)) {
            try {
                lastBundle = support::CreateBundle(CaptureSupportSnapshot(app), options);
                bundleStatusError = !lastBundle.ok;
                bundleStatus = lastBundle.ok
                    ? "Support bundle created. Review manifest.json before uploading it."
                    : lastBundle.error;
            } catch (const std::exception& error) {
                lastBundle = {};
                bundleStatusError = true;
                bundleStatus = std::string("Support bundle creation failed safely: ") + error.what();
            } catch (...) {
                lastBundle = {};
                bundleStatusError = true;
                bundleStatus = "Support bundle creation failed safely with an unknown error.";
            }
            consent = false;
        }
        if (creationDisabled) ImGui::EndDisabled();

        if (!bundleStatus.empty())
            StatusMessage(bundleStatusError ? SettingsStatus::Error : SettingsStatus::Success,
                          bundleStatus.c_str());
        if (lastBundle.ok) {
            ImGui::TextWrapped("%s", lastBundle.path.string().c_str());
            if (PaddedButton("Copy bundle", 120.0f)) {
                const bool copied = win32util::SetClipboardFileDrop(
                    app->GetHwnd(), {lastBundle.path.wstring()});
                bundleStatusError = !copied;
                bundleStatus = copied
                    ? "The ZIP is on the Windows file clipboard and can be pasted into an upload control."
                    : "Could not place the ZIP on the Windows clipboard.";
            }
            ImGui::SameLine();
            if (PaddedButton("Open containing folder", 180.0f)) {
                const HINSTANCE opened = ShellExecuteW(nullptr, L"open",
                    lastBundle.path.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (reinterpret_cast<INT_PTR>(opened) <= 32) {
                    bundleStatusError = true;
                    bundleStatus = "Windows could not open the support bundle folder.";
                }
            }
        }
        StatusMessage(SettingsStatus::Muted,
                      "Support ZIPs older than 14 days are removed automatically when this page is opened or a bundle is created.");
    }
    EndSettingsCard();

    static int category = 0;
    static char title[256]{};
    static char actual[4096]{};
    static char expected[4096]{};
    static char reproduction[8192]{};
    static char username[128]{};
    static bool labelUi = false;
    static bool labelPerformance = false;
    static bool labelData = false;
    static std::string issueStatus;
    static bool issueStatusError = false;
    const char* categories[] = {"Bug report", "Feature request", "Security-sensitive report"};

    if (BeginSettingsCard("##issue_composer", "GitHub issue composer",
                          "Prepare consistent public issue text. Clipboard++ never asks for or stores a GitHub token.")) {
        ImGui::SetNextItemWidth(220.0f);
        ImGui::Combo("Category", &category, categories, IM_ARRAYSIZE(categories));
        if (category == 2)
            StatusMessage(SettingsStatus::Warning,
                "Do not enter vulnerability details or secrets here. The browser action opens GitHub's private security-advisory form without putting this text in the URL.");
        ImGui::TextUnformatted("Title");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##issue_title", title, sizeof(title));
        ImGui::TextUnformatted("Actual behavior");
        ImGui::InputTextMultiline("##issue_actual", actual, sizeof(actual), {-1.0f, 80.0f});
        ImGui::TextUnformatted("Expected behavior");
        ImGui::InputTextMultiline("##issue_expected", expected, sizeof(expected), {-1.0f, 80.0f});
        ImGui::TextUnformatted("Steps to reproduce");
        ImGui::InputTextMultiline("##issue_repro", reproduction, sizeof(reproduction), {-1.0f, 100.0f});
        ImGui::TextUnformatted("GitHub username (optional)");
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("##issue_username", username, sizeof(username));
        ImGui::TextUnformatted("Suggested public labels");
        ImGui::Checkbox("UI", &labelUi); ImGui::SameLine();
        ImGui::Checkbox("Performance", &labelPerformance); ImGui::SameLine();
        ImGui::Checkbox("Data safety", &labelData);

        auto draft = [&]() {
            support::IssueDraft value;
            value.category = categories[category];
            value.title = title;
            value.actualBehavior = actual;
            value.expectedBehavior = expected;
            value.reproductionSteps = reproduction;
            value.githubUsername = username;
            if (labelUi) value.labels.push_back("ui");
            if (labelPerformance) value.labels.push_back("performance");
            if (labelData) value.labels.push_back("data-safety");
            return value;
        };
        const bool ready = title[0] && actual[0] && expected[0] && reproduction[0];
        if (!ready) ImGui::BeginDisabled();
        if (BlueButton("Copy issue text", 150.0f)) {
            const std::string text = support::BuildIssueMarkdown(draft(), CaptureSupportSnapshot(app));
            issueStatusError = !app->CopyTextToClipboard(text);
            issueStatus = issueStatusError ? "Could not copy the issue text."
                                           : "Issue text copied to the clipboard.";
        }
        ImGui::SameLine();
        if (PaddedButton(category == 2 ? "Open private security report" : "Open GitHub issue",
                         category == 2 ? 220.0f : 160.0f)) {
            const std::string url = category == 2
                ? "https://github.com/james28909/clipboard-plus-plus/security/advisories/new"
                : support::BuildGitHubIssueUrl(draft(), CaptureSupportSnapshot(app));
            const HINSTANCE opened = ShellExecuteA(nullptr, "open", url.c_str(), nullptr,
                                                   nullptr, SW_SHOWNORMAL);
            issueStatusError = reinterpret_cast<INT_PTR>(opened) <= 32;
            issueStatus = issueStatusError
                ? "GitHub could not be opened. Copy the issue text and submit it when online."
                : (category == 2
                    ? "GitHub's private security-reporting page opened without issue text in the URL."
                    : "GitHub opened with safe prefilled text. Attach the support ZIP manually.");
        }
        if (!ready) ImGui::EndDisabled();
        if (!ready)
            StatusMessage(SettingsStatus::Muted,
                          "Title, actual behavior, expected behavior, and reproduction steps are required.");
        if (!issueStatus.empty())
            StatusMessage(issueStatusError ? SettingsStatus::Error : SettingsStatus::Success,
                          issueStatus.c_str());
        StatusMessage(SettingsStatus::Muted,
                      "The ZIP is never uploaded automatically and its contents are never placed in a URL.");
    }
    EndSettingsCard();
}
