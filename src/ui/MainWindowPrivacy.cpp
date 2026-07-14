#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "ImGuiWidgets.h"
#include "../app/Application.h"
#include "../app/ConfigStore.h"
#include "../app/TrayIcon.h"
#include "../clipboard/ImageStore.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ContentDetector.h"
#include "../filters/CustomFilter.h"
#include "Appearance.h"
#include "PopupWindow.h"
#include <imgui.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>


using namespace MainWindowInternal;

// -- Section: Privacy ---------------------------------------------------------

void MainWindow::DrawPrivacy() {
    Application* app = Application::Get();
    if (!app) return;

    PageHeader("Privacy", "Control sensitive capture behavior and review local encryption status.");

    static bool detectSecrets = true;
    static bool autoDiscard   = false;
    static bool clearOnLock   = false;
    static char exclusionBuf[512] = "KeePass.exe\n1Password.exe\nBitwarden.exe";

    if (BeginSettingsCard("##privacy_capture", "Private capture",
                          "Pause capture immediately or review planned automatic protections.")) {
        bool incognito = app->IsIncognito();
        if (ImGui::Checkbox("Incognito mode (pause capture)", &incognito))
            app->SetIncognito(incognito);
        StatusMessage(incognito ? SettingsStatus::Warning : SettingsStatus::Muted,
            incognito ? "Clipboard monitoring is paused until Incognito mode is turned off."
                      : "Clipboard monitoring is active.");

        ImGui::BeginDisabled();
        ImGui::Checkbox("Detect secret patterns (API keys, tokens, PEM, JWTs)", &detectSecrets);
        if (detectSecrets) {
            ImGui::Indent();
            ImGui::Checkbox("Auto-discard detected secrets", &autoDiscard);
            ImGui::Unindent();
        }
        ImGui::Checkbox("Clear history when Windows locks", &clearOnLock);
        ImGui::EndDisabled();
        StatusMessage(SettingsStatus::Muted,
                      "Automatic secret detection and lock clearing are visible design placeholders and are not active yet.");
    }
    EndSettingsCard();

    if (BeginSettingsCard("##privacy_exclusions", "Process exclusions",
                          "Applications listed here will eventually be excluded from capture.")) {
        ImGui::BeginDisabled();
        ImGui::InputTextMultiline("##excl", exclusionBuf, sizeof(exclusionBuf), {-1, 100});
        ImGui::EndDisabled();
        StatusMessage(SettingsStatus::Muted, "Process exclusions are not active yet.");
    }
    EndSettingsCard();

    if (BeginSettingsCard("##privacy_encryption", "Local encryption",
                          "Clipboard history, overflow vault data, and image storage are encrypted at rest.")) {
        StatusMessage(SettingsStatus::Success, "Encrypted SQLite VFS: active");
        ImGui::TextWrapped("Database keys are protected by Windows DPAPI for the current Windows user. Clipboard contents and encryption keys are never shown on this page.");
        ImGui::TextDisabled("Storage folder: %s", ConfigStore::Directory().string().c_str());
    }
    EndSettingsCard();
}
