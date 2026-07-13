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

// -- Section nav ---------------------------------------------------------------

enum Section {
    SEC_GENERAL    = 0,
    SEC_HOTKEYS    = 1,
    SEC_APPEARANCE = 2,
    SEC_HISTORY    = 3,
    SEC_FILTERS    = 4,
    SEC_EDITOR     = 5,
    SEC_IMAGES     = 6,
    SEC_ANDROID    = 7,
    SEC_PRIVACY    = 8,
#ifndef NDEBUG
    SEC_DEVELOPER  = 9,
    SEC_ABOUT      = 10,
#else
    SEC_ABOUT      = 9,
#endif
    SEC_COUNT
};

static const char* kSectionLabels[SEC_COUNT] = {
#ifndef NDEBUG
    "General", "Hotkeys", "Appearance", "History",
    "Filters", "Editor", "Images", "Android", "Privacy", "Developer", "About",
#else
    "General", "Hotkeys", "Appearance", "History",
    "Filters", "Editor", "Images", "Android", "Privacy", "About",
#endif
};

static int s_activeSection = SEC_GENERAL;
static int s_focusFrames = 0;

namespace MainWindowInternal {

const ContentTag kDisplayTagOrder[30] = {
    TAG_SECRET, TAG_URL, TAG_EMAIL, TAG_JSON, TAG_XML, TAG_SQL,
    TAG_CODE, TAG_COMMAND, TAG_CONFIG, TAG_FILE, TAG_FOLDER, TAG_PATH,
    TAG_IMAGE_FILE, TAG_DOCUMENT, TAG_ARCHIVE, TAG_EXECUTABLE, TAG_SCRIPT,
    TAG_DATA, TAG_AUDIO, TAG_VIDEO, TAG_MARKDOWN, TAG_CSV, TAG_HTML,
    TAG_HEX, TAG_UUID, TAG_IP, TAG_DATE, TAG_BASE64, TAG_LOG, TAG_PHONE
};

bool PickIcoFile(char* path, DWORD pathSize) {
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = Application::Get() ? Application::Get()->GetHwnd() : nullptr;
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = pathSize;
    ofn.lpstrFilter = "Icon Files (*.ico)\0*.ico\0All Files (*.*)\0*.*\0";
    ofn.lpstrTitle  = "Choose exe icon";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) != FALSE;
}

bool PickFontFile(char* path, DWORD pathSize) {
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = Application::Get() ? Application::Get()->GetHwnd() : nullptr;
    ofn.lpstrFile = path;
    ofn.nMaxFile = pathSize;
    ofn.lpstrFilter = "Font Files (*.ttf;*.otf)\0*.ttf;*.otf\0All Files (*.*)\0*.*\0";
    ofn.lpstrTitle = "Choose font";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) != FALSE;
}

bool PickExecutableFile(char* path, DWORD pathSize) {
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = Application::Get() ? Application::Get()->GetHwnd() : nullptr;
    ofn.lpstrFile = path;
    ofn.nMaxFile = pathSize;
    ofn.lpstrFilter = "Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.lpstrTitle = "Choose external editor";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) != FALSE;
}

bool BindingHasConflict(const HotkeySettings& settings, size_t index) {
    if (index >= settings.bindings.size()) return false;
    const KeyBinding& a = settings.bindings[index];
    if (a.vkey == 0) return false;

    for (size_t i = 0; i < settings.bindings.size(); ++i) {
        if (i == index) continue;
        const KeyBinding& b = settings.bindings[i];
        if (a.ctrl == b.ctrl && a.shift == b.shift &&
            a.alt == b.alt && a.vkey == b.vkey) {
            return true;
        }
    }
    return false;
}

std::string TimeLabel(std::chrono::system_clock::time_point tp) {
    if (tp.time_since_epoch().count() == 0)
        return "(none)";
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string TagList(uint32_t tags) {
    if (tags == TAG_NONE)
        return "(none)";

    std::string out;
    for (ContentTag tag : kDisplayTagOrder) {
        if ((tags & tag) == 0)
            continue;
        if (!out.empty())
            out += ", ";
        out += ContentDetector::TagName(tag);
    }
    return out.empty() ? "(unknown)" : out;
}

std::string TrimAscii(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

bool EqualsIgnoreCase(std::string a, std::string b) {
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::transform(a.begin(), a.end(), a.begin(), lower);
    std::transform(b.begin(), b.end(), b.begin(), lower);
    return a == b;
}

bool IsBuiltInThemeName(const std::string& name) {
    for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
        if (EqualsIgnoreCase(name, ThemeName(static_cast<ThemeId>(i))))
            return true;
    }
    return false;
}

float UiScale() {
    Application* app = Application::Get();
    return app ? EffectiveUiScale(app->GetAppearance()) : 1.0f;
}

float S(float value) {
    return value;
}

float ChromeS(float value) {
    return value * UiScale();
}

// Width for an InputInt field wide enough to show maxVal at any font scale.
float IntInputWidth(int maxVal) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", maxVal);
    return std::max(132.0f, ImGui::CalcTextSize(tmp).x
         + ImGui::GetStyle().FramePadding.x * 2.0f
         + ImGui::GetFrameHeight() * 2.0f
         + 22.0f); // +/- arrow buttons plus comfortable value padding
}

float ButtonWidthForText(const char* text, float minWidth) {
    const ImGuiStyle& style = ImGui::GetStyle();
    return std::max(minWidth, ImGui::CalcTextSize(text).x + style.FramePadding.x * 2.0f);
}

ImVec2 ButtonSizeForText(const char* text, float minWidth) {
    return {ButtonWidthForText(text, minWidth), 0.0f};
}

bool PaddedButton(const char* label, float minWidth) {
    return ImGui::Button(label, ButtonSizeForText(label, minWidth));
}

bool DangerButton(const char* label, float minWidth) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(220, 35, 35, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 65, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 20, 20, 255));
    const bool clicked = PaddedButton(label, minWidth);
    ImGui::PopStyleColor(3);
    return clicked;
}

bool BlueButton(const char* label, float minWidth) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(77, 145, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(106, 166, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(46, 112, 220, 255));
    const bool clicked = PaddedButton(label, minWidth);
    ImGui::PopStyleColor(3);
    return clicked;
}

void SectionHeader(const char* label) {
    ImGui::Spacing();
    ImGui::SeparatorText(label);
    ImGui::Spacing();
}

float SidebarWidth() {
    float widest = 0.0f;
    for (const char* label : kSectionLabels)
        widest = std::max(widest, ImGui::CalcTextSize(label).x);
    const float childPaddingX = 12.0f;
    const float buttonPaddingX = 14.0f;
    return std::max(168.0f, widest + childPaddingX * 2.0f + buttonPaddingX * 2.0f + 16.0f);
}

void PreviewText(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, const char* text) {
    const ImVec4 clip(min.x, min.y, max.x, max.y);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {min.x, min.y},
                color, text, nullptr, 0.0f, &clip);
}

void HelpTooltip(const char* text) {
    Application* app = Application::Get();
    if (!app || !app->GetUiSettings().showHelperText)
        return;

    const UiSettings& ui = app->GetUiSettings();
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        return;

    static ImGuiID activeItem = 0;
    static double shownAt = 0.0;
    const ImGuiID item = ImGui::GetItemID();
    const double now = ImGui::GetTime();
    if (item != activeItem) {
        activeItem = item;
        shownAt = now;
    }
    const double maxSeconds = std::max(0.5, static_cast<double>(ui.helperDurationMs) / 1000.0);
    if (now - shownAt <= maxSeconds)
        ImGui::SetTooltip("%s", text);
}

std::string JoinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) {
        if (!out.empty())
            out += "\n";
        out += line;
    }
    return out;
}

std::vector<std::string> SplitLines(const char* text) {
    std::vector<std::string> lines;
    std::istringstream in(text ? text : "");
    std::string line;
    while (std::getline(in, line)) {
        line = TrimAscii(line);
        if (!line.empty())
            lines.push_back(line);
    }
    return lines;
}

std::string SafeFilename(std::string value) {
    if (value.empty())
        return "unnamed";
    for (char& c : value) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            c = '_';
        }
    }
    return value;
}

std::filesystem::path DumpCurrentIcons() {
    const std::filesystem::path outDir = ConfigStore::Directory() / "icon-dump";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec)
        return {};

    for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
        const ThemeId theme = static_cast<ThemeId>(i);
        const std::filesystem::path outPath =
            outDir / (SafeFilename(ThemeName(theme)) + ".ico");
        TrayIcon::WriteThemeIco(ThemeDefaults(theme), outPath.wstring());
    }

    if (Application* app = Application::Get()) {
        const AppearanceSettings& ap = app->GetAppearance();
        const std::string customName = ap.customColors
            ? ap.customThemeName
            : std::string("active_") + ThemeName(ap.theme);
        TrayIcon::WriteThemeIco(ap, (outDir / (SafeFilename(customName) + "_current.ico")).wstring());

        if (!ap.exeIconPath.empty()) {
            const std::filesystem::path src(ap.exeIconPath);
            if (std::filesystem::exists(src, ec)) {
                std::filesystem::copy_file(
                    src,
                    outDir / ("user_selected_" + SafeFilename(src.filename().string())),
                    std::filesystem::copy_options::overwrite_existing,
                    ec);
            }
        }

        app->AddDeveloperEvent("dumped themed icons to " + outDir.string());
    }

    return outDir;
}

using ImGuiWidgets::KeepMouseWheelOnLastItem;
using ImGuiWidgets::SmoothScrollCurrentWindow;
using ImGuiWidgets::SliderFloatWheel;
using ImGuiWidgets::SliderIntWheel;

// -- Clipboard icon (theme-driven, ImDrawList) ---------------------------------

void DrawClipboardIconAt(ImDrawList* dl, ImVec2 pos, float sz,
                                const AppearanceSettings& ap) {
    const float s = sz / 64.0f;
    auto P = [&](float x, float y) { return ImVec2{pos.x + x * s, pos.y + y * s}; };
    auto C = [](const ImVec4& v)   { return ImGui::ColorConvertFloat4ToU32(v); };

    const ImU32 outline = IM_COL32(17, 17, 17, 255);

    // Board (gradient via two overlapping rects: top-half tint, bottom-half tint)
    ImVec2 b0 = P(9,  12), b1 = P(55, 61);
    float  bMy = (b0.y + b1.y) * 0.5f;
    dl->AddRectFilled(b0, {b1.x, bMy}, C(ap.iconBoardTop),  3.0f * s);
    dl->AddRectFilled({b0.x, bMy}, b1, C(ap.iconBoardBottom), 3.0f * s);
    dl->AddRect(b0, b1, outline, 3.0f * s, 0, 2.5f * s);

    // Clip housing (metallic gray, fixed)
    const ImU32 clipTop = IM_COL32(220, 220, 220, 255);
    const ImU32 clipBot = IM_COL32( 80,  80,  80, 255);
    ImVec2 c0 = P(21, 3), c1 = P(43, 17);
    float  cMy = (c0.y + c1.y) * 0.5f;
    dl->AddRectFilled(c0, {c1.x, cMy}, clipTop, 3.0f * s);
    dl->AddRectFilled({c0.x, cMy}, c1, clipBot,  3.0f * s);
    dl->AddRect(c0, c1, outline, 3.0f * s, 0, 2.0f * s);
    // Clip slot
    dl->AddRectFilled(P(26, 7), P(38, 15), IM_COL32(10, 10, 10, 255), 2.0f * s);
    dl->AddRect(P(26, 7), P(38, 15), IM_COL32(70, 70, 70, 255), 2.0f * s, 0, 1.0f * s);

    // Paper
    dl->AddRectFilled(P(13, 18), P(51, 61), C(ap.iconPaper), 2.0f * s);
    dl->AddRect(P(13, 18), P(51, 61), outline, 2.0f * s, 0, 1.75f * s);

    // Red margin line (vertical) — double-stroke
    dl->AddLine(P(20, 22), P(20, 58), IM_COL32(0,0,0,255),    5.75f * s);
    dl->AddLine(P(20, 22), P(20, 58), C(ap.iconMarginLine),    4.0f  * s);

    // 4 ruled lines (horizontal) — double-stroke
    for (int i = 0; i < 4; i++) {
        float y = 28.0f + i * 9.0f;
        dl->AddLine(P(23, y), P(48, y), IM_COL32(0,0,0,255),  3.25f * s);
        dl->AddLine(P(23, y), P(48, y), C(ap.iconRuledLines),  2.0f  * s);
    }
}

void DrawClipboardIcon(float sz, const AppearanceSettings& ap) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy({sz, sz});
    DrawClipboardIconAt(ImGui::GetWindowDrawList(), pos, sz, ap);
}

// -- Title bar helpers ---------------------------------------------------------

// Draws a single title bar button. Returns true on click.
bool TitleBtn(const char* id, float x, float w, float h, ImU32 baseCol, ImU32 hoverCol) {
    ImGui::SetCursorPos(ImVec2(x, 0.0f));
    ImGui::InvisibleButton(id, ImVec2(w, h));
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
                   (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left));
    ImVec2 wp = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered())
        dl->AddRectFilled({wp.x + x, wp.y}, {wp.x + x + w, wp.y + h}, hoverCol);
    else if ((baseCol >> 24) != 0)
        dl->AddRectFilled({wp.x + x, wp.y}, {wp.x + x + w, wp.y + h}, baseCol);
    return clicked;
}

// -- Title bar -----------------------------------------------------------------

} // namespace MainWindowInternal

using namespace MainWindowInternal;

void MainWindow::DrawTitleBar() {
    const float W  = ImGui::GetWindowSize().x;
    const float H  = ChromeS((float)kTitleBarHeight);
    const float BW = ChromeS((float)kTitleBtnWidth);

    ImVec2      wp  = ImGui::GetWindowPos();
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    HWND        wnd = Application::Get()->GetHwnd();

    // -- Themed button hover colors -------------------------------------------
    const AppearanceSettings& ap = Application::Get()->GetAppearance();
    const ImU32 minBaseCol    = ImGui::ColorConvertFloat4ToU32(ap.titleMinBase);
    const ImU32 maxBaseCol    = ImGui::ColorConvertFloat4ToU32(ap.titleMaxBase);
    const ImU32 closeBaseCol  = ImGui::ColorConvertFloat4ToU32(ap.titleCloseBase);
    const ImU32 exitBaseCol   = ImGui::ColorConvertFloat4ToU32(ap.titleExitBase);
    const ImU32 minHoverCol   = ImGui::ColorConvertFloat4ToU32(ap.titleMinHover);
    const ImU32 maxHoverCol   = ImGui::ColorConvertFloat4ToU32(ap.titleMaxHover);
    const ImU32 closeHoverCol = ImGui::ColorConvertFloat4ToU32(ap.titleCloseHover);
    const ImU32 exitHoverCol  = ImGui::ColorConvertFloat4ToU32(ap.titleExitHover);
    const ImU32 titleBg       = ImGui::ColorConvertFloat4ToU32(ap.titleBarBg);
    const ImU32 titleLine     = ImGui::ColorConvertFloat4ToU32(ap.titleBarBorder);
    const ImU32 titleText     = ImGui::ColorConvertFloat4ToU32(ap.titleBarText);
    const ImU32 minGlyph      = ImGui::ColorConvertFloat4ToU32(ap.titleMinGlyph);
    const ImU32 maxGlyph      = ImGui::ColorConvertFloat4ToU32(ap.titleMaxGlyph);
    const ImU32 exitGlyph     = ImGui::ColorConvertFloat4ToU32(ap.titleExitGlyph);
    const ImU32 closeGlyph    = ImGui::ColorConvertFloat4ToU32(ap.titleCloseGlyph);

    // -- Background -----------------------------------------------------------
    dl->AddRectFilled(wp, {wp.x + W, wp.y + H}, titleBg);

    // -- App icon + title text ------------------------------------------------
    float textY   = (H - ImGui::GetTextLineHeight()) * 0.5f;
    float cursorX = ChromeS(10.0f);

    {
        float iconSz = H - ChromeS(10.0f);
        ImGui::SetCursorPos({cursorX, (H - iconSz) * 0.5f});
        DrawClipboardIcon(iconSz, ap);
        cursorX += iconSz + ChromeS(6.0f);
    }

    ImGui::SetCursorPos({cursorX, textY});
    ImGui::PushStyleColor(ImGuiCol_Text, titleText);
    ImGui::TextUnformatted("Clipboard++");
    ImGui::PopStyleColor();

    // -- Buttons: minimize | maximize/restore | exit | close -------------------
    const bool isMax = IsZoomed(wnd) != 0;
    float bx = std::max(0.0f, W - BW * 4.0f);

    // - Minimize -
    if (TitleBtn("##min", bx, BW, H, minBaseCol, minHoverCol))
        PostMessageW(wnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    {
        ImVec2 c = {wp.x + bx + BW * 0.5f, wp.y + H * 0.5f + 1.0f};
        dl->AddLine({c.x - ChromeS(5.0f), c.y}, {c.x + ChromeS(5.0f), c.y}, minGlyph, ChromeS(1.5f));
    }
    bx += BW;

    // - Maximize / Restore -
    if (TitleBtn("##max", bx, BW, H, maxBaseCol, maxHoverCol))
        PostMessageW(wnd, WM_SYSCOMMAND, isMax ? SC_RESTORE : SC_MAXIMIZE, 0);
    {
        ImVec2 c = {wp.x + bx + BW * 0.5f, wp.y + H * 0.5f};
        if (isMax) {
            dl->AddRect({c.x - ChromeS(2.0f), c.y - ChromeS(5.0f)}, {c.x + ChromeS(5.0f), c.y + ChromeS(2.0f)},
                        maxGlyph, 0, 0, ChromeS(1.2f));
            dl->AddRectFilled({c.x - ChromeS(5.0f), c.y - ChromeS(2.0f)}, {c.x + ChromeS(1.0f), c.y + ChromeS(5.0f)},
                              titleBg);
            dl->AddRect({c.x - ChromeS(5.0f), c.y - ChromeS(2.0f)}, {c.x + ChromeS(2.0f), c.y + ChromeS(5.0f)},
                        maxGlyph, 0, 0, ChromeS(1.2f));
        } else {
            dl->AddRect({c.x - ChromeS(5.0f), c.y - ChromeS(5.0f)}, {c.x + ChromeS(5.0f), c.y + ChromeS(5.0f)},
                        maxGlyph, 0, 0, ChromeS(1.2f));
        }
    }
    bx += BW;

    // - Exit (quit app) -
    if (TitleBtn("##exit", bx, BW, H, exitBaseCol, exitHoverCol))
        PostMessageW(wnd, WM_DESTROY, 0, 0);
    {
        ImVec2 c   = {wp.x + bx + BW * 0.5f, wp.y + H * 0.5f + ChromeS(1.0f)};
        const float r        = ChromeS(4.5f);
        const float gapHalf  = 0.52f; // ~30 degree gap at top
        // Arc from just past top-right of gap, clockwise through bottom, to just past top-left
        static constexpr float kPi = 3.14159265f;
        dl->PathArcTo(c, r, -kPi * 0.5f + gapHalf, -kPi * 0.5f + kPi * 2.0f - gapHalf, 32);
        dl->PathStroke(exitGlyph, false, ChromeS(1.5f));
        // Vertical stem through the gap up to slightly above the circle
        dl->AddLine({c.x, c.y}, {c.x, c.y - r - ChromeS(2.0f)}, exitGlyph, ChromeS(1.5f));
    }
    bx += BW;

    // - Close -
    if (TitleBtn("##close", bx, BW, H, closeBaseCol, closeHoverCol))
        PostMessageW(wnd, WM_CLOSE, 0, 0);
    {
        ImVec2 c   = {wp.x + bx + BW * 0.5f, wp.y + H * 0.5f};
        dl->AddLine({c.x - ChromeS(5.0f), c.y - ChromeS(5.0f)}, {c.x + ChromeS(5.0f), c.y + ChromeS(5.0f)}, closeGlyph, ChromeS(1.5f));
        dl->AddLine({c.x + ChromeS(5.0f), c.y - ChromeS(5.0f)}, {c.x - ChromeS(5.0f), c.y + ChromeS(5.0f)}, closeGlyph, ChromeS(1.5f));
    }

    // -- Separator line --------------------------------------------------------
    dl->AddLine({wp.x, wp.y + H}, {wp.x + W, wp.y + H}, titleLine);

    // Advance ImGui cursor below the title bar
    ImGui::SetCursorPos({0.0f, H});
}

// -- Main draw -----------------------------------------------------------------

void MainWindow::RequestFocus() {
    s_focusFrames = 3;
}

void MainWindow::Draw(bool& open) {
    Application* app = Application::Get();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    if (s_focusFrames > 0) {
        ImGui::SetNextWindowFocus();
        --s_focusFrames;
    }

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin("##main", nullptr, flags)) {
        ImGui::End();
        return;
    }

    DrawTitleBar();

    // -- Sidebar + content layout below the title bar --------------------------
    const float titleH   = ChromeS((float)kTitleBarHeight);
    const float pad      = S(18.0f);
    const float sidebarW = SidebarWidth();
    const float contentW = ImGui::GetContentRegionAvail().x - sidebarW - pad * 2.0f;

    ImGui::SetCursorPos({pad, titleH + pad});

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(12.0f), S(12.0f)});
    ImGui::BeginChild("##sidebar", {sidebarW, 0},
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollWithMouse);
    DrawSidebarNav(s_activeSection);
    SmoothScrollCurrentWindow("sidebar");
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine(0, pad);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {S(22.0f), S(20.0f)});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {S(10.0f), S(13.0f)});
    ImGuiWindowFlags contentFlags = app && !app->GetAppearance().showScrollbars
        ? ImGuiWindowFlags_NoScrollbar
        : ImGuiWindowFlags_None;
    contentFlags |= ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##content", {contentW, 0},
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      contentFlags);
    switch (s_activeSection) {
    case SEC_GENERAL:    DrawGeneral();    break;
    case SEC_HOTKEYS:    DrawHotkeys();    break;
    case SEC_APPEARANCE: DrawAppearance(); break;
    case SEC_HISTORY:    DrawHistory();    break;
    case SEC_FILTERS:    DrawFilters();    break;
    case SEC_EDITOR:     DrawEditor();     break;
    case SEC_IMAGES:     DrawImages();     break;
    case SEC_ANDROID:    DrawAndroid();    break;
    case SEC_PRIVACY:    DrawPrivacy();    break;
#ifndef NDEBUG
    case SEC_DEVELOPER:  DrawDeveloper();  break;
#endif
    case SEC_ABOUT:      DrawAbout();      break;
    }
    SmoothScrollCurrentWindow("settings_content");
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    ImGui::End();
}

// -- Sidebar nav ---------------------------------------------------------------

void MainWindow::DrawSidebarNav(int& selected) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 6.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {14.0f, 8.0f});
    const AppearanceSettings appearance = Application::Get()
        ? Application::Get()->GetAppearance()
        : AppearanceSettings{};
    const ImVec4 selectedColor = appearance.customColors
        ? appearance.selectedTab
        : ThemeDefaults(appearance.theme).selectedTab;
    for (int i = 0; i < SEC_COUNT; ++i) {
        bool active = (i == selected);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, selectedColor);
        if (ImGui::Button(kSectionLabels[i], {-1.0f, 0.0f}))
            selected = i;
        if (active)
            ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar(2);
}
