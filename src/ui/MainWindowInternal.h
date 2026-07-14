#pragma once

#include "Appearance.h"
#include "ImGuiWidgets.h"
#include "../app/ConfigStore.h"
#include "../clipboard/ClipboardItem.h"
#include <imgui.h>
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace MainWindowInternal {

enum class SettingsDestination {
    General, Clipboard, Popup, Hotkeys, Appearance,
    Integrations, Privacy, Developer, Support, About
};

enum class SettingsStatus { Muted, Success, Warning, Error };

extern const ContentTag kDisplayTagOrder[30];

using ImGuiWidgets::KeepMouseWheelOnLastItem;
using ImGuiWidgets::SmoothScrollCurrentWindow;
using ImGuiWidgets::SliderFloatWheel;
using ImGuiWidgets::SliderIntWheel;

bool PickIcoFile(char* path, DWORD pathSize);
bool PickFontFile(char* path, DWORD pathSize);
bool PickExecutableFile(char* path, DWORD pathSize);
bool BindingHasConflict(const HotkeySettings& settings, size_t index);
std::string TimeLabel(std::chrono::system_clock::time_point tp);
std::string TagList(uint32_t tags);
std::string TrimAscii(std::string value);
bool EqualsIgnoreCase(std::string a, std::string b);
bool IsBuiltInThemeName(const std::string& name);
float UiScale();
float S(float value);
float ChromeS(float value);
float IntInputWidth(int maxVal);
float ButtonWidthForText(const char* text, float minWidth = 0.0f);
ImVec2 ButtonSizeForText(const char* text, float minWidth = 0.0f);
bool PaddedButton(const char* label, float minWidth = 0.0f);
bool DangerButton(const char* label, float minWidth = 0.0f);
bool BlueButton(const char* label, float minWidth = 0.0f);
void NavigateToSettings(SettingsDestination destination, int subTab = -1);
bool SettingsLinkButton(const char* label, SettingsDestination destination,
                        int subTab = -1);
void PageHeader(const char* title, const char* description);
bool BeginSettingsCard(const char* id, const char* title, const char* description = nullptr);
void EndSettingsCard();
void StatusMessage(SettingsStatus status, const char* text);
void EmptyState(const char* text);
bool BeginSettingsTable(const char* id, int columns,
                        ImGuiTableFlags flags = ImGuiTableFlags_None);
void EndSettingsTable();
bool SameLineIfFits(float nextItemWidth);
void SectionHeader(const char* label);
float SidebarWidth();
void PreviewText(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, const char* text);
void HelpTooltip(const char* text);
std::string JoinLines(const std::vector<std::string>& lines);
std::vector<std::string> SplitLines(const char* text);
std::string SafeFilename(std::string value);
std::filesystem::path DumpCurrentIcons();
void DrawClipboardIconAt(ImDrawList* dl, ImVec2 pos, float sz,
                         const AppearanceSettings& ap);
void DrawClipboardIcon(float sz, const AppearanceSettings& ap);
bool TitleBtn(const char* id, float x, float w, float h,
              ImU32 baseCol, ImU32 hoverCol);

} // namespace MainWindowInternal
