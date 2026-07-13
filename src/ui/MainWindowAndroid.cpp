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

// -- Section: Android ---------------------------------------------------------

void MainWindow::DrawAndroid() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("Android Sync");
    ImGui::Separator();
    ImGui::Spacing();

    const bool running = app->IsAndroidSyncServerRunning();
    const unsigned short port = app->AndroidSyncServerPort();
    const std::string localhostHealth = "http://127.0.0.1:" + std::to_string(port) + "/health";
    const std::string hotspotEndpoint = "http://192.168.137.1:" + std::to_string(port);

    ImGui::Text("Windows receiver: %s", running ? "listening" : "not running");
    ImGui::Text("Port: %hu", port);
    ImGui::TextWrapped("In the Android app, set Windows Endpoint to this PC's reachable address, for example:");
    ImGui::BulletText("%s", hotspotEndpoint.c_str());
    ImGui::TextDisabled("Use 192.168.137.1 when the phone is connected to the Windows hotspot.");

    ImGui::Spacing();
    if (ImGui::Button("Copy hotspot endpoint")) {
        ImGui::SetClipboardText(hotspotEndpoint.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Open local health check")) {
        ShellExecuteA(nullptr, "open", localhostHealth.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Android endpoint");
    ImGui::TextWrapped("Set the Android app API endpoint used when Clipboard++ sends items to Android or requests a sync.");

    static char endpointBuf[256]{};
    static std::string lastLoadedEndpoint;
    static std::string endpointStatus;
    const std::string currentEndpoint = app->GetAndroidDeviceEndpoint();
    if (!ImGui::IsAnyItemActive() && currentEndpoint != lastLoadedEndpoint) {
        std::snprintf(endpointBuf, sizeof(endpointBuf), "%s", currentEndpoint.c_str());
        lastLoadedEndpoint = currentEndpoint;
    }
    if (lastLoadedEndpoint.empty() && endpointBuf[0] == '\0' && !currentEndpoint.empty()) {
        std::snprintf(endpointBuf, sizeof(endpointBuf), "%s", currentEndpoint.c_str());
        lastLoadedEndpoint = currentEndpoint;
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##androidEndpointSettings",
                             "Android app API endpoint, e.g. http://192.168.137.42:8765",
                             endpointBuf, sizeof(endpointBuf));
    if (ImGui::Button("Save Android endpoint")) {
        app->SetAndroidDeviceEndpoint(endpointBuf);
        std::snprintf(endpointBuf, sizeof(endpointBuf), "%s",
                      app->GetAndroidDeviceEndpoint().c_str());
        lastLoadedEndpoint = app->GetAndroidDeviceEndpoint();
        endpointStatus = app->GetAndroidDeviceEndpoint().empty()
            ? "Android endpoint cleared"
            : "Android endpoint saved";
    }
    ImGui::SameLine();
    if (ImGui::Button("Test Android endpoint")) {
        app->SetAndroidDeviceEndpoint(endpointBuf);
        std::snprintf(endpointBuf, sizeof(endpointBuf), "%s",
                      app->GetAndroidDeviceEndpoint().c_str());
        lastLoadedEndpoint = app->GetAndroidDeviceEndpoint();
        std::string error;
        if (app->CheckAndroidDeviceHealth(&error))
            endpointStatus = "Android endpoint reachable";
        else
            endpointStatus = error.empty() ? "Android endpoint test failed" : error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear##androidEndpoint")) {
        endpointBuf[0] = '\0';
        app->SetAndroidDeviceEndpoint("");
        lastLoadedEndpoint.clear();
        endpointStatus = "Android endpoint cleared";
    }
    if (app->GetAndroidDeviceEndpoint().empty())
        ImGui::TextDisabled("No Android endpoint saved. The popup Android list will ask for one.");
    else
        ImGui::TextDisabled("Saved: %s", app->GetAndroidDeviceEndpoint().c_str());
    if (!endpointStatus.empty())
        ImGui::TextDisabled("%s", endpointStatus.c_str());

    ImGui::Spacing();
    ImGui::SeparatorText("Android app");
    ImGui::TextWrapped("Install the latest debug APK, enable Clipboard++ Capture Keyboard, then turn on Push captured items to Windows Clipboard++.");
    ImGui::TextWrapped("If the phone cannot reach the health URL, allow inbound TCP %hu in Windows Firewall.", port);

    ImGui::Spacing();
    ImGui::SeparatorText("Current behavior");
    ImGui::BulletText("Captured Android text is shown in the dedicated Android popup list.");
    ImGui::BulletText("Clipboard++ can send selected text items to the saved Android endpoint.");
    ImGui::BulletText("Android may show a clipboard access banner when the IME reads copied text.");
}
