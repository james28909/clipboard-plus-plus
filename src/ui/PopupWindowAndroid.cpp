#include "PopupWindow.h"
#include "../app/Application.h"
#include "ImGuiWidgets.h"

#include <imgui.h>

#include <cstdio>
#include <string>
#include <vector>

using ImGuiWidgets::SmoothScrollCurrentWindow;

// -- Android panel ------------------------------------------------------------

void PopupWindow::DrawAndroidPanel() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGuiWindowFlags childFlags = m_appearance.showScrollbars
        ? ImGuiWindowFlags_None
        : ImGuiWindowFlags_NoScrollbar;
    childFlags |= ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##android_clipboard_panel", {0.f, 0.f}, ImGuiChildFlags_None, childFlags);

    const bool running = app->IsAndroidSyncServerRunning();
    const unsigned short port = app->AndroidSyncServerPort();
    const std::string endpoint = "http://192.168.137.1:" + std::to_string(port);
    if (m_androidEndpointBuf[0] == '\0' && !app->GetAndroidDeviceEndpoint().empty())
        std::snprintf(m_androidEndpointBuf, sizeof(m_androidEndpointBuf), "%s",
                      app->GetAndroidDeviceEndpoint().c_str());
    const std::vector<AndroidClipboardEntry> entries = app->GetAndroidClipboardEntries();
    const bool justOpened = m_androidPanelOpen && !m_lastAndroidPanelOpen;
    m_lastAndroidPanelOpen = m_androidPanelOpen;
    if (justOpened) {
        const std::string savedEndpoint = app->GetAndroidDeviceEndpoint();
        if (!savedEndpoint.empty()) {
            std::snprintf(m_androidEndpointBuf, sizeof(m_androidEndpointBuf), "%s",
                          savedEndpoint.c_str());
            m_androidEndpointEditing = false;
            m_dialogTextCapture = false;
        }
    }

    auto requestSync = [&]() {
        std::string error;
        if (app->RequestAndroidSyncToWindows(&error))
            m_androidSyncStatus = "Sync requested";
        else
            m_androidSyncStatus = error.empty() ? "Sync failed" : error;
    };

    if (justOpened && !app->GetAndroidDeviceEndpoint().empty())
        requestSync();

    ImGui::TextDisabled("Android Clipboard");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Receiver: %s   Items: %zu", running ? "listening" : "not running", entries.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Sync"))
        requestSync();
    ImGui::SameLine();
    if (ImGui::SmallButton("Test Android")) {
        std::string error;
        if (app->CheckAndroidDeviceHealth(&error))
            m_androidSyncStatus = "Android endpoint reachable";
        else
            m_androidSyncStatus = error.empty() ? "Android endpoint test failed" : error;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy endpoint")) {
        ImGui::SetClipboardText(endpoint.c_str());
    }
    if (!m_androidSyncStatus.empty())
        ImGui::TextDisabled("%s", m_androidSyncStatus.c_str());

    ImGui::Spacing();
    const std::string savedEndpoint = app->GetAndroidDeviceEndpoint();
    const bool hasSavedEndpoint = !savedEndpoint.empty();
    if (hasSavedEndpoint && !m_androidEndpointEditing) {
        std::string displayEndpoint = savedEndpoint;
        if (displayEndpoint.rfind("http://", 0) == 0)
            displayEndpoint.erase(0, 7);
        if (displayEndpoint.rfind("https://", 0) == 0)
            displayEndpoint.erase(0, 8);
        const size_t slash = displayEndpoint.find('/');
        if (slash != std::string::npos)
            displayEndpoint.erase(slash);
        ImGui::Text("Android API: %s", displayEndpoint.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Change")) {
            std::snprintf(m_androidEndpointBuf, sizeof(m_androidEndpointBuf), "%s", savedEndpoint.c_str());
            m_androidEndpointEditing = true;
            m_dialogTextCapture = true;
        }
    } else {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##android_device_endpoint",
                                 "Android app API endpoint, e.g. http://192.168.137.42:8765",
                                 m_androidEndpointBuf, sizeof(m_androidEndpointBuf));
        const bool endpointActive = ImGui::IsItemActive();
        if (endpointActive || ImGui::IsItemClicked()) {
            ActivateKeyboardCapture();
            m_dialogTextCapture = true;
        }
        if (ImGui::SmallButton("Save Android API endpoint")) {
            app->SetAndroidDeviceEndpoint(m_androidEndpointBuf);
            std::snprintf(m_androidEndpointBuf, sizeof(m_androidEndpointBuf), "%s",
                          app->GetAndroidDeviceEndpoint().c_str());
            m_androidEndpointEditing = false;
            m_dialogTextCapture = false;
        }
        if (hasSavedEndpoint) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel")) {
                std::snprintf(m_androidEndpointBuf, sizeof(m_androidEndpointBuf), "%s", savedEndpoint.c_str());
                m_androidEndpointEditing = false;
                m_dialogTextCapture = false;
            }
        }
        if (m_dialogTextCapture && !endpointActive)
            m_dialogTextCapture = false;
    }

    ImGui::Spacing();
    if (entries.empty()) {
        ImGui::TextDisabled("No Android pushed items yet.");
        ImGui::TextWrapped("Set the Android app's Windows Endpoint to %s and keep auto push enabled.", endpoint.c_str());
    } else {
        for (const AndroidClipboardEntry& entry : entries) {
            const std::string preview = entry.text.substr(0, 140);
            std::string label = (entry.pinned ? "[P] " : "") + preview + "##android_" + std::to_string(entry.id);
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                ImGui::SetClipboardText(entry.text.c_str());
            }

            if (ImGui::BeginPopupContextItem()) {
                ImGui::TextDisabled("Android item %llu", static_cast<unsigned long long>(entry.id));
                ImGui::Separator();
                if (ImGui::MenuItem("Copy to Windows clipboard")) {
                    ImGui::SetClipboardText(entry.text.c_str());
                }
                if (ImGui::MenuItem("Copy to Clipboard++ history")) {
                    app->InsertExternalClipboardText(entry.text, entry.source.empty() ? "android" : entry.source);
                }
                if (ImGui::MenuItem(entry.pinned ? "Unpin" : "Pin")) {
                    app->SetAndroidClipboardEntryPinned(entry.id, !entry.pinned);
                }
                if (ImGui::MenuItem("Remove")) {
                    app->RemoveAndroidClipboardEntry(entry.id);
                }
                ImGui::EndPopup();
            }
        }
    }

    SmoothScrollCurrentWindow("popup_android", 112.0f, 0.22f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
