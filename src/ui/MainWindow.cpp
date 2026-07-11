#include "MainWindow.h"
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

static constexpr ContentTag kDisplayTagOrder[] = {
    TAG_SECRET, TAG_URL, TAG_EMAIL, TAG_JSON, TAG_XML, TAG_SQL,
    TAG_CODE, TAG_COMMAND, TAG_CONFIG, TAG_FILE, TAG_FOLDER, TAG_PATH,
    TAG_IMAGE_FILE, TAG_DOCUMENT, TAG_ARCHIVE, TAG_EXECUTABLE, TAG_SCRIPT,
    TAG_DATA, TAG_AUDIO, TAG_VIDEO, TAG_MARKDOWN, TAG_CSV, TAG_HTML,
    TAG_HEX, TAG_UUID, TAG_IP, TAG_DATE, TAG_BASE64, TAG_LOG, TAG_PHONE
};

static bool PickIcoFile(char* path, DWORD pathSize) {
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

static bool PickFontFile(char* path, DWORD pathSize) {
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

static bool PickExecutableFile(char* path, DWORD pathSize) {
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

static bool BindingHasConflict(const HotkeySettings& settings, size_t index) {
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

static std::string TimeLabel(std::chrono::system_clock::time_point tp) {
    if (tp.time_since_epoch().count() == 0)
        return "(none)";
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

static std::string TagList(uint32_t tags) {
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

static std::string TrimAscii(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

static bool EqualsIgnoreCase(std::string a, std::string b) {
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::transform(a.begin(), a.end(), a.begin(), lower);
    std::transform(b.begin(), b.end(), b.begin(), lower);
    return a == b;
}

static bool IsBuiltInThemeName(const std::string& name) {
    for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
        if (EqualsIgnoreCase(name, ThemeName(static_cast<ThemeId>(i))))
            return true;
    }
    return false;
}

static float UiScale() {
    Application* app = Application::Get();
    return app ? EffectiveUiScale(app->GetAppearance()) : 1.0f;
}

static float S(float value) {
    return value;
}

static float ChromeS(float value) {
    return value * UiScale();
}

// Width for an InputInt field wide enough to show maxVal at any font scale.
static float IntInputWidth(int maxVal) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", maxVal);
    return std::max(132.0f, ImGui::CalcTextSize(tmp).x
         + ImGui::GetStyle().FramePadding.x * 2.0f
         + ImGui::GetFrameHeight() * 2.0f
         + 22.0f); // +/- arrow buttons plus comfortable value padding
}

static float ButtonWidthForText(const char* text, float minWidth = 0.0f) {
    const ImGuiStyle& style = ImGui::GetStyle();
    return std::max(minWidth, ImGui::CalcTextSize(text).x + style.FramePadding.x * 2.0f);
}

static ImVec2 ButtonSizeForText(const char* text, float minWidth = 0.0f) {
    return {ButtonWidthForText(text, minWidth), 0.0f};
}

static bool PaddedButton(const char* label, float minWidth = 0.0f) {
    return ImGui::Button(label, ButtonSizeForText(label, minWidth));
}

static bool DangerButton(const char* label, float minWidth = 0.0f) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(220, 35, 35, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 65, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 20, 20, 255));
    const bool clicked = PaddedButton(label, minWidth);
    ImGui::PopStyleColor(3);
    return clicked;
}

static bool BlueButton(const char* label, float minWidth = 0.0f) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(77, 145, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(106, 166, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(46, 112, 220, 255));
    const bool clicked = PaddedButton(label, minWidth);
    ImGui::PopStyleColor(3);
    return clicked;
}

static void SectionHeader(const char* label) {
    ImGui::Spacing();
    ImGui::SeparatorText(label);
    ImGui::Spacing();
}

static float SidebarWidth() {
    float widest = 0.0f;
    for (const char* label : kSectionLabels)
        widest = std::max(widest, ImGui::CalcTextSize(label).x);
    const float childPaddingX = 12.0f;
    const float buttonPaddingX = 14.0f;
    return std::max(168.0f, widest + childPaddingX * 2.0f + buttonPaddingX * 2.0f + 16.0f);
}

static void PreviewText(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, const char* text) {
    const ImVec4 clip(min.x, min.y, max.x, max.y);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {min.x, min.y},
                color, text, nullptr, 0.0f, &clip);
}

static void HelpTooltip(const char* text) {
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

static std::string JoinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) {
        if (!out.empty())
            out += "\n";
        out += line;
    }
    return out;
}

static std::vector<std::string> SplitLines(const char* text) {
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

static std::string SafeFilename(std::string value) {
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

static std::filesystem::path DumpCurrentIcons() {
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

static void DrawClipboardIconAt(ImDrawList* dl, ImVec2 pos, float sz,
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

static void DrawClipboardIcon(float sz, const AppearanceSettings& ap) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy({sz, sz});
    DrawClipboardIconAt(ImGui::GetWindowDrawList(), pos, sz, ap);
}

// -- Title bar helpers ---------------------------------------------------------

// Draws a single title bar button. Returns true on click.
static bool TitleBtn(const char* id, float x, float w, float h, ImU32 baseCol, ImU32 hoverCol) {
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

// -- Section: General ---------------------------------------------------------

void MainWindow::DrawGeneral() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("General");
    ImGui::Separator();
    ImGui::Spacing();

    static bool startWithWindows = true;
    static bool deduplication    = true;

    ImGui::Checkbox("Start with Windows", &startWithWindows);
    ImGui::Spacing();
    bool newItemsAtTop = app->GetNewItemsAtTop();
    if (ImGui::Checkbox("New items added to top of list", &newItemsAtTop)) {
        app->SetNewItemsAtTop(newItemsAtTop);
    }
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    HelpTooltip("When off, new items are added to the bottom.");
    ImGui::Spacing();
    bool hidePopupOnOutsideClick = app->GetHidePopupOnOutsideClick();
    if (ImGui::Checkbox("Hide popup when clicking outside it", &hidePopupOnOutsideClick))
        app->SetHidePopupOnOutsideClick(hidePopupOnOutsideClick);
    ImGui::Spacing();
    ImGui::Checkbox("Deduplicate - move existing copy to configured position", &deduplication);

    SectionHeader("Interface");
    UiSettings ui = app->GetUiSettings();
    bool uiChanged = false;
    uiChanged |= ImGui::Checkbox("Show helper text", &ui.showHelperText);
    if (ui.showHelperText) {
        ImGui::SetNextItemWidth(180.0f);
        uiChanged |= SliderIntWheel("Helper delay (ms)", &ui.helperDelayMs, 0, 5000, "%d", 50);
        ImGui::SetNextItemWidth(180.0f);
        uiChanged |= SliderIntWheel("Helper duration (ms)", &ui.helperDurationMs, 500, 30000, "%d", 250);
    } else {
        ImGui::TextDisabled("Inline helper popups are hidden.");
    }
    if (uiChanged) {
        ui.helperDelayMs = std::clamp(ui.helperDelayMs, 0, 5000);
        ui.helperDurationMs = std::clamp(ui.helperDurationMs, 500, 30000);
        app->SetUiSettings(ui);
    }

    SectionHeader("Clipboards");

    const ClipboardProfileConfig* activeProfile = app->GetActiveClipboardProfile();
    static std::string lastProfileId;
    static char clipboardNameBuf[128]{};
    static bool clipboardDropdownOpen = false;
    enum class PendingClipboardAction { None, Rename, Create, Delete };
    static PendingClipboardAction pendingAction = PendingClipboardAction::None;
    static std::string pendingName;
    if (activeProfile && activeProfile->id != lastProfileId) {
        lastProfileId = activeProfile->id;
        strncpy_s(clipboardNameBuf, activeProfile->name.c_str(), _TRUNCATE);
    }

    auto clipboardNameExists = [&]() {
        const std::string name = TrimAscii(clipboardNameBuf);
        if (name.empty())
            return true;
        for (const ClipboardProfileConfig& profile : app->GetClipboardProfiles()) {
            if (EqualsIgnoreCase(profile.name, name))
                return true;
        }
        return false;
    };

    const bool showClipboardSave = !clipboardNameExists();
    const float saveW = ButtonWidthForText("Save", 72.0f);
    const float saveReserve = showClipboardSave ? saveW + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - saveReserve));
    ImGui::InputText("##clipboard_profile_name", clipboardNameBuf, sizeof(clipboardNameBuf));
    const bool clipboardInputHovered = ImGui::IsItemHovered();
    const bool clipboardInputClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const ImVec2 clipboardInputMin = ImGui::GetItemRectMin();
    const ImVec2 clipboardInputMax = ImGui::GetItemRectMax();
    if (clipboardInputClicked) {
        clipboardDropdownOpen = true;
        ImGui::SetKeyboardFocusHere(-1);
    }
    if (clipboardInputHovered && !ImGui::IsItemActive())
        HelpTooltip("Click to select a clipboard or type a name");
    if (showClipboardSave) {
        ImGui::SameLine();
        if (BlueButton("Save", saveW)) {
            std::string name = TrimAscii(clipboardNameBuf);
            if (!name.empty()) {
                app->CreateClipboardProfile(name);
                if (const ClipboardProfileConfig* profile = app->GetActiveClipboardProfile()) {
                    strncpy_s(clipboardNameBuf, profile->name.c_str(), _TRUNCATE);
                    lastProfileId = profile->id;
                }
                clipboardDropdownOpen = false;
            }
        }
    }

    if (clipboardDropdownOpen) {
        const float dropW = clipboardInputMax.x - clipboardInputMin.x;
        ImGui::SetNextWindowPos({clipboardInputMin.x, clipboardInputMax.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints({dropW, 0.0f}, {dropW, 300.0f});
        constexpr ImGuiWindowFlags dropFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::Begin("##clipboard_profile_picker", nullptr, dropFlags);
        const bool dropdownHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
            ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        for (const ClipboardProfileConfig& profile : app->GetClipboardProfiles()) {
            const bool selected = activeProfile && profile.id == activeProfile->id;
            std::string label = profile.name;
            if (!profile.processName.empty())
                label += " (" + profile.processName + ")";
            if (ImGui::Selectable(label.c_str(), selected)) {
                app->SetActiveClipboardProfile(profile.id);
                strncpy_s(clipboardNameBuf, profile.name.c_str(), _TRUNCATE);
                lastProfileId = profile.id;
                clipboardDropdownOpen = false;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::End();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !dropdownHovered && !clipboardInputHovered)
            clipboardDropdownOpen = false;
    }

    auto requestConfirmation = [&](PendingClipboardAction action, std::string name) {
        pendingAction = action;
        pendingName = std::move(name);
        MessageBeep(MB_ICONQUESTION);
        ImGui::OpenPopup("Confirm clipboard action");
    };

    std::string typedName = TrimAscii(clipboardNameBuf);
    ImGui::Spacing();
    if (PaddedButton("Set name", 100.0f)) {
        if (typedName.empty() && activeProfile)
            typedName = activeProfile->name;
        requestConfirmation(PendingClipboardAction::Rename, typedName);
    }
    ImGui::SameLine();
    if (PaddedButton("New clipboard", 130.0f)) {
        std::string name = typedName;
        if (name.empty())
            name = "Clipboard " + std::to_string(app->GetClipboardProfiles().size() + 1);
        requestConfirmation(PendingClipboardAction::Create, name);
    }
    ImGui::SameLine();
    if (!app->CanDeleteActiveClipboardProfile())
        ImGui::BeginDisabled();
    if (DangerButton("Delete active", 130.0f)) {
        requestConfirmation(PendingClipboardAction::Delete,
                            activeProfile ? activeProfile->name : "Clipboard");
    }
    if (!app->CanDeleteActiveClipboardProfile())
        ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("Confirm clipboard action", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* actionText = "continue";
        if (pendingAction == PendingClipboardAction::Rename)
            actionText = "rename the active clipboard";
        else if (pendingAction == PendingClipboardAction::Create)
            actionText = "create a new clipboard";
        else if (pendingAction == PendingClipboardAction::Delete)
            actionText = "delete the active clipboard";

        ImGui::TextWrapped("Confirm that you want to %s.", actionText);
        if (!pendingName.empty())
            ImGui::TextDisabled("%s", pendingName.c_str());
        ImGui::Spacing();

        if (PaddedButton("Confirm", 110.0f)) {
            if (pendingAction == PendingClipboardAction::Rename) {
                app->RenameActiveClipboardProfile(pendingName);
                strncpy_s(clipboardNameBuf, pendingName.c_str(), _TRUNCATE);
            } else if (pendingAction == PendingClipboardAction::Create) {
                app->CreateClipboardProfile(pendingName);
                strncpy_s(clipboardNameBuf, pendingName.c_str(), _TRUNCATE);
            } else if (pendingAction == PendingClipboardAction::Delete) {
                app->DeleteActiveClipboardProfile();
                if (const ClipboardProfileConfig* profile = app->GetActiveClipboardProfile())
                    strncpy_s(clipboardNameBuf, profile->name.c_str(), _TRUNCATE);
            }
            pendingAction = PendingClipboardAction::None;
            pendingName.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pendingAction = PendingClipboardAction::None;
            pendingName.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (activeProfile) {
        ImGui::TextDisabled("ID: %s", activeProfile->id.c_str());
        ImGui::TextDisabled("Created: %s", activeProfile->createdAt.c_str());
        ImGui::TextDisabled("Updated: %s", activeProfile->updatedAt.c_str());
        ImGui::TextDisabled("Bound app: %s",
                            activeProfile->processName.empty()
                                ? "(none)"
                                : activeProfile->processName.c_str());
    }

}

// -- Section: Hotkeys ---------------------------------------------------------

void MainWindow::DrawHotkeys() {
    Application* app = Application::Get();
    if (!app) return;
    HotkeyManager* hotkeys = app->GetHotkeys();
    if (!hotkeys) return;

    ImGui::TextDisabled("Hotkeys");
    ImGui::Separator();
    ImGui::Spacing();

    static HotkeySettings draft = app->GetHotkeySettings();
    static bool initialized = false;
    static int captureIndex = -1;
    static bool passthroughCaptureOpen = false;
    static bool passthroughCaptureReady = false;
    static KeyBinding passthroughPending{};
    if (!initialized) {
        draft = app->GetHotkeySettings();
        initialized = true;
    }

    KeyBinding captured;
    if (hotkeys->ConsumeCapturedBinding(captured)) {
        if (passthroughCaptureOpen) {
            passthroughPending = captured;
            passthroughCaptureReady = true;
        } else if (captureIndex >= 0 &&
                   static_cast<size_t>(captureIndex) < draft.bindings.size()) {
            captured.action = draft.bindings[static_cast<size_t>(captureIndex)].action;
            captured.data = draft.bindings[static_cast<size_t>(captureIndex)].data;
            draft.bindings[static_cast<size_t>(captureIndex)] = captured;
            app->RequestHotkeySettings(draft);
            captureIndex = -1;
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10.0f, 8.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12.0f, 6.0f});

    float widestBindingText = ButtonWidthForText("Press keys...", 180.0f);
    for (const KeyBinding& binding : draft.bindings) {
        const std::string text = HotkeyManager::BindingText(binding);
        widestBindingText = std::max(widestBindingText, ButtonWidthForText(text.c_str(), 180.0f));
    }
    const float resetButtonW = ButtonWidthForText("Reset", 84.0f);
    const float bindingColumnW = widestBindingText + resetButtonW + ImGui::GetStyle().ItemSpacing.x;

    if (ImGui::BeginTable("##hotkeys", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Binding",  ImGuiTableColumnFlags_WidthFixed, bindingColumnW);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Function");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Binding");
        for (size_t i = 0; i < draft.bindings.size(); ++i) {
            const KeyBinding& binding = draft.bindings[i];
            const bool conflict = BindingHasConflict(draft, i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(HotkeyManager::ActionName(binding.action));
            if (conflict) {
                ImGui::SameLine();
                ImGui::TextColored({1.0f, 0.45f, 0.25f, 1.0f}, "Conflict");
            }

            ImGui::TableSetColumnIndex(1);
            const std::string label = captureIndex == static_cast<int>(i)
                ? "Press keys...##capture" + std::to_string(i)
                : HotkeyManager::BindingText(binding) + "##capture" + std::to_string(i);
            if (ImGui::Button(label.c_str(), {widestBindingText, 0.0f})) {
                captureIndex = static_cast<int>(i);
                hotkeys->BeginCapture();
            }
            ImGui::SameLine();
            const std::string resetId = "Reset##resetHotkey" + std::to_string(i);
            if (ImGui::Button(resetId.c_str(), {resetButtonW, 0.0f})) {
                HotkeySettings defaults = HotkeyManager::DefaultSettings();
                auto it = std::find_if(defaults.bindings.begin(), defaults.bindings.end(),
                    [&](const KeyBinding& b) { return b.action == binding.action; });
                if (it != defaults.bindings.end()) {
                    draft.bindings[i] = *it;
                    app->RequestHotkeySettings(draft);
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);

    if (captureIndex >= 0) {
        ImGui::TextDisabled("Press Esc to cancel capture.");
        if (!hotkeys->IsCapturing())
            captureIndex = -1;
    }

    ImGui::Spacing();
    ImGui::Text("Hidden paste slots");
    bool changed = false;
    changed |= ImGui::Checkbox("Ctrl##hiddenCtrl", &draft.hiddenPasteCtrl);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Shift##hiddenShift", &draft.hiddenPasteShift);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Alt##hiddenAlt", &draft.hiddenPasteAlt);
    changed |= ImGui::Checkbox("Allow F1-F12 hidden paste slots", &draft.hiddenPasteFunctionKeys);
    ImGui::TextDisabled("%s + 1-9/a-z%s",
        HotkeyManager::ModifiersText(draft.hiddenPasteCtrl,
                                     draft.hiddenPasteShift,
                                     draft.hiddenPasteAlt).c_str(),
        draft.hiddenPasteFunctionKeys ? "/F1-F12" : "");
    if (changed)
        app->RequestHotkeySettings(draft);

    SectionHeader("Popup Pass-through Hotkeys");
    ImGui::TextDisabled("Defined hotkeys are ignored by Clipboard++ and passed to Windows.");
    if (draft.passthroughHotkeys.empty()) {
        ImGui::TextDisabled("No pass-through hotkeys defined.");
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10.0f, 6.0f});
        if (ImGui::BeginTable("##popup_passthrough_table", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Hotkey", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            for (size_t i = 0; i < draft.passthroughHotkeys.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(draft.passthroughHotkeys[i].c_str());
                ImGui::TableSetColumnIndex(1);
                const std::string removeId = "Remove##passthrough_" + std::to_string(i);
                if (DangerButton(removeId.c_str(), 84.0f)) {
                    draft.passthroughHotkeys.erase(draft.passthroughHotkeys.begin() + static_cast<std::ptrdiff_t>(i));
                    app->RequestHotkeySettings(draft);
                    break;
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    ImGui::Spacing();
    if (PaddedButton("New pass-through hotkey", 190.0f)) {
        passthroughCaptureOpen = true;
        passthroughCaptureReady = false;
        passthroughPending = {};
        hotkeys->BeginCapture();
        ImGui::OpenPopup("New pass-through hotkey");
    }
    if (!draft.passthroughHotkeys.empty()) {
        ImGui::SameLine();
    }
    if (!draft.passthroughHotkeys.empty() && PaddedButton("Reset pass-through list", 180.0f)) {
        HotkeySettings defaults = HotkeyManager::DefaultSettings();
        draft.passthroughHotkeys = defaults.passthroughHotkeys;
        app->RequestHotkeySettings(draft);
    }

    if (ImGui::BeginPopupModal("New pass-through hotkey", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Press the hotkey Clipboard++ should ignore and pass to Windows.");
        std::string preview = passthroughCaptureReady
            ? HotkeyManager::BindingText(passthroughPending)
            : hotkeys->CapturePreviewText();
        char previewBuf[128]{};
        strncpy_s(previewBuf, preview.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputText("##passthrough_capture_preview", previewBuf, sizeof(previewBuf),
                         ImGuiInputTextFlags_ReadOnly);

        if (!passthroughCaptureReady)
            ImGui::BeginDisabled();
        if (PaddedButton("Accept", 100.0f)) {
            const std::string text = HotkeyManager::BindingText(passthroughPending);
            auto exists = std::find_if(draft.passthroughHotkeys.begin(), draft.passthroughHotkeys.end(),
                [&](const std::string& existing) { return EqualsIgnoreCase(existing, text); });
            if (exists == draft.passthroughHotkeys.end()) {
                draft.passthroughHotkeys.push_back(text);
                app->RequestHotkeySettings(draft);
            }
            passthroughCaptureOpen = false;
            passthroughCaptureReady = false;
            passthroughPending = {};
            ImGui::CloseCurrentPopup();
        }
        if (!passthroughCaptureReady)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            hotkeys->CancelCapture();
            passthroughCaptureOpen = false;
            passthroughCaptureReady = false;
            passthroughPending = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (passthroughCaptureOpen && !passthroughCaptureReady) {
        hotkeys->CancelCapture();
        passthroughCaptureOpen = false;
    }

    ImGui::Spacing();
    if (PopupWindow* popup = Application::Get()->GetPopup()) {
        bool newline = app->GetAppendNewlineAfterPaste();
        if (ImGui::Checkbox("Append newline after paste", &newline))
            app->SetAppendNewlineAfterPaste(newline);

        int moveMode = 0;
        switch (app->GetPasteMoveTarget()) {
        case ClipboardHistory::MoveTarget::Top:    moveMode = 1; break;
        case ClipboardHistory::MoveTarget::Bottom: moveMode = 2; break;
        default:                                   moveMode = 0; break;
        }

        const char* modes[] = { "Keep item in place", "Move pasted item to top", "Move pasted item to bottom" };
        ImGui::Text("After paste");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::Combo("##pasteMove", &moveMode, modes, IM_ARRAYSIZE(modes))) {
            ClipboardHistory::MoveTarget target = ClipboardHistory::MoveTarget::None;
            if (moveMode == 1) target = ClipboardHistory::MoveTarget::Top;
            if (moveMode == 2) target = ClipboardHistory::MoveTarget::Bottom;
            app->SetPasteMoveTarget(target);
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Hotkey changes apply immediately. Settings persistence is next.");
}

// -- Section: Appearance ------------------------------------------------------

static bool ColorControl(const char* label, ImVec4& color) {
    ImGui::SetNextItemWidth(220.0f);
    return ImGui::ColorEdit4(label, &color.x,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
}

static bool ColorControlWithReset(const char* label, ImVec4& color, const ImVec4& resetValue) {
    bool changed = ColorControl(label, color);
    ImGui::SameLine();
    std::string id = std::string("Reset##reset_") + label;
    if (ImGui::SmallButton(id.c_str())) {
        color = resetValue;
        changed = true;
    }
    return changed;
}

static void DrawPopupPreview(const AppearanceSettings& draft) {
    AppearanceSettings preview = draft.customColors ? draft : ThemeDefaults(draft.theme);
    const PopupToggleColors toggles = GetPopupToggleColors(draft);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    ImVec2 size = {std::min(360.0f, ImGui::GetContentRegionAvail().x), 168.0f};
    ImVec2 end = {start.x + size.x, start.y + size.y};
    const float pad = 10.0f;
    const ImU32 text = ImGui::GetColorU32(preview.text);
    const ImU32 muted = ImGui::GetColorU32(preview.mutedText);

    dl->AddRectFilled(start, end, ImGui::GetColorU32(preview.windowBg), 6.0f);
    dl->AddRect(start, end, ImGui::GetColorU32(preview.accent), 6.0f, 0, 1.5f);

    ImVec2 closeA = {start.x + pad, start.y + pad};
    ImVec2 closeB = {closeA.x + 24.0f, closeA.y + 24.0f};
    dl->AddRectFilled(closeA, closeB, ImGui::GetColorU32(toggles.off), 3.0f);
    PreviewText(dl, {closeA.x + 8.0f, closeA.y + 4.0f}, {closeB.x - 4.0f, closeB.y}, text, "x");

    ImVec2 comboA = {closeB.x + 10.0f, closeA.y};
    ImVec2 comboB = {end.x - pad, closeB.y};
    dl->AddRectFilled(comboA, comboB, ImGui::GetColorU32(preview.panelBg), 3.0f);
    PreviewText(dl, {comboA.x + 8.0f, comboA.y + 4.0f}, {comboB.x - 8.0f, comboB.y}, text, "Clipboard profile");

    ImVec2 searchA = {start.x + pad, closeB.y + 10.0f};
    ImVec2 searchB = {end.x - pad, searchA.y + 24.0f};
    dl->AddRectFilled(searchA, searchB, ImGui::GetColorU32(preview.panelBg), 3.0f);
    PreviewText(dl, {searchA.x + 8.0f, searchA.y + 4.0f}, {searchB.x - 8.0f, searchB.y}, muted, "Search clipboard...");

    const char* labels[] = {"All", "Text", "Image", "URL"};
    float x = searchA.x;
    float y = searchB.y + 12.0f;
    for (int i = 0; i < IM_ARRAYSIZE(labels); ++i) {
        const float labelW = std::min(58.0f, std::max(42.0f, ImGui::CalcTextSize(labels[i]).x + 16.0f));
        ImVec2 a = {x, y};
        ImVec2 b = {std::min(x + labelW, end.x - pad), y + 22.0f};
        if (b.x <= a.x + 20.0f)
            break;
        const ImU32 col = ImGui::GetColorU32(i == 0 ? toggles.on : toggles.off);
        dl->AddRectFilled(a, b, col, 3.0f);
        PreviewText(dl, {a.x + 8.0f, a.y + 3.0f}, {b.x - 6.0f, b.y}, text, labels[i]);
        x = b.x + 6.0f;
    }

    ImVec2 rowA = {start.x + pad, y + 34.0f};
    ImVec2 rowB = {end.x - pad - 10.0f, rowA.y + 28.0f};
    dl->AddRectFilled(rowA, rowB, ImGui::GetColorU32(preview.panelBg), 3.0f);
    dl->AddCircleFilled({rowA.x + 10.0f, rowA.y + 14.0f}, 3.0f, IM_COL32(255, 196, 64, 255), 12);
    PreviewText(dl, {rowA.x + 20.0f, rowA.y + 6.0f}, {rowB.x - 8.0f, rowB.y}, text, "1  Pinned item preview");

    ImVec2 row2A = {rowA.x, rowB.y + 6.0f};
    ImVec2 row2B = {rowB.x, row2A.y + 28.0f};
    dl->AddRectFilled(row2A, row2B, ImGui::GetColorU32(preview.panelBg), 3.0f);
    PreviewText(dl, {row2A.x + 20.0f, row2A.y + 6.0f}, {row2B.x - 8.0f, row2B.y}, text, "2  History item preview");

    ImVec2 scrollA = {end.x - pad - 6.0f, searchB.y + 8.0f};
    ImVec2 scrollB = {end.x - pad, row2B.y};
    dl->AddRectFilled(scrollA, scrollB, ImGui::GetColorU32(preview.scrollbarBg), 3.0f);
    dl->AddRectFilled({scrollA.x, scrollA.y + 10.0f}, {scrollB.x, scrollA.y + 42.0f},
                      ImGui::GetColorU32(preview.scrollbarGrab), 3.0f);

    ImGui::Dummy(size);
}

static void DrawSettingsPreview(const AppearanceSettings& draft) {
    AppearanceSettings preview = draft.customColors ? draft : ThemeDefaults(draft.theme);
    const PopupToggleColors toggles = GetPopupToggleColors(draft);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();
    ImVec2 size = {std::min(360.0f, ImGui::GetContentRegionAvail().x), 148.0f};
    ImVec2 end = {start.x + size.x, start.y + size.y};
    const ImU32 text = ImGui::GetColorU32(preview.text);
    const ImU32 muted = ImGui::GetColorU32(preview.mutedText);

    dl->AddRectFilled(start, end, ImGui::GetColorU32(preview.windowBg), 0.0f);
    dl->AddRectFilled(start, {end.x, start.y + 30.0f}, ImGui::GetColorU32(preview.panelBg), 0.0f);
    PreviewText(dl, {start.x + 12.0f, start.y + 7.0f}, {end.x - 12.0f, start.y + 28.0f}, text, "Clipboard++ Settings");

    ImVec2 sideA = {start.x + 10.0f, start.y + 38.0f};
    ImVec2 sideB = {std::min(sideA.x + 104.0f, end.x - 130.0f), end.y - 12.0f};
    dl->AddRectFilled(sideA, sideB, ImGui::GetColorU32(preview.panelBg), 4.0f);
    dl->AddRectFilled({sideA.x + 8.0f, sideA.y + 10.0f}, {sideB.x - 8.0f, sideA.y + 34.0f},
                      ImGui::GetColorU32(toggles.on), 3.0f);
    PreviewText(dl, {sideA.x + 14.0f, sideA.y + 14.0f}, {sideB.x - 10.0f, sideA.y + 32.0f}, text, "Appearance");
    PreviewText(dl, {sideA.x + 14.0f, sideA.y + 44.0f}, {sideB.x - 10.0f, sideA.y + 62.0f}, muted, "General");

    ImVec2 paneA = {sideB.x + 14.0f, sideA.y};
    ImVec2 paneB = {end.x - 12.0f, sideB.y};
    dl->AddRectFilled(paneA, paneB, ImGui::GetColorU32(preview.panelBg), 4.0f);
    PreviewText(dl, {paneA.x + 12.0f, paneA.y + 12.0f}, {paneB.x - 12.0f, paneA.y + 32.0f}, text, "Theme selector");
    ImVec2 inputA = {paneA.x + 12.0f, paneA.y + 42.0f};
    ImVec2 inputB = {paneB.x - 12.0f, inputA.y + 24.0f};
    dl->AddRectFilled(inputA, inputB, ImGui::GetColorU32(preview.windowBg), 3.0f);
    PreviewText(dl, {inputA.x + 8.0f, inputA.y + 4.0f}, {inputB.x - 8.0f, inputB.y}, text, "Custom theme");
    ImVec2 buttonA = {paneA.x + 12.0f, inputB.y + 10.0f};
    ImVec2 buttonB = {std::min(buttonA.x + 96.0f, paneB.x - 12.0f), buttonA.y + 24.0f};
    dl->AddRectFilled(buttonA, buttonB, ImGui::GetColorU32(toggles.off), 3.0f);
    PreviewText(dl, {buttonA.x + 10.0f, buttonA.y + 4.0f}, {buttonB.x - 8.0f, buttonB.y}, text, "Save");
    ImGui::Dummy(size);
}

void MainWindow::DrawAppearance() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("Appearance");
    ImGui::Separator();
    ImGui::Spacing();

    static AppearanceSettings draft = app->GetAppearance();
    static char fontPath[512]{};
    static char iconPathBuf[512]{};
    static char customName[96] = "Custom";
    static bool initialized = false;
    static bool themeDropdownOpen = false;
    if (!initialized) {
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        std::snprintf(iconPathBuf, sizeof(iconPathBuf), "%s", draft.exeIconPath.c_str());
        std::snprintf(customName, sizeof(customName), "%s",
                      draft.customColors ? draft.customThemeName.c_str() : ThemeName(draft.theme));
        initialized = true;
    }

    SectionHeader("Theme");
    auto syncThemeName = [&]() {
        std::snprintf(customName, sizeof(customName), "%s",
                      draft.customColors ? draft.customThemeName.c_str() : ThemeName(draft.theme));
    };
    auto applyBuiltInTheme = [&](ThemeId theme) {
        AppearanceSettings next = ThemeDefaults(theme);
        const AppearanceSettings current = app->GetAppearance();
        next.fontPath = current.fontPath;
        next.fontSize = current.fontSize;
        next.uiScale = 1.0f;
        next.popupOpacity = current.popupOpacity;
        next.popupOutlineStrength = current.popupOutlineStrength;
        next.popupOutlineEffect = current.popupOutlineEffect;
        next.popupOutlineAnimated = current.popupOutlineAnimated;
        next.popupOutlineAnimationSpeed = current.popupOutlineAnimationSpeed;
        next.popupOutlineColorSharpness = current.popupOutlineColorSharpness;
        next.popupOutlineColorSpread = current.popupOutlineColorSpread;
        next.popupOutlineSaturation = current.popupOutlineSaturation;
        next.popupOutlineBrightness = current.popupOutlineBrightness;
        next.popupOutlineReverse = current.popupOutlineReverse;
        next.popupWidth = current.popupWidth;
        next.popupHeight = current.popupHeight;
        next.mainWindowWidth = current.mainWindowWidth;
        next.mainWindowHeight = current.mainWindowHeight;
        next.showScrollbars = current.showScrollbars;
        next.scrollbarSize = current.scrollbarSize;
        next.scrollbarRounding = current.scrollbarRounding;
        next.scrollbarPadding = current.scrollbarPadding;
        next.popupRounding = current.popupRounding;
        next.popupButtonRowPadding = current.popupButtonRowPadding;
        next.popupButtonColumnPadding = current.popupButtonColumnPadding;
        next.controlRounding = current.controlRounding;
        next.savedThemes  = current.savedThemes;
        next.exeIconPath  = current.exeIconPath;
        next.customColors = false;
        next.customThemeName = ThemeName(theme);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };
    auto applyUserTheme = [&](const SavedAppearanceTheme& saved) {
        AppearanceSettings next = app->GetAppearance();
        ApplySavedTheme(next, saved);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };
    auto saveUserTheme = [&](const std::string& requestedName) {
        AppearanceSettings next = draft;
        next.savedThemes = app->GetAppearance().savedThemes;
        next.fontPath = app->GetAppearance().fontPath;
        next.fontSize = app->GetAppearance().fontSize;
        next.uiScale = 1.0f;
        next.customColors = true;
        next.customThemeName = requestedName.empty() ? "Custom" : requestedName;
        SavedAppearanceTheme saved = ToSavedTheme(next, next.customThemeName);
        auto it = std::find_if(next.savedThemes.begin(), next.savedThemes.end(),
            [&](const SavedAppearanceTheme& existing) { return EqualsIgnoreCase(existing.name, saved.name); });
        if (it == next.savedThemes.end())
            next.savedThemes.push_back(saved);
        else
            *it = saved;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };
    auto findUserTheme = [&](const std::string& name) {
        const std::vector<SavedAppearanceTheme>& themes = app->GetAppearance().savedThemes;
        return std::find_if(themes.begin(), themes.end(),
            [&](const SavedAppearanceTheme& saved) { return EqualsIgnoreCase(saved.name, name); });
    };
    auto deleteUserTheme = [&](const std::string& name) {
        AppearanceSettings next = app->GetAppearance();
        auto it = std::remove_if(next.savedThemes.begin(), next.savedThemes.end(),
            [&](const SavedAppearanceTheme& saved) { return EqualsIgnoreCase(saved.name, name); });
        if (it == next.savedThemes.end())
            return;

        next.savedThemes.erase(it, next.savedThemes.end());
        if (next.customColors && EqualsIgnoreCase(next.customThemeName, name)) {
            AppearanceSettings fallback = ThemeDefaults(next.theme);
            fallback.fontPath = next.fontPath;
            fallback.fontSize = next.fontSize;
            fallback.uiScale = 1.0f;
            fallback.popupOpacity = next.popupOpacity;
            fallback.popupOutlineStrength = next.popupOutlineStrength;
            fallback.popupOutlineEffect = next.popupOutlineEffect;
            fallback.popupOutlineAnimated = next.popupOutlineAnimated;
            fallback.popupOutlineAnimationSpeed = next.popupOutlineAnimationSpeed;
            fallback.popupOutlineColorSharpness = next.popupOutlineColorSharpness;
            fallback.popupOutlineColorSpread = next.popupOutlineColorSpread;
            fallback.popupOutlineSaturation = next.popupOutlineSaturation;
            fallback.popupOutlineBrightness = next.popupOutlineBrightness;
            fallback.popupOutlineReverse = next.popupOutlineReverse;
            fallback.popupWidth = next.popupWidth;
            fallback.popupHeight = next.popupHeight;
            fallback.mainWindowWidth = next.mainWindowWidth;
            fallback.mainWindowHeight = next.mainWindowHeight;
            fallback.showScrollbars = next.showScrollbars;
            fallback.scrollbarSize = next.scrollbarSize;
            fallback.scrollbarRounding = next.scrollbarRounding;
            fallback.scrollbarPadding = next.scrollbarPadding;
            fallback.popupRounding = next.popupRounding;
            fallback.popupButtonRowPadding = next.popupButtonRowPadding;
            fallback.popupButtonColumnPadding = next.popupButtonColumnPadding;
            fallback.controlRounding = next.controlRounding;
            fallback.savedThemes = next.savedThemes;
            next = fallback;
        }
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        syncThemeName();
    };

    const std::string preThemeName = TrimAscii(customName);
    const std::string preSelectedThemeName = draft.customColors
        ? draft.customThemeName
        : ThemeName(draft.theme);
    const bool showThemeSave = !preThemeName.empty() &&
        !EqualsIgnoreCase(preThemeName, preSelectedThemeName);
    const float themeSaveW = ButtonWidthForText("Save", 72.0f);
    const float themeSaveReserve = showThemeSave ? themeSaveW + ImGui::GetStyle().ItemSpacing.x : 0.0f;

    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - themeSaveReserve));
    ImGui::InputText("##theme_name", customName, sizeof(customName));
    const bool themeInputHovered = ImGui::IsItemHovered();
    const bool themeInputClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const ImVec2 themeInputMin = ImGui::GetItemRectMin();
    const ImVec2 themeInputMax = ImGui::GetItemRectMax();
    if (themeInputClicked) {
        themeDropdownOpen = true;
        ImGui::SetKeyboardFocusHere(-1);
    }
    if (themeInputHovered && !ImGui::IsItemActive())
        HelpTooltip("Click to select a theme or type a custom theme name");
    if (showThemeSave) {
        ImGui::SameLine();
        if (BlueButton("Save", themeSaveW)) {
            const std::string name = TrimAscii(customName);
            if (IsBuiltInThemeName(name)) {
                MessageBeep(MB_ICONWARNING);
                ImGui::OpenPopup("Rename custom theme");
            } else {
                saveUserTheme(name);
            }
        }
    }

    if (themeDropdownOpen) {
        const float dropW = themeInputMax.x - themeInputMin.x;
        ImGui::SetNextWindowPos({themeInputMin.x, themeInputMax.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints({dropW, 0.0f}, {dropW, 320.0f});
        constexpr ImGuiWindowFlags dropFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::Begin("##theme_picker", nullptr, dropFlags);
        const bool dropdownHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
            ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        ImGui::TextDisabled("Built-in themes");
        for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
            const ThemeId theme = static_cast<ThemeId>(i);
            const bool selected = !draft.customColors && draft.theme == theme;
            if (ImGui::Selectable(ThemeName(theme), selected)) {
                applyBuiltInTheme(theme);
                themeDropdownOpen = false;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::Separator();
        ImGui::TextDisabled("User configured");
        if (draft.savedThemes.empty()) {
            ImGui::TextDisabled("No saved themes");
        } else {
            for (const SavedAppearanceTheme& saved : draft.savedThemes) {
                const bool selected = draft.customColors && saved.name == draft.customThemeName;
                if (ImGui::Selectable(saved.name.c_str(), selected)) {
                    applyUserTheme(saved);
                    themeDropdownOpen = false;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::End();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !dropdownHovered && !themeInputHovered)
            themeDropdownOpen = false;
    }

    const std::string themeNameForDelete = TrimAscii(customName);
    const bool canDeleteTheme = !themeNameForDelete.empty() &&
        findUserTheme(themeNameForDelete) != app->GetAppearance().savedThemes.end();
    ImGui::Spacing();
    if (!canDeleteTheme)
        ImGui::BeginDisabled();
    if (DangerButton("Delete theme", 122.0f)) {
        MessageBeep(MB_ICONWARNING);
        ImGui::OpenPopup("Delete custom theme");
    }
    if (!canDeleteTheme)
        ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("Delete custom theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this user theme?");
        ImGui::TextDisabled("%s", themeNameForDelete.c_str());
        ImGui::Spacing();
        if (DangerButton("Delete", 100.0f)) {
            deleteUserTheme(themeNameForDelete);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Rename custom theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char renameThemeBuf[96]{};
        if (renameThemeBuf[0] == '\0') {
            std::string base = TrimAscii(customName);
            if (base.empty() || IsBuiltInThemeName(base))
                base = std::string(base.empty() ? "Custom" : base) + " Custom";
            std::snprintf(renameThemeBuf, sizeof(renameThemeBuf), "%s", base.c_str());
        }

        ImGui::TextWrapped("Built-in theme names are reserved. Choose a custom theme name.");
        {
            const float labelW = ImGui::CalcTextSize("Name").x + ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::SetNextItemWidth(-labelW);
        }
        ImGui::InputText("Name##rename_theme", renameThemeBuf, sizeof(renameThemeBuf));

        const std::string renameName = TrimAscii(renameThemeBuf);
        if (renameName.empty() || IsBuiltInThemeName(renameName))
            ImGui::BeginDisabled();
        if (PaddedButton("Save custom theme", 150.0f)) {
            saveUserTheme(renameName);
            renameThemeBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        if (renameName.empty() || IsBuiltInThemeName(renameName))
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            renameThemeBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    SectionHeader("Popup");
    ImGui::Text("Popup opacity");
    ImGui::SetNextItemWidth(240.0f);
    SliderFloatWheel("##opacity", &draft.popupOpacity, 0.1f, 1.0f, "%.2f", 0.05f);
    ImGui::Text("Popup outline");
    ImGui::SetNextItemWidth(240.0f);
    SliderFloatWheel("##outline_strength", &draft.popupOutlineStrength, 0.0f, 1.0f, "%.2f", 0.05f);
    const char* outlineEffects[] = {"Solid accent", "Rainbow flow", "Pulse glow", "Comet chase"};
    draft.popupOutlineEffect = std::clamp(draft.popupOutlineEffect, 0, 3);
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::Combo("Outline effect", &draft.popupOutlineEffect,
                     outlineEffects, IM_ARRAYSIZE(outlineEffects))) {
        AppearanceSettings next = app->GetAppearance();
        next.popupOutlineEffect = draft.popupOutlineEffect;
        next.popupOutlineAnimated = draft.popupOutlineEffect != 0;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }
    if (draft.popupOutlineEffect != 0) {
        bool outlineAnimationChanged = false;
        auto applyOutlineAnimation = [&]() {
            AppearanceSettings next = app->GetAppearance();
            next.popupOutlineEffect = std::clamp(draft.popupOutlineEffect, 0, 3);
            next.popupOutlineAnimated = next.popupOutlineEffect != 0;
            next.popupOutlineAnimationSpeed = std::clamp(draft.popupOutlineAnimationSpeed, 0.05f, 5.0f);
            next.popupOutlineColorSharpness = std::clamp(draft.popupOutlineColorSharpness, 0.0f, 1.0f);
            next.popupOutlineColorSpread = std::clamp(draft.popupOutlineColorSpread, 0.0f, 2.0f);
            next.popupOutlineSaturation = std::clamp(draft.popupOutlineSaturation, 0.0f, 1.0f);
            next.popupOutlineBrightness = std::clamp(draft.popupOutlineBrightness, 0.20f, 1.0f);
            next.popupOutlineReverse = draft.popupOutlineReverse;
            app->RequestAppearance(next);
            draft = app->GetAppearance();
            std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
        };
        ImGui::SetNextItemWidth(240.0f);
        outlineAnimationChanged |= SliderFloatWheel("Animation speed", &draft.popupOutlineAnimationSpeed,
                                                    0.05f, 5.0f, "%.2fx", 0.05f);
        ImGui::SetNextItemWidth(240.0f);
        outlineAnimationChanged |= SliderFloatWheel("Color spread", &draft.popupOutlineColorSpread,
                                                    0.0f, 2.0f, "%.2f", 0.05f);
        ImGui::SetNextItemWidth(240.0f);
        outlineAnimationChanged |= SliderFloatWheel("Color sharpness", &draft.popupOutlineColorSharpness,
                                                    0.0f, 1.0f, "%.2f", 0.05f);
        ImGui::SetNextItemWidth(240.0f);
        outlineAnimationChanged |= SliderFloatWheel("Color saturation", &draft.popupOutlineSaturation,
                                                    0.0f, 1.0f, "%.2f", 0.05f);
        ImGui::SetNextItemWidth(240.0f);
        outlineAnimationChanged |= SliderFloatWheel("Color brightness", &draft.popupOutlineBrightness,
                                                    0.20f, 1.0f, "%.2f", 0.05f);
        if (ImGui::Checkbox("Reverse direction", &draft.popupOutlineReverse))
            outlineAnimationChanged = true;
        if (outlineAnimationChanged)
            applyOutlineAnimation();
        if (PaddedButton("Reset outline animation", 180.0f)) {
            draft.popupOutlineEffect = 1;
            draft.popupOutlineAnimationSpeed = 1.0f;
            draft.popupOutlineColorSpread = 1.0f;
            draft.popupOutlineColorSharpness = 0.55f;
            draft.popupOutlineSaturation = 0.72f;
            draft.popupOutlineBrightness = 1.0f;
            draft.popupOutlineReverse = false;
            applyOutlineAnimation();
        }
    }

    SectionHeader("Default Popup Size");
    bool sizeChanged = false;
    ImGui::SetNextItemWidth(IntInputWidth(9999));
    sizeChanged |= ImGui::InputInt("W##pw", &draft.popupWidth, 10);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(IntInputWidth(9999));
    sizeChanged |= ImGui::InputInt("H##ph", &draft.popupHeight, 10);
    if (sizeChanged) {
        draft.popupWidth = std::max(360, draft.popupWidth);
        draft.popupHeight = std::max(260, draft.popupHeight);
    }
    SIZE livePopupSize = app->PopupCurrentSize();
    ImGui::TextDisabled("Current popup: %ldx%ld",
                        static_cast<long>(livePopupSize.cx),
                        static_cast<long>(livePopupSize.cy));
    ImGui::Spacing();
    if (PopupWindow* pw = app->GetPopup()) {
        SectionHeader("Focus Behavior (Test)");
        bool focusTest = pw->m_focusTestMode;
        if (ImGui::Checkbox("Restore focus to caller on show", &focusTest))
            pw->m_focusTestMode = focusTest;
        HelpTooltip("Removes WS_EX_NOACTIVATE so the popup activates normally,\nthen immediately returns foreground focus to the window\nthat was active when the popup was opened.\nTakes effect on the next popup show.");
    }
    ImGui::Spacing();
    if (PaddedButton("Use current as default", 180.0f)) {
        app->UseCurrentPopupSizeAsDefault();
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }
    ImGui::SameLine();
    if (PaddedButton("Apply popup settings", 170.0f)) {
        AppearanceSettings next = app->GetAppearance();
        next.popupOpacity = std::clamp(draft.popupOpacity, 0.1f, 1.0f);
        next.popupOutlineStrength = std::clamp(draft.popupOutlineStrength, 0.0f, 1.0f);
        next.popupOutlineEffect = std::clamp(draft.popupOutlineEffect, 0, 3);
        next.popupOutlineAnimated = next.popupOutlineEffect != 0;
        next.popupOutlineAnimationSpeed = std::clamp(draft.popupOutlineAnimationSpeed, 0.05f, 5.0f);
        next.popupOutlineColorSharpness = std::clamp(draft.popupOutlineColorSharpness, 0.0f, 1.0f);
        next.popupOutlineColorSpread = std::clamp(draft.popupOutlineColorSpread, 0.0f, 2.0f);
        next.popupOutlineSaturation = std::clamp(draft.popupOutlineSaturation, 0.0f, 1.0f);
        next.popupOutlineBrightness = std::clamp(draft.popupOutlineBrightness, 0.20f, 1.0f);
        next.popupOutlineReverse = draft.popupOutlineReverse;
        next.popupWidth = std::max(360, draft.popupWidth);
        next.popupHeight = std::max(260, draft.popupHeight);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }

    SectionHeader("Default Settings Window Size");
    bool mainSizeChanged = false;
    ImGui::SetNextItemWidth(IntInputWidth(9999));
    mainSizeChanged |= ImGui::InputInt("W##mw", &draft.mainWindowWidth, 10);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(IntInputWidth(9999));
    mainSizeChanged |= ImGui::InputInt("H##mh", &draft.mainWindowHeight, 10);
    if (mainSizeChanged) {
        draft.mainWindowWidth = std::max(800, draft.mainWindowWidth);
        draft.mainWindowHeight = std::max(500, draft.mainWindowHeight);
    }
    SIZE liveMainSize = app->MainWindowCurrentSize();
    ImGui::TextDisabled("Current settings: %ldx%ld",
                        static_cast<long>(liveMainSize.cx),
                        static_cast<long>(liveMainSize.cy));
    ImGui::Spacing();
    if (PaddedButton("Use current as default##main_size", 180.0f)) {
        app->UseCurrentMainWindowSizeAsDefault();
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }
    ImGui::SameLine();
    if (PaddedButton("Apply settings size", 160.0f)) {
        AppearanceSettings next = app->GetAppearance();
        next.mainWindowWidth = std::max(800, draft.mainWindowWidth);
        next.mainWindowHeight = std::max(500, draft.mainWindowHeight);
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }

    SectionHeader("Font");
    auto applyFont = [&]() {
        AppearanceSettings next = app->GetAppearance();
        next.fontPath = fontPath;
        next.fontSize = std::clamp(std::round(draft.fontSize * 2.0f) * 0.5f, 9.0f, 32.0f);
        {
            std::ostringstream out;
            out << "MainWindow::ApplyFont: requested"
                << " path=\"" << next.fontPath << "\""
                << " size=" << next.fontSize
                << " draftSize=" << draft.fontSize;
            app->LogDebug(out.str());
        }
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    };

    ImGui::SetNextItemWidth(-FLT_MIN);
    bool fontPathEdited = ImGui::InputText("##fontPath", fontPath, sizeof(fontPath),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    bool fontPathDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Spacing();
    int fontSizeStep = static_cast<int>(std::round(draft.fontSize * 2.0f));
    if (PaddedButton("Browse", 100.0f)) {
        if (PickFontFile(fontPath, sizeof(fontPath)))
            applyFont();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240.0f);
    bool fontSizeChanged = SliderIntWheel("Font size##fontSizeSlider", &fontSizeStep,
                                          18, 64, "", 1);
    ImGui::SameLine();
    ImGui::Text("%.1f", static_cast<float>(fontSizeStep) * 0.5f);
    if (fontPathEdited)
        applyFont();
    else if (fontPathDeactivated) {
        draft.fontPath = fontPath;
    }

    if (fontSizeChanged) {
        fontSizeStep = std::clamp(fontSizeStep, 18, 64);
        const float snapped = static_cast<float>(fontSizeStep) * 0.5f;
        if (std::fabs(snapped - draft.fontSize) > 0.01f) {
            draft.fontSize = snapped;
            applyFont();
        }
    }

    SectionHeader("Exe Icon");
    ImGui::TextDisabled("Choose a .ico file to use as the Windows Explorer icon for this exe.");
    ImGui::TextColored({1.0f, 0.36f, 0.32f, 1.0f},
                       "Restart Clipboard++ after selecting a custom exe icon.");
    auto applyIconPath = [&]() {
        AppearanceSettings next = app->GetAppearance();
        next.exeIconPath = iconPathBuf;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        std::snprintf(iconPathBuf, sizeof(iconPathBuf), "%s", draft.exeIconPath.c_str());
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    };
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool iconEdited      = ImGui::InputText("##exeIconPath", iconPathBuf, sizeof(iconPathBuf),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    bool iconDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Spacing();
    if (PaddedButton("Browse##icon", 100.0f)) {
        if (PickIcoFile(iconPathBuf, sizeof(iconPathBuf)))
            applyIconPath();
    }
    ImGui::SameLine();
    if (PaddedButton("Clear##icon", 90.0f)) {
        iconPathBuf[0] = '\0';
        applyIconPath();
    }
    if (BlueButton("Restart Clipboard++", 160.0f)) {
        ShellExecuteA(nullptr, "open", app->ExecutablePath().c_str(), nullptr,
                      app->WorkingDirectory().c_str(), SW_SHOWNORMAL);
        DestroyWindow(app->GetHwnd());
    }
    if (iconEdited)
        applyIconPath();
    else if (iconDeactivated)
        draft.exeIconPath = iconPathBuf;

    SectionHeader("Scrollbars");
    bool scrollbarChanged = false;
    scrollbarChanged |= ImGui::Checkbox("Show scrollbars", &draft.showScrollbars);
    if (draft.showScrollbars) {
        ImGui::SetNextItemWidth(240.0f);
        scrollbarChanged |= SliderFloatWheel("Scrollbar width", &draft.scrollbarSize,
                                             0.0f, 24.0f, "%.1f", 1.0f);
        ImGui::SetNextItemWidth(240.0f);
        scrollbarChanged |= SliderFloatWheel("Scrollbar rounding", &draft.scrollbarRounding,
                                             0.0f, 16.0f, "%.1f", 1.0f);
        ImGui::SetNextItemWidth(240.0f);
        scrollbarChanged |= SliderFloatWheel("Scrollbar padding", &draft.scrollbarPadding,
                                             0.0f, 8.0f, "%.1f", 0.5f);
        if (draft.customColors) {
            const AppearanceSettings resetForScrollbars = ThemeDefaults(draft.theme);
            scrollbarChanged |= ColorControlWithReset("Scrollbar background", draft.scrollbarBg, resetForScrollbars.scrollbarBg);
            scrollbarChanged |= ColorControlWithReset("Scrollbar grab", draft.scrollbarGrab, resetForScrollbars.scrollbarGrab);
            scrollbarChanged |= ColorControlWithReset("Scrollbar hover", draft.scrollbarGrabHover, resetForScrollbars.scrollbarGrabHover);
            scrollbarChanged |= ColorControlWithReset("Scrollbar active", draft.scrollbarGrabActive, resetForScrollbars.scrollbarGrabActive);
        }
    } else {
        ImGui::TextDisabled("Scrollbar controls are hidden while scrollbars are disabled.");
    }
    if (scrollbarChanged) {
        draft.scrollbarSize = std::clamp(draft.scrollbarSize, 0.0f, 24.0f);
        draft.scrollbarRounding = std::clamp(draft.scrollbarRounding, 0.0f, 16.0f);
        draft.scrollbarPadding = std::clamp(draft.scrollbarPadding, 0.0f, 8.0f);
        AppearanceSettings next = app->GetAppearance();
        next.showScrollbars = draft.showScrollbars;
        next.scrollbarSize = draft.scrollbarSize;
        next.scrollbarRounding = draft.scrollbarRounding;
        next.scrollbarPadding = draft.scrollbarPadding;
        next.scrollbarBg = draft.scrollbarBg;
        next.scrollbarGrab = draft.scrollbarGrab;
        next.scrollbarGrabHover = draft.scrollbarGrabHover;
        next.scrollbarGrabActive = draft.scrollbarGrabActive;
        if (draft.customColors)
            next.customColors = true;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        syncThemeName();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }

    SectionHeader("Colors");
    const AppearanceSettings reset = ThemeDefaults(draft.theme);
    bool colorsChanged = ImGui::Checkbox("Enable custom colors", &draft.customColors);
    if (!draft.customColors) {
        ImGui::TextDisabled("Color controls are hidden while custom colors are disabled.");
    } else if (ImGui::BeginTable("##appearance_color_preview", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Colors", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        const ImGuiTreeNodeFlags groupFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (ImGui::TreeNodeEx("Base surfaces and text", groupFlags)) {
            colorsChanged |= ColorControlWithReset("Window background", draft.windowBg, reset.windowBg);
            colorsChanged |= ColorControlWithReset("Panel background", draft.panelBg, reset.panelBg);
            colorsChanged |= ColorControlWithReset("Text", draft.text, reset.text);
            colorsChanged |= ColorControlWithReset("Muted text", draft.mutedText, reset.mutedText);
            colorsChanged |= ColorControlWithReset("Accent", draft.accent, reset.accent);
            colorsChanged |= ColorControlWithReset("Hover", draft.hover, reset.hover);
            colorsChanged |= ColorControlWithReset("Selected tab", draft.selectedTab, reset.selectedTab);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Popup controls", groupFlags)) {
            colorsChanged |= ColorControlWithReset("Button off", draft.buttonOff, reset.buttonOff);
            colorsChanged |= ColorControlWithReset("Button on", draft.buttonOn, reset.buttonOn);
            colorsChanged |= ColorControlWithReset("Popup close", draft.closeButton, reset.closeButton);
            colorsChanged |= ColorControlWithReset("Popup close hover", draft.closeButtonHover, reset.closeButtonHover);
            colorsChanged |= ColorControlWithReset("Popup close text", draft.closeButtonText, reset.closeButtonText);
            colorsChanged |= ColorControlWithReset("Opacity knob fill", draft.opacityKnobFill, reset.opacityKnobFill);
            colorsChanged |= ColorControlWithReset("Opacity knob ring", draft.opacityKnobRing, reset.opacityKnobRing);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Settings title bar", groupFlags)) {
            colorsChanged |= ColorControlWithReset("Title bar background", draft.titleBarBg, reset.titleBarBg);
            colorsChanged |= ColorControlWithReset("Title bar divider", draft.titleBarBorder, reset.titleBarBorder);
            colorsChanged |= ColorControlWithReset("Title text", draft.titleBarText, reset.titleBarText);
            colorsChanged |= ColorControlWithReset("Minimize base",  draft.titleMinBase,   reset.titleMinBase);
            colorsChanged |= ColorControlWithReset("Minimize hover", draft.titleMinHover,  reset.titleMinHover);
            colorsChanged |= ColorControlWithReset("Minimize glyph", draft.titleMinGlyph,  reset.titleMinGlyph);
            colorsChanged |= ColorControlWithReset("Restore base",   draft.titleMaxBase,   reset.titleMaxBase);
            colorsChanged |= ColorControlWithReset("Restore hover",  draft.titleMaxHover,  reset.titleMaxHover);
            colorsChanged |= ColorControlWithReset("Restore glyph",  draft.titleMaxGlyph,  reset.titleMaxGlyph);
            colorsChanged |= ColorControlWithReset("Close base",     draft.titleCloseBase,  reset.titleCloseBase);
            colorsChanged |= ColorControlWithReset("Close hover",    draft.titleCloseHover, reset.titleCloseHover);
            colorsChanged |= ColorControlWithReset("Close glyph",    draft.titleCloseGlyph, reset.titleCloseGlyph);
            colorsChanged |= ColorControlWithReset("Exit base",      draft.titleExitBase,   reset.titleExitBase);
            colorsChanged |= ColorControlWithReset("Exit hover",     draft.titleExitHover,  reset.titleExitHover);
            colorsChanged |= ColorControlWithReset("Exit glyph",     draft.titleExitGlyph,  reset.titleExitGlyph);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Clipboard icon", groupFlags)) {
            const float previewSz = 48.0f;
            ImGui::BeginGroup();
            colorsChanged |= ColorControlWithReset("Board top",    draft.iconBoardTop,    reset.iconBoardTop);
            colorsChanged |= ColorControlWithReset("Board bottom", draft.iconBoardBottom, reset.iconBoardBottom);
            colorsChanged |= ColorControlWithReset("Paper",        draft.iconPaper,       reset.iconPaper);
            colorsChanged |= ColorControlWithReset("Margin line",  draft.iconMarginLine,  reset.iconMarginLine);
            colorsChanged |= ColorControlWithReset("Ruled lines",  draft.iconRuledLines,  reset.iconRuledLines);
            ImGui::EndGroup();
            ImGui::SameLine(0, 16.0f);
            DrawClipboardIcon(previewSz, draft);
            ImGui::TreePop();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("Preview");
        DrawPopupPreview(draft);
        ImGui::Spacing();
        DrawSettingsPreview(draft);
        ImGui::EndTable();
    }
    if (colorsChanged) {
        AppearanceSettings next = draft;
        next.fontPath = app->GetAppearance().fontPath;
        next.fontSize = app->GetAppearance().fontSize;
        next.uiScale = 1.0f;
        next.savedThemes = app->GetAppearance().savedThemes;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        syncThemeName();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }

    ImGui::Spacing(); ImGui::SeparatorText("Advanced shape");
    ImGui::SetNextItemWidth(240.0f);
    bool shapeChanged = SliderFloatWheel("Popup corner rounding", &draft.popupRounding,
                                         0.0f, 18.0f, "%.1f", 0.5f);
    ImGui::SetNextItemWidth(240.0f);
    shapeChanged |= SliderFloatWheel("Control rounding", &draft.controlRounding,
                                     0.0f, 12.0f, "%.1f", 0.5f);
    if (shapeChanged) {
        draft.popupRounding = std::clamp(draft.popupRounding, 0.0f, 18.0f);
        draft.popupButtonRowPadding = std::clamp(draft.popupButtonRowPadding, 0.0f, 12.0f);
        draft.popupButtonColumnPadding = std::clamp(draft.popupButtonColumnPadding, 0.0f, 16.0f);
        draft.controlRounding = std::clamp(draft.controlRounding, 0.0f, 12.0f);
        AppearanceSettings next = app->GetAppearance();
        next.popupRounding = draft.popupRounding;
        next.popupButtonRowPadding = draft.popupButtonRowPadding;
        next.popupButtonColumnPadding = draft.popupButtonColumnPadding;
        next.controlRounding = draft.controlRounding;
        app->RequestAppearance(next);
        draft = app->GetAppearance();
        syncThemeName();
        std::snprintf(fontPath, sizeof(fontPath), "%s", draft.fontPath.c_str());
    }
}

// -- Section: History ---------------------------------------------------------

void MainWindow::DrawHistory() {
    ImGui::TextDisabled("History");
    ImGui::Separator();
    ImGui::Spacing();

    static int  activeLimit    = kMaxClipboardHistoryItems;
    static bool persistHistory = true;
    static bool sessionOnly    = false;
    static bool vaultUnlimited = true;
    static int  vaultLimitMB   = 0;

    ImGui::Text("Active history size (items)");
    ImGui::SetNextItemWidth(120.0f);
    SliderIntWheel("##active", &activeLimit, 1, kMaxClipboardHistoryItems, "%d", 1);

    ImGui::Spacing();
    ImGui::Checkbox("Persist history across sessions", &persistHistory);
    if (persistHistory) {
        ImGui::Indent();
        ImGui::Checkbox("Session only (clear on exit)", &sessionOnly);
        ImGui::Unindent();
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("Vault (overflow archive)");
    ImGui::Checkbox("Unlimited vault size", &vaultUnlimited);
    if (!vaultUnlimited) {
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Max vault size (MB)", &vaultLimitMB);
        if (vaultLimitMB < 1) vaultLimitMB = 1;
    }
    // -- Live history preview --------------------------------------------------
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ClipboardHistory* hist = Application::Get()->GetHistory();
    size_t count = hist ? hist->Size() : 0;
    ImGui::Text("Live history  (%zu / %d items)", count, activeLimit);
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
        ImGuiWindowFlags childFlags = Application::Get()->GetAppearance().showScrollbars
            ? ImGuiWindowFlags_None
            : ImGuiWindowFlags_NoScrollbar;
        childFlags |= ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::BeginChild("##histlive", {-1.0f, 220.0f},
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                              childFlags)) {
        if (count == 0) {
            ImGui::TextDisabled("  Nothing captured yet - copy something!");
        } else {
            for (size_t i = 0; i < count; ++i) {
                const ClipboardItem* item = hist->Get(i);
                if (!item) break;

                // Slot label: 1-9 then a-z; items beyond slot 'z' show no label
                char slot[4]{};
                if (i < 9)       slot[0] = (char)('1' + (int)i);
                else if (i < 35) slot[0] = (char)('a' + (int)(i - 9));
                ImGui::TextDisabled(" %s ", slot);
                ImGui::SameLine();

                // Inline tag badges
                for (ContentTag t : kDisplayTagOrder) {
                    if (!(item->tags & t)) continue;
                    ImVec4 col = (item->tags & TAG_SECRET)
                        ? ImVec4(1.f, 0.34f, 0.34f, 1.f)
                        : ImVec4(0.4f, 0.7f, 1.0f, 1.f);
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::Text("[%s]", ContentDetector::TagName(t));
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                }

#ifndef NDEBUG
                const DeveloperSettings& dev = Application::Get()->GetDeveloperSettings();
                if (dev.enabled && dev.showSourceProcess && !item->sourceProcess.empty()) {
                    ImGui::TextDisabled("{%s}", item->sourceProcess.c_str());
                    ImGui::SameLine();
                }
#endif

                ImGui::TextUnformatted(item->Preview(72).c_str());
            }
        }
        SmoothScrollCurrentWindow("history_live", 72.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// -- Section: Filters ---------------------------------------------------------

void MainWindow::DrawFilters() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("Filters");
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<CustomFilter> filters = app->GetCustomFilters();
    static std::string selectedId;
    static CustomFilter draft;
    static char nameBuf[96]{};
    static char patternBuf[512]{};
    static char testBuf[512]{};

    auto findFilter = [&](const std::string& id) {
        return std::find_if(filters.begin(), filters.end(),
            [&](const CustomFilter& filter) { return filter.id == id; });
    };
    auto loadDraft = [&](const CustomFilter& filter) {
        draft = filter;
        selectedId = filter.id;
        strncpy_s(nameBuf, filter.name.c_str(), _TRUNCATE);
        strncpy_s(patternBuf, filter.pattern.c_str(), _TRUNCATE);
    };
    auto saveFilters = [&]() {
        app->SetCustomFilters(filters);
    };

    if (selectedId.empty() && !filters.empty())
        loadDraft(filters.front());
    if (!selectedId.empty() && findFilter(selectedId) == filters.end()) {
        selectedId.clear();
        if (!filters.empty())
            loadDraft(filters.front());
    }

    SectionHeader("Filter Buttons");
    if (filters.empty()) {
        ImGui::TextDisabled("No custom filters yet.");
    } else {
        ImGui::TextDisabled("Drag filters to reorder popup buttons.");
        ImGui::Spacing();
        for (int i = 0; i < static_cast<int>(filters.size()); ++i) {
            ImGui::PushID(filters[i].id.c_str());
            const bool selected = filters[i].id == selectedId;
            std::string label = filters[i].name;
            if (!filters[i].enabled)
                label += " (off)";
            if (ImGui::Selectable(label.c_str(), selected))
                loadDraft(filters[i]);

            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("CPP_CUSTOM_FILTER", &i, sizeof(i));
                ImGui::TextUnformatted(filters[i].name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CPP_CUSTOM_FILTER")) {
                    int from = *static_cast<const int*>(payload->Data);
                    if (from >= 0 && from < static_cast<int>(filters.size()) && from != i) {
                        CustomFilter moved = filters[static_cast<size_t>(from)];
                        filters.erase(filters.begin() + from);
                        const int to = from < i ? i - 1 : i;
                        filters.insert(filters.begin() + to, std::move(moved));
                        saveFilters();
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }
    }

    SectionHeader("Editor");
    if (BlueButton("New filter", 120.0f)) {
        CustomFilter filter;
        filter.id = NewCustomFilterId();
        filter.name = "New filter";
        filter.pattern = "text";
        filters.push_back(filter);
        saveFilters();
        loadDraft(filters.back());
    }

    if (selectedId.empty()) {
        ImGui::TextDisabled("Create a filter to edit its button and matching rule.");
        return;
    }

    draft.name = TrimAscii(nameBuf);
    draft.pattern = patternBuf;

    ImGui::Checkbox("Enabled", &draft.enabled);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Name", nameBuf, sizeof(nameBuf));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("Pattern", patternBuf, sizeof(patternBuf), {0.0f, 88.0f});
    draft.name = TrimAscii(nameBuf);
    draft.pattern = patternBuf;

    int mode = static_cast<int>(draft.mode);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Mode", CustomFilterModeName(draft.mode))) {
        for (int i = 0; i <= 3; ++i) {
            const auto value = static_cast<CustomFilterMode>(i);
            if (ImGui::Selectable(CustomFilterModeName(value), mode == i))
                mode = i;
        }
        ImGui::EndCombo();
    }
    draft.mode = static_cast<CustomFilterMode>(std::clamp(mode, 0, 3));

    int target = static_cast<int>(draft.target);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Target", CustomFilterTargetName(draft.target))) {
        for (int i = 0; i <= 4; ++i) {
            const auto value = static_cast<CustomFilterTarget>(i);
            if (ImGui::Selectable(CustomFilterTargetName(value), target == i))
                target = i;
        }
        ImGui::EndCombo();
    }
    draft.target = static_cast<CustomFilterTarget>(std::clamp(target, 0, 4));

    ImGui::Checkbox("Case sensitive", &draft.caseSensitive);
    if (draft.mode == CustomFilterMode::Regex) {
        ImGui::Checkbox("Multiline", &draft.multiline);
        ImGui::Checkbox("Dot matches newline", &draft.dotMatchesNewline);
    }

    SectionHeader("Routing");
    ImGui::Checkbox("Route matching copies to another clipboard", &draft.routeToProfile);
    if (draft.routeToProfile) {
        const std::vector<ClipboardProfileConfig>& profiles = app->GetClipboardProfiles();
        const char* selectedProfileName = "(select profile)";
        for (const ClipboardProfileConfig& profile : profiles) {
            if (profile.id == draft.routeProfileId) {
                selectedProfileName = profile.name.c_str();
                break;
            }
        }
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::BeginCombo("Destination profile", selectedProfileName)) {
            for (const ClipboardProfileConfig& profile : profiles) {
                const bool selected = profile.id == draft.routeProfileId;
                if (ImGui::Selectable(profile.name.c_str(), selected))
                    draft.routeProfileId = profile.id;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Move instead of copy", &draft.routeMove);
        ImGui::TextDisabled("Copy keeps the item in the active clipboard and also adds it to the destination.");
    }

    CustomFilterValidation validation = ValidateCustomFilter(draft);
    const bool routingOk = !draft.routeToProfile || !draft.routeProfileId.empty();
    if (validation.ok)
        ImGui::TextDisabled("Pattern is valid.");
    else
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", validation.message.c_str());
    if (!routingOk)
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Choose a destination profile for routing.");

    SectionHeader("Test");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextMultiline("Sample text", testBuf, sizeof(testBuf), {0.0f, 72.0f});
    ClipboardItem sample;
    sample.type = ContentType::Text;
    sample.text = testBuf;
    sample.tags = ContentDetector::DetectTags(sample.text);
    const bool sampleMatches = validation.ok && CustomFilterMatches(draft, sample);
    ImGui::TextDisabled("Result: %s", sampleMatches ? "match" : "no match");

    SectionHeader("Actions");
    if (!validation.ok || !routingOk)
        ImGui::BeginDisabled();
    if (BlueButton("Save filter", 120.0f)) {
        draft.name = TrimAscii(nameBuf);
        draft.pattern = patternBuf;
        auto it = findFilter(selectedId);
        if (it != filters.end()) {
            *it = draft;
            saveFilters();
            loadDraft(*it);
        }
    }
    if (!validation.ok || !routingOk)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (DangerButton("Delete filter", 120.0f))
        ImGui::OpenPopup("Confirm filter delete");

    if (ImGui::BeginPopupModal("Confirm filter delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Delete this custom filter?");
        ImGui::TextDisabled("%s", draft.name.c_str());
        ImGui::Spacing();
        if (DangerButton("Delete", 90.0f)) {
            filters.erase(std::remove_if(filters.begin(), filters.end(),
                [&](const CustomFilter& filter) { return filter.id == selectedId; }),
                filters.end());
            selectedId.clear();
            saveFilters();
            if (!filters.empty())
                loadDraft(filters.front());
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (PaddedButton("Cancel", 90.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// -- Section: Editor ----------------------------------------------------------

void MainWindow::DrawEditor() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("Text / Script Editor");
    ImGui::Separator();
    ImGui::Spacing();

    EditorSettings s = app->GetEditorSettings();
    bool changed = false;

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

    if (changed)
        app->SetEditorSettings(s);
}

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

// -- Section: Developer -------------------------------------------------------

#ifndef NDEBUG
void MainWindow::DrawDeveloper() {
    Application* app = Application::Get();
    if (!app) return;

    ImGui::TextDisabled("Developer Mode");
    ImGui::Separator();
    ImGui::Spacing();

    DeveloperSettings dev = app->GetDeveloperSettings();
    bool changed = false;

    changed |= ImGui::Checkbox("Enable Developer Mode", &dev.enabled);
    if (changed)
        app->SetDeveloperSettings(dev);

    if (!dev.enabled) {
        ImGui::Spacing();
        ImGui::TextDisabled("Developer tools are hidden until Developer Mode is enabled.");
        return;
    }

    ImGui::Spacing();
    changed |= ImGui::Checkbox("Enable CLI interface", &dev.cliEnabled);
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    HelpTooltip("When off, runtime CLI commands are blocked. Help, status, config, and --set still work.");
    changed |= ImGui::Checkbox("Show source process metadata", &dev.showSourceProcess);
    changed |= ImGui::Checkbox("Enable developer event log", &dev.eventLogEnabled);

    if (changed)
        app->SetDeveloperSettings(dev);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Advanced Clipboard Routing");

    if (ImGui::SmallButton("Bind active clipboard to focused app"))
        app->BindActiveClipboardToForegroundProcess();

    bool autoSwitch = app->GetAutoSwitchClipboardByProcess();
    if (ImGui::Checkbox("Auto-switch clipboard by focused app", &autoSwitch))
        app->SetAutoSwitchClipboardByProcess(autoSwitch);

    bool autoCreate = app->GetAutoCreateClipboardByProcess();
    if (ImGui::Checkbox("Auto-create clipboard for focused app", &autoCreate))
        app->SetAutoCreateClipboardByProcess(autoCreate);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Runtime Diagnostics");
    const ClipboardProfileConfig* active = app->GetActiveClipboardProfile();
    ClipboardHistory* hist = app->GetHistory();
    const SIZE livePopupSize = app->PopupCurrentSize();
    ImGui::Text("Process ID: %lu", static_cast<unsigned long>(app->ProcessId()));
    ImGui::TextWrapped("Executable: %s    Popup: %ldx%ld",
                       app->ExecutablePath().c_str(),
                       static_cast<long>(livePopupSize.cx),
                       static_cast<long>(livePopupSize.cy));
    ImGui::TextWrapped("Working directory: %s", app->WorkingDirectory().c_str());
    ImGui::Text("Foreground process: %s", app->ForegroundProcessName().c_str());
    ImGui::Text("Active clipboard: %s", active ? active->name.c_str() : "(none)");
    ImGui::Text("Clipboard ID: %s", active ? active->id.c_str() : "(none)");
    ImGui::Text("Bound process: %s",
                active && !active->processName.empty() ? active->processName.c_str() : "(none)");
    ImGui::Text("History items: %zu", hist ? hist->Size() : 0);
    ImGui::Text("Pinned items: %zu", hist ? hist->PinnedSize() : 0);
    ImGui::Text("Config: %s", ConfigStore::Path().string().c_str());
    ImGui::Text("Fonts: %s", ConfigStore::FontsDirectory().string().c_str());
    if (ImGui::SmallButton("Toggle debug output"))
        app->ToggleDebugWindow();
    ImGui::SameLine();
    static std::filesystem::path lastIconDumpPath;
    if (ImGui::SmallButton("Dump current icons")) {
        lastIconDumpPath = DumpCurrentIcons();
        if (!lastIconDumpPath.empty())
            ShellExecuteW(nullptr, L"open", lastIconDumpPath.wstring().c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (!lastIconDumpPath.empty()) {
        ImGui::TextWrapped("Icon dump: %s", lastIconDumpPath.string().c_str());
    }

    if (dev.enabled && hist && hist->Size() > 0) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextDisabled("Clipboard Item Inspector");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
        ImGuiWindowFlags childFlags = app->GetAppearance().showScrollbars
            ? ImGuiWindowFlags_None
            : ImGuiWindowFlags_NoScrollbar;
        childFlags |= ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::BeginChild("##dev_item_inspector", {-1.0f, 210.0f},
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                              childFlags)) {
            const size_t limit = std::min<size_t>(hist->Size(), 25);
            for (size_t i = 0; i < limit; ++i) {
                const ClipboardItem* item = hist->Get(i);
                if (!item) continue;
                ImGui::PushID(static_cast<int>(i));
                const std::string label = std::to_string(i + 1) + "  " + item->Preview(64);
                if (ImGui::TreeNode(label.c_str())) {
                    ImGui::Text("ID: %llu", static_cast<unsigned long long>(item->id));
                    ImGui::Text("Hash: %llu", static_cast<unsigned long long>(item->contentHash));
                    ImGui::Text("Type: %s", ContentTypeName(item->type));
                    ImGui::TextWrapped("Tags: %s", TagList(item->tags).c_str());
                    ImGui::Text("Pinned: %s", item->pinned ? "yes" : "no");
                    ImGui::Text("Source: %s",
                                item->sourceProcess.empty() ? "(unknown)" : item->sourceProcess.c_str());
                    ImGui::Text("Captured: %s", TimeLabel(item->timestamp).c_str());
                    ImGui::Text("Created: %s", TimeLabel(item->createdAt).c_str());
                    ImGui::Text("Updated: %s", TimeLabel(item->updatedAt).c_str());
                    ImGui::Text("Last used: %s", TimeLabel(item->lastUsedAt).c_str());
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            SmoothScrollCurrentWindow("dev_item_inspector", 72.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    if (dev.enabled) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextDisabled("Developer Event Log");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy log")) {
            std::string text;
            for (const std::string& line : app->GetDeveloperEvents()) {
                text += line;
                text += "\n";
            }
            ImGui::SetClipboardText(text.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear log"))
            app->ClearDeveloperEvents();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
        ImGuiWindowFlags childFlags = app->GetAppearance().showScrollbars
            ? ImGuiWindowFlags_None
            : ImGuiWindowFlags_NoScrollbar;
        childFlags |= ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::BeginChild("##dev_event_log", {-1.0f, 180.0f},
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                              childFlags)) {
            const auto& events = app->GetDeveloperEvents();
            if (!dev.eventLogEnabled) {
                ImGui::TextDisabled("  Enable developer event log to collect new events.");
            } else if (events.empty()) {
                ImGui::TextDisabled("  No developer events yet.");
            } else {
                for (const std::string& line : events)
                    ImGui::Selectable(line.c_str(), false);
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
            }
            SmoothScrollCurrentWindow("dev_event_log", 72.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::Spacing();
        ImGui::TextDisabled("Still planned: regex transforms, named slots, raw bytes viewer, pretty-print, exports.");
    }
}
#endif

// -- Section: About -----------------------------------------------------------

void MainWindow::DrawAbout() {
    ImGui::TextDisabled("About");
    ImGui::Separator();
    ImGui::Spacing();

    // -- Icon + app identity --------------------------------------------------
    Application* app = Application::Get();
    float iconSz = S(64.0f);
    if (app) {
        DrawClipboardIcon(iconSz, app->GetAppearance());
        ImGui::SameLine(0, S(16.0f));
    }
    ImGui::BeginGroup();
    ImGui::Text("Clipboard++");
    ImGui::TextDisabled("Version 0.1.0  (Beta 4)");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "A lean, modern Windows clipboard manager built with\n"
        "C++17, Dear ImGui (docking branch), and DirectX 11.");
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Built with:");
    ImGui::BulletText("Dear ImGui (docking branch)  —  ocornut/imgui");
    ImGui::BulletText("nlohmann/json v3.11.3");
    ImGui::BulletText("SQLite 3.45.0");
    ImGui::BulletText("DirectX 11 / WIC / Win32 API");

    ImGui::Spacing();
    ImGui::TextDisabled("Tools:");
    ImGui::BulletText("SQLite Editor  —  standalone database browser");
    ImGui::BulletText("JSON Viewer    —  standalone JSON file viewer");

    ImGui::Spacing();
    ImGui::TextDisabled("License: MIT");
}
