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

// -- Section: About -----------------------------------------------------------

void MainWindow::DrawAbout() {
    PageHeader("About", "Version, architecture, bundled tools, and project information.");

    // -- Icon + app identity --------------------------------------------------
    Application* app = Application::Get();
    if (BeginSettingsCard("##about_identity", "Clipboard++",
                          "A lean Windows clipboard manager built with C++17, Dear ImGui, and DirectX 11.")) {
    float iconSz = S(64.0f);
    if (app) {
        DrawClipboardIcon(iconSz, app->GetAppearance());
        ImGui::SameLine(0, S(16.0f));
    }
    ImGui::BeginGroup();
    ImGui::TextDisabled("Version 0.1.0  (Beta 7)");
    ImGui::EndGroup();
    }
    EndSettingsCard();

    if (BeginSettingsCard("##about_components", "Components", nullptr)) {
    ImGui::TextDisabled("Built with:");
    ImGui::BulletText("Dear ImGui (docking branch)  -  ocornut/imgui");
    ImGui::BulletText("nlohmann/json v3.11.3");
    ImGui::BulletText("SQLite 3.45.0");
    ImGui::BulletText("DirectX 11 / WIC / Win32 API");

    ImGui::Spacing();
    ImGui::TextDisabled("Tools:");
    ImGui::BulletText("SQLite Editor  -  standalone database browser");
    ImGui::BulletText("JSON Viewer    -  standalone JSON file viewer");
    }
    EndSettingsCard();

    if (BeginSettingsCard("##about_license", "License", nullptr)) {
        ImGui::TextUnformatted("MIT");
    }
    EndSettingsCard();
}
