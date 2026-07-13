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
    ImGui::TextDisabled("Privacy & Security");
    ImGui::Separator();
    ImGui::Spacing();

    static bool detectSecrets = true;
    static bool autoDiscard   = false;
    static bool clearOnLock   = false;
    static char exclusionBuf[512] = "KeePass.exe\n1Password.exe\nBitwarden.exe";

    ImGui::Checkbox("Detect secret patterns (API keys, tokens, PEM, JWTs)", &detectSecrets);
    if (detectSecrets) {
        ImGui::Indent();
        ImGui::Checkbox("Auto-discard detected secrets (no prompt)", &autoDiscard);
        ImGui::Unindent();
    }
    ImGui::Spacing();
    ImGui::Checkbox("Clear history when Windows locks", &clearOnLock);
    ImGui::Spacing();
    ImGui::Text("Process exclusion list (one per line):");
    ImGui::InputTextMultiline("##excl", exclusionBuf, sizeof(exclusionBuf), {-1, 120});
}
