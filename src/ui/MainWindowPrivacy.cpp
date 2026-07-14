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
#include "../security/BackupRestore.h"
#include "Appearance.h"
#include "PopupWindow.h"
#include <imgui.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <future>
#include <iomanip>
#include <sstream>
#include <string>


using namespace MainWindowInternal;

namespace {

bool PickFolder(HWND owner, const wchar_t* title, std::filesystem::path& path) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BROWSEINFOW browse{};
    browse.hwndOwner = owner;
    browse.lpszTitle = title;
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&browse);
    wchar_t buffer[32768]{};
    const bool ok = selected && SHGetPathFromIDListW(selected, buffer);
    if (selected) CoTaskMemFree(selected);
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (ok) path = buffer;
    return ok;
}

bool PickStatePackage(HWND owner, bool save, bool encrypted,
                      std::filesystem::path& path) {
    wchar_t buffer[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.lpstrFilter = L"Clipboard++ state packages (*.cppstate;*.json)\0*.cppstate;*.json\0All files (*.*)\0*.*\0";
    dialog.lpstrDefExt = encrypted ? L"cppstate" : L"json";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST |
        (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const bool ok = save ? GetSaveFileNameW(&dialog) != FALSE
                         : GetOpenFileNameW(&dialog) != FALSE;
    if (ok) path = buffer;
    return ok;
}

} // namespace

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

    static std::string backupStatus;
    static bool backupStatusError = false;
    static std::filesystem::path lastBackupPath;
    static const std::string previousRestore = app->LastEncryptedRestoreStatus();
    enum class StorageTask { None, Backup, Restore };
    static StorageTask storageTask = StorageTask::None;
    static std::future<backup_restore::Result> storageFuture;
    if (storageFuture.valid() &&
        storageFuture.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready) {
        const backup_restore::Result result = storageFuture.get();
        backupStatus = result.message;
        backupStatusError = !result.ok;
        if (storageTask == StorageTask::Backup && result.ok)
            lastBackupPath = result.path;
        storageTask = StorageTask::None;
    }
    if (BeginSettingsCard("##privacy_backup_restore", "Encrypted backup & restore",
                          "Back up or restore Clipboard++ history, images, automation data, and settings together.")) {
        ImGui::TextWrapped("Backups use SQLite's online backup API, remain AES-256-XTS encrypted, and receive fresh Windows DPAPI-protected keys. The configuration is separately DPAPI-protected inside the same backup folder. Clipboard++ never copies a live database/WAL pair directly.");
        StatusMessage(SettingsStatus::Warning,
            "Encrypted backups are normally usable only by the same Windows user on this computer. Decrypted vault exports are plaintext, are not restore backups, and must be protected separately.");

        const bool storageBusy = storageTask != StorageTask::None;
        if (storageBusy) ImGui::BeginDisabled();
        if (BlueButton("Create encrypted backup", 205.0f)) {
            std::filesystem::path parent;
            if (PickFolder(app->GetHwnd(), L"Choose the folder that will contain a new Clipboard++ backup", parent)) {
                storageTask = StorageTask::Backup;
                storageFuture = std::async(std::launch::async, [parent]() {
                    return backup_restore::CreateEncryptedBackup(
                        ConfigStore::Directory(), parent);
                });
            }
        }
        if (storageBusy) ImGui::EndDisabled();
        if (!lastBackupPath.empty()) {
            ImGui::SameLine();
            if (PaddedButton("Open backup folder", 165.0f))
                ShellExecuteW(nullptr, L"open", lastBackupPath.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
        }

        ImGui::Separator();
        if (!app->HasPendingEncryptedRestore()) {
            if (storageBusy) ImGui::BeginDisabled();
            if (DangerButton("Choose backup to restore", 205.0f)) {
                std::filesystem::path source;
                if (PickFolder(app->GetHwnd(), L"Choose a Clipboard++ encrypted backup folder", source)) {
                    const int answer = MessageBoxW(app->GetHwnd(),
                        L"Clipboard++ will validate and re-encrypt this backup now, then replace the matching live databases on restart.\n\nThe current encrypted databases will be retained in a rollback folder. Continue?",
                        L"Stage encrypted restore",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                    if (answer == IDYES) {
                        storageTask = StorageTask::Restore;
                        storageFuture = std::async(std::launch::async, [source]() {
                            return backup_restore::StageEncryptedRestore(
                                ConfigStore::Directory(), source);
                        });
                    }
                }
            }
            if (storageBusy) ImGui::EndDisabled();
        } else {
            StatusMessage(SettingsStatus::Warning,
                          "A verified restore is pending. Current databases have not been changed yet.");
            if (DangerButton("Restart and restore now", 205.0f)) {
                if (!app->RestartForEncryptedRestore()) {
                    backupStatus = "Could not launch the restore restart process.";
                    backupStatusError = true;
                }
            }
            ImGui::SameLine();
            if (PaddedButton("Cancel pending restore", 185.0f))
                backupStatusError = !app->CancelPendingEncryptedRestore(backupStatus);
        }

        if (storageBusy) {
            StatusMessage(SettingsStatus::Muted,
                storageTask == StorageTask::Backup
                    ? "Creating and verifying encrypted backup..."
                    : "Validating and re-encrypting restore snapshot...");
        }

        if (!backupStatus.empty())
            StatusMessage(backupStatusError ? SettingsStatus::Error
                                            : SettingsStatus::Success,
                          backupStatus.c_str());
        if (!previousRestore.empty()) {
            ImGui::TextDisabled("Last restore: %s", previousRestore.c_str());
        }
    }
    EndSettingsCard();

    static bool encryptStatePackage = true;
    static int conflictPolicy = 0;
    static std::string statePackageStatus;
    static bool statePackageError = false;
    static bool stateRestartRequired = false;
    if (BeginSettingsCard("##privacy_state_package", "Settings & definition transfer",
                          "Export or import portable settings, profiles, named slots, transforms, templates, and workflow actions.")) {
        ImGui::Checkbox("Encrypt export for this Windows user", &encryptStatePackage);
        if (encryptStatePackage) {
            StatusMessage(SettingsStatus::Muted,
                "DPAPI-encrypted packages normally open only for this Windows user on this computer.");
        } else {
            StatusMessage(SettingsStatus::Warning,
                "Plaintext packages can contain clipboard slot text, templates, executable arguments, and other secrets.");
        }
        if (BlueButton("Export state package", 185.0f)) {
            std::filesystem::path destination;
            if (PickStatePackage(app->GetHwnd(), true, encryptStatePackage,
                                 destination)) {
                statePackageError = !app->ExportStatePackage(
                    destination, encryptStatePackage, statePackageStatus);
            }
        }

        ImGui::Separator();
        const char* policies[] = {"Skip existing", "Replace existing", "Keep both"};
        ImGui::SetNextItemWidth(190.0f);
        ImGui::Combo("Name conflicts", &conflictPolicy, policies,
                     static_cast<int>(std::size(policies)));
        ImGui::TextDisabled("Configuration is a single item: Skip/Keep both retains local settings; Replace stages imported settings for restart.");
        if (DangerButton("Import state package", 185.0f)) {
            std::filesystem::path source;
            if (PickStatePackage(app->GetHwnd(), false, true, source)) {
                const int answer = MessageBoxW(app->GetHwnd(),
                    L"Import profiles and reusable definitions from this package?\n\nPlaintext packages may contain secrets. Replace mode also stages the package's app settings for the next restart.",
                    L"Import Clipboard++ state", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                if (answer == IDYES) {
                    state_package::ConflictPolicy policy =
                        static_cast<state_package::ConflictPolicy>(conflictPolicy);
                    bool restart = false;
                    statePackageError = !app->ImportStatePackage(
                        source, policy, restart, statePackageStatus);
                    stateRestartRequired = stateRestartRequired || restart;
                }
            }
        }
        if (stateRestartRequired) {
            ImGui::SameLine();
            if (PaddedButton("Restart and apply settings", 205.0f))
                statePackageError = !app->RestartForEncryptedRestore();
        }
        if (!statePackageStatus.empty())
            StatusMessage(statePackageError ? SettingsStatus::Error
                                             : SettingsStatus::Success,
                          statePackageStatus.c_str());
    }
    EndSettingsCard();
}
