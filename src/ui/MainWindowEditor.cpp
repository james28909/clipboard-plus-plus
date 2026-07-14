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

// -- Section: Editor ----------------------------------------------------------

void MainWindow::DrawEditor() {
    Application* app = Application::Get();
    if (!app) return;

    EditorSettings s = app->GetEditorSettings();
    bool changed = false;

    if (BeginSettingsCard("##editor_configuration", "Text & script editor",
                          "Choose the editor provider, clipboard handoff, and editing behavior.")) {

    changed |= ImGui::Checkbox("Enable editor hotkey and menu actions", &s.enabled);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    HelpTooltip("When enabled, Ctrl+Shift+E opens the selected editor provider.");

    if (!s.enabled)
        ImGui::BeginDisabled();

    if (BlueButton("Open editor", 120.0f))
        app->ShowEditorPopup();
    ImGui::SameLine();
    ImGui::TextDisabled("Default hotkey: Ctrl+Shift+E");

    SectionHeader("Provider");
    const char* providers[] = {"Built-in popup", "External executable"};
    ImGui::SetNextItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::Combo("Editor provider##editorProvider", &s.provider, providers, IM_ARRAYSIZE(providers))) {
        s.provider = std::clamp(s.provider, 0, static_cast<int>(IM_ARRAYSIZE(providers)) - 1);
        changed = true;
    }

    const char* modes[] = {"Plain text", "PowerShell", "Batch", "JSON", "Markdown"};
    ImGui::SetNextItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::Combo("Default mode##editorMode", &s.mode, modes, IM_ARRAYSIZE(modes))) {
        s.mode = std::clamp(s.mode, 0, static_cast<int>(IM_ARRAYSIZE(modes)) - 1);
        changed = true;
    }

    SectionHeader("Clipboard");
    changed |= ImGui::Checkbox("Load clipboard text when the editor opens", &s.openWithClipboard);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    HelpTooltip("Only text clipboard content is loaded. Images and files are ignored.");

    if (s.provider == 0) {
        changed |= ImGui::Checkbox("Copy editor text to clipboard when closing", &s.copyOnClose);

        SectionHeader("Built-in Popup");
        changed |= ImGui::Checkbox("Keep editor on top", &s.alwaysOnTop);
        changed |= ImGui::Checkbox("Confirm before closing unsaved text", &s.confirmClose);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputInt("Width##editorWidth", &s.width, 20)) {
            s.width = std::clamp(s.width, 520, 3840);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputInt("Height##editorHeight", &s.height, 20)) {
            s.height = std::clamp(s.height, 360, 2160);
            changed = true;
        }

        SectionHeader("Editing");
        changed |= ImGui::Checkbox("Show line numbers", &s.showLineNumbers);
        changed |= ImGui::Checkbox("Show status bar", &s.showStatusBar);
        changed |= ImGui::Checkbox("Allow Tab inside editor", &s.allowTabInput);
    } else {
        SectionHeader("External Executable");
        ImGui::TextDisabled("Leave the path empty to use bundled clipboardpp_ide.exe.");
        char pathBuf[MAX_PATH]{};
        strncpy_s(pathBuf, s.externalPath.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(std::max(260.0f, ImGui::GetContentRegionAvail().x - 96.0f));
        if (ImGui::InputText("Path##externalEditorPath", pathBuf, sizeof(pathBuf))) {
            s.externalPath = pathBuf;
            changed = true;
        }
        ImGui::SameLine();
        if (PaddedButton("Browse", 84.0f)) {
            char picked[MAX_PATH]{};
            strncpy_s(picked, s.externalPath.c_str(), _TRUNCATE);
            if (PickExecutableFile(picked, sizeof(picked))) {
                const std::string bundledArgs = EditorSettings{}.externalArguments;
                s.externalPath = picked;
                if (s.externalArguments == bundledArgs)
                    s.externalArguments = "{file}";
                changed = true;
            }
        }

        char argsBuf[512]{};
        strncpy_s(argsBuf, s.externalArguments.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("Arguments##externalEditorArgs", argsBuf, sizeof(argsBuf))) {
            s.externalArguments = argsBuf;
            changed = true;
        }
        ImGui::SameLine(); ImGui::TextDisabled("(?)");
        HelpTooltip("{file} inserts the quoted temporary file path. {filePath} inserts the raw path.");

        char extBuf[32]{};
        strncpy_s(extBuf, s.externalTempExtension.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputText("Temp extension##externalEditorExt", extBuf, sizeof(extBuf))) {
            s.externalTempExtension = extBuf;
            changed = true;
        }
        ImGui::SameLine(); ImGui::TextDisabled("(?)");
        HelpTooltip("Leave blank to use the extension implied by the default mode.");

        changed |= ImGui::Checkbox("Wait for external editor to exit", &s.externalWaitForExit);
        changed |= ImGui::Checkbox("Copy edited file back to clipboard after exit", &s.externalReadBackToClipboard);
        if (s.externalReadBackToClipboard && !s.externalWaitForExit) {
            s.externalWaitForExit = true;
            changed = true;
        }
    }

    if (!s.enabled)
        ImGui::EndDisabled();

    }
    EndSettingsCard();

    if (changed)
        app->SetEditorSettings(s);

    if (BeginSettingsCard("##editor_shortcuts", "Editor shortcut",
                          "The editor shortcut is managed with all other global actions.")) {
        SettingsLinkButton("Open editor hotkey", SettingsDestination::Hotkeys);
    }
    EndSettingsCard();
}
