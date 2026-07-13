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

// -- Section: Images ----------------------------------------------------------

void MainWindow::DrawImages() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("Images");
    ImGui::Separator();
    ImGui::Spacing();

    AppConfig cfg = app->GetConfig();
    ImageSettings& s = cfg.images;
    bool changed = false;

    // -- Capture toggle --------------------------------------------------------
    changed |= ImGui::Checkbox("Capture images from clipboard", &s.captureImages);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    HelpTooltip("When off, images copied to the clipboard are ignored entirely.");
    ImGui::Spacing();

    if (!s.captureImages)
        ImGui::BeginDisabled();

    // -- Storage format --------------------------------------------------------
    ImGui::SeparatorText("Storage format");
    int fmt = static_cast<int>(s.format);
    bool fmtChanged = false;
    fmtChanged |= ImGui::RadioButton("PNG — convert to PNG (lossless, ~40-80%% smaller than raw DIB)", &fmt, 0);
    fmtChanged |= ImGui::RadioButton("JPEG — convert to JPEG (lossy, smallest file size)", &fmt, 1);
    fmtChanged |= ImGui::RadioButton("Raw — store exact clipboard bytes, no GDI+ conversion", &fmt, 2);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    HelpTooltip("Raw: DIB clipboard data is stored as-is.\n"
                "PNG clipboard data (from browsers, Snipping Tool) is stored as PNG.\n"
                "Scale-down is not available in Raw mode.");
    if (fmtChanged) { s.format = static_cast<ImageFormat>(fmt); changed = true; }

    if (s.format == ImageFormat::JPEG) {
        ImGui::SetNextItemWidth(200.0f);
        if (SliderIntWheel("JPEG quality##jpegq", &s.jpegQuality, 1, 100, "%d%%", 5))
            changed = true;
        ImGui::SameLine(); ImGui::TextDisabled("(?)");
        HelpTooltip("Higher = better quality, larger file.\n85 is a good default.");
    }

    // -- Scale-down ------------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Scale down");
    const bool rawMode = (s.format == ImageFormat::Raw);
    if (rawMode) ImGui::BeginDisabled();
    changed |= ImGui::Checkbox("Scale down large images before storing", &s.scaleDown);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    HelpTooltip("Proportionally resizes so the longest side is at most Max dimension.\n"
                "Useful for screenshots or high-DPI images.\n"
                "Not available in Raw storage mode.");
    if (s.scaleDown && !rawMode) {
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputInt("Max dimension (px)##maxdim", &s.maxDimension, 64)) {
            s.maxDimension = std::clamp(s.maxDimension, 64, 16384);
            changed = true;
        }
        ImGui::SameLine(); ImGui::TextDisabled("longest side");
    }
    if (rawMode) ImGui::EndDisabled();

    // -- Skip small images -----------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Skip small images");
    changed |= ImGui::Checkbox("Ignore images smaller than minimum size", &s.skipSmallImages);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    HelpTooltip("Avoids storing tiny icons, favicons, or copy-protection placeholder images.");
    if (s.skipSmallImages) {
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputInt("Min W##minw", &s.minWidth, 8)) {
            s.minWidth = std::clamp(s.minWidth, 1, 4096);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputInt("Min H##minh", &s.minHeight, 8)) {
            s.minHeight = std::clamp(s.minHeight, 1, 4096);
            changed = true;
        }
        ImGui::SameLine(); ImGui::TextDisabled("pixels");
    }

    // -- Max stored images -----------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Storage limit");
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::InputInt("Max stored images##maximgs", &s.maxImages, 10)) {
        s.maxImages = std::clamp(s.maxImages, 0, 100000);
        changed = true;
    }
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    HelpTooltip("Oldest images are purged when this limit is exceeded.\nSet to 0 for unlimited.");
    if (s.maxImages == 0)
        ImGui::TextDisabled("Unlimited storage — images accumulate until manually cleared.");
    else
        ImGui::TextDisabled("Oldest images are removed when the limit is reached.");

    if (!s.captureImages)
        ImGui::EndDisabled();

    if (changed)
        app->SetImageSettings(s);

    // -- DB stats --------------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Database");

    ImageStore* store = app->GetImageStore();
    if (store && store->IsOpen()) {
        const std::filesystem::path dbPath = ConfigStore::Directory() / "images.db";
        std::error_code ec;
        const uintmax_t dbBytes = std::filesystem::file_size(dbPath, ec);
        if (!ec) {
            if (dbBytes >= 1024 * 1024)
                ImGui::TextDisabled("DB size: %.2f MB", static_cast<double>(dbBytes) / (1024.0 * 1024.0));
            else
                ImGui::TextDisabled("DB size: %.1f KB", static_cast<double>(dbBytes) / 1024.0);
        } else {
            ImGui::TextDisabled("DB size: (unknown)");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("  Path: %s", dbPath.string().c_str());

        static bool confirmClear = false;
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 30, 30, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(220, 55, 55, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(140, 15, 15, 255));
        if (ImGui::Button("Clear all stored images")) {
            confirmClear = true;
            ImGui::OpenPopup("Confirm clear images");
        }
        ImGui::PopStyleColor(3);

        if (ImGui::BeginPopupModal("Confirm clear images", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("This will permanently delete ALL stored images from the database.");
            ImGui::TextWrapped("This cannot be undone.");
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(220, 35, 35, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 65, 65, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 20, 20, 255));
            if (ImGui::Button("Delete all images", {S(160.0f), 0.0f})) {
                store->DeleteAll();
                confirmClear = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {S(90.0f), 0.0f}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                confirmClear = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    } else {
        ImGui::TextDisabled("Image database not open.");
    }
}
