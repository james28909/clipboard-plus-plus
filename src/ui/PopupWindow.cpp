#include "PopupWindow.h"
#include "../app/Application.h"
#include "../clipboard/ImageStore.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ClipboardMonitor.h"
#include "../clipboard/ContentDetector.h"
#include "../util/Win32Util.h"
#include "Appearance.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cfloat>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>
#include "../hotkeys/HotkeyManager.h"  // kClipboardPasteMagic

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static constexpr wchar_t kPopupClass[] = L"CPPPopupWnd";
static constexpr UINT_PTR kPopupResizeRenderTimerId = 1;
static constexpr int kPopupResizeBorder = 8;
static constexpr int kPopupTitlePad = 8;
static constexpr int kPopupTitleButton = 22;
static constexpr int kPopupTitleGap = 4;
static constexpr int kPopupTitleHeight = kPopupTitlePad + kPopupTitleButton + 8;

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

static const char* PasteMoveLabel(ClipboardHistory::MoveTarget target) {
    switch (target) {
    case ClipboardHistory::MoveTarget::Top:    return "Move: Top";
    case ClipboardHistory::MoveTarget::Bottom: return "Move: Bottom";
    default:                                   return "Move: Keep";
    }
}

static ClipboardHistory::MoveTarget NextPasteMoveTarget(ClipboardHistory::MoveTarget target) {
    switch (target) {
    case ClipboardHistory::MoveTarget::None:   return ClipboardHistory::MoveTarget::Top;
    case ClipboardHistory::MoveTarget::Top:    return ClipboardHistory::MoveTarget::Bottom;
    case ClipboardHistory::MoveTarget::Bottom: return ClipboardHistory::MoveTarget::None;
    default:                                   return ClipboardHistory::MoveTarget::None;
    }
}

static std::string TrimAscii(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

static std::wstring UrlEncodeWide(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<wchar_t>(c));
        } else if (c == ' ') {
            out.push_back(L'+');
        } else {
            out.push_back(L'%');
            out.push_back(static_cast<wchar_t>(kHex[c >> 4]));
            out.push_back(static_cast<wchar_t>(kHex[c & 0x0F]));
        }
    }
    return out;
}

// -- Create / Destroy ----------------------------------------------------------

bool PopupWindow::Create(HINSTANCE hInstance,
                          ID3D11Device* device,
                          ID3D11DeviceContext* context) {
    m_hInstance = hInstance;
    m_device    = device;
    m_context   = context;

    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = nullptr;  // D3D owns the background - prevents white flash on resize
    wc.lpszClassName = kPopupClass;
    RegisterClassExW(&wc);

    // WS_EX_TOPMOST  - always above other windows
    // WS_EX_LAYERED  - needed for SetLayeredWindowAttributes (opacity)
    // WS_EX_NOACTIVATE - don't steal focus from the app being pasted into.
    //                    Keyboard input for the search bar will be handled
    //                    via WH_KEYBOARD_LL in Milestone 4.
    // WS_POPUP + WS_THICKFRAME - borderless but resizable
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        kPopupClass, nullptr,
        WS_POPUP | WS_THICKFRAME,
        0, 0, m_width, m_height,
        nullptr, nullptr, hInstance,
        static_cast<LPVOID>(this));

    if (!m_hwnd) return false;

    if (!CreateSwapChain()) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    // ImGui context - independent from the main window's context
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();

    m_imguiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGuiIO& io  = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyThemeStyle(ThemeId::DarkDefault, true);

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);

    // Restore the main context so we don't interfere with Application init
    ImGui::SetCurrentContext(prevCtx);
    return true;
}

void PopupWindow::ApplyAppearance(const AppearanceSettings& settings) {
    if (!m_imguiCtx) return;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    m_appearance = settings;
    m_appearance.dpiScale = win32util::DpiScaleForWindow(m_hwnd);
    ApplyThemeStyle(m_appearance, true);
    ImGui_ImplDX11_InvalidateDeviceObjects();
    RebuildFontAtlas(ImGui::GetIO(), m_appearance);
    ImGui_ImplDX11_CreateDeviceObjects();
    m_opacity = settings.popupOpacity;
    m_outlineStrength = settings.popupOutlineStrength;
    const float dpiScale = EffectiveUiScale(m_appearance);
    m_width = static_cast<int>(std::lround(settings.popupWidth * dpiScale));
    m_height = static_cast<int>(std::lround(settings.popupHeight * dpiScale));
    ApplyOpacity();
    if (m_visible) {
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, m_width, m_height,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        ResizeSwapChainToClient();
        ApplyWindowCorners();
    }

    ImGui::SetCurrentContext(prevCtx);
}

void PopupWindow::Destroy() {
    ClearThumbCache();
    if (m_imguiCtx) {
        ImGuiContext* prevCtx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(m_imguiCtx);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imguiCtx = nullptr;
        if (prevCtx != m_imguiCtx)
            ImGui::SetCurrentContext(prevCtx);
    }
    DestroySwapChain();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        UnregisterClassW(kPopupClass, m_hInstance);
        m_hwnd = nullptr;
    }
}

// -- Show / Hide ---------------------------------------------------------------

void PopupWindow::Show(bool focusSearch) {
    m_prevForeground = GetForegroundWindow();
    PositionAtCursor();
    ResizeSwapChainToClient();
    ApplyWindowCorners();
    ApplyOpacity();
    ShowWindow(m_hwnd, SW_SHOWNA); // NA = no-activate
    m_visible           = true;
    m_justOpened        = true;
    m_focusSearchOnOpen = focusSearch;
    m_searchActive      = false;
    m_searchCapture     = focusSearch;
    m_keyboardCapture   = true;
    m_maximized         = false;
    m_queueMode         = false;
    m_queue.clear();
    std::memset(m_searchBuf, 0, sizeof(m_searchBuf));
    m_imageListDirty = true;   // refresh image list on next open
}

void PopupWindow::Hide() {
    ShowWindow(m_hwnd, SW_HIDE);
    m_visible       = false;
    m_searchActive  = false;
    m_searchCapture = false;
    m_dialogTextCapture = false;
    m_clipboardDropdownOpen = false;
    m_openDeleteConfirm = false;
    m_pendingDeleteProfileId.clear();
    m_openProfileContextMenu = false;
    m_contextMenuProfileId.clear();
    m_keyboardCapture = false;
    m_maximized = false;
    m_queueMode     = false;
    m_queue.clear();
    ClearThumbCache();
}

void PopupWindow::OpenSettingsWindow() {
    ActivateKeyboardCapture();
    Application::Get()->ShowMainWindow();
    Hide();
}

SIZE PopupWindow::GetCurrentSize() const {
    RECT rc{};
    if (m_hwnd && GetWindowRect(m_hwnd, &rc))
        return {rc.right - rc.left, rc.bottom - rc.top};
    return {m_width, m_height};
}

void PopupWindow::RequestSearchFocus() {
    m_focusSearchOnOpen = true;
    m_searchCapture = true;
    m_keyboardCapture = true;
    m_justOpened = true;
}

// -- Render --------------------------------------------------------------------

void PopupWindow::Render() {
    if (!m_visible || !m_imguiCtx) return;

    if (Application* app = Application::Get())
        app->SyncClipboardForForegroundProcess();

    // Switch to popup ImGui context for this frame
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_imguiCtx);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Full-window ImGui overlay - no title bar, fills the entire HWND
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({(float)(rc.right), (float)(rc.bottom)}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f); // opacity handled at OS level

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##popup", nullptr, flags);

    DrawTitleBar();

    // Escape closes
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::End(); ImGui::Render();
        ImGui::SetCurrentContext(prevCtx);
        Hide();
        return;
    }

    DrawSearchBar();
    ImGui::Spacing();
    DrawFilterStrip();
    ImGui::Separator();
    DrawItemList();

    AppearanceSettings effective = m_appearance.customColors
        ? m_appearance
        : ThemeDefaults(m_appearance.theme);
    effective.popupOutlineAnimated = m_appearance.popupOutlineAnimated;
    effective.popupOutlineAnimationSpeed = m_appearance.popupOutlineAnimationSpeed;
    effective.popupOutlineColorSharpness = m_appearance.popupOutlineColorSharpness;
    effective.popupOutlineColorSpread = m_appearance.popupOutlineColorSpread;
    effective.popupOutlineSaturation = m_appearance.popupOutlineSaturation;
    effective.popupOutlineBrightness = m_appearance.popupOutlineBrightness;
    effective.popupOutlineReverse = m_appearance.popupOutlineReverse;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max = {pos.x + size.x, pos.y + size.y};
    const float rounding = ImGui::GetStyle().WindowRounding;
    const float strength = std::clamp(m_outlineStrength, 0.0f, 1.0f);
    if (strength > 0.001f) {
        auto outlineColor = [&](float offset, float alpha) {
            if (!effective.popupOutlineAnimated) {
                ImVec4 color = effective.accent;
                color.w = alpha;
                return color;
            }
            float r = 1.0f, g = 1.0f, b = 1.0f;
            const float direction = effective.popupOutlineReverse ? -1.0f : 1.0f;
            const float speed = std::clamp(effective.popupOutlineAnimationSpeed, 0.05f, 5.0f);
            const float spread = std::clamp(effective.popupOutlineColorSpread, 0.0f, 2.0f);
            const float sharpness = std::clamp(effective.popupOutlineColorSharpness, 0.0f, 1.0f);
            const float saturation = std::clamp(effective.popupOutlineSaturation, 0.0f, 1.0f);
            const float brightness = std::clamp(effective.popupOutlineBrightness, 0.20f, 1.0f);
            float hue = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.10f * speed * direction +
                                  offset * spread, 1.0f);
            if (hue < 0.0f)
                hue += 1.0f;
            const float stepped = std::floor(hue * 6.0f + 0.5f) / 6.0f;
            hue = hue + (stepped - hue) * sharpness;
            ImGui::ColorConvertHSVtoRGB(hue, saturation, brightness, r, g, b);
            return ImVec4(r, g, b, alpha);
        };
        ImVec4 glow = outlineColor(0.0f, strength * 0.26f);
        glow.w = strength * 0.26f;
        ImVec4 soft = outlineColor(0.08f, strength * 0.38f);
        dl->AddRect({pos.x + 1.0f, pos.y + 1.0f}, {max.x - 1.0f, max.y - 1.0f},
                    ImGui::GetColorU32(glow), rounding, 0, 1.0f + strength * 4.5f);
        dl->AddRect({pos.x + 2.0f, pos.y + 2.0f}, {max.x - 2.0f, max.y - 2.0f},
                    ImGui::GetColorU32(soft), rounding, 0, 0.75f + strength * 2.5f);
        if (effective.popupOutlineAnimated) {
            const float thick = 0.75f + strength * 1.35f;
            dl->AddLine({pos.x + rounding, pos.y + 0.5f}, {max.x - rounding, pos.y + 0.5f},
                        ImGui::GetColorU32(outlineColor(0.00f, strength * 0.95f)), thick);
            dl->AddLine({max.x - 0.5f, pos.y + rounding}, {max.x - 0.5f, max.y - rounding},
                        ImGui::GetColorU32(outlineColor(0.25f, strength * 0.95f)), thick);
            dl->AddLine({max.x - rounding, max.y - 0.5f}, {pos.x + rounding, max.y - 0.5f},
                        ImGui::GetColorU32(outlineColor(0.50f, strength * 0.95f)), thick);
            dl->AddLine({pos.x + 0.5f, max.y - rounding}, {pos.x + 0.5f, pos.y + rounding},
                        ImGui::GetColorU32(outlineColor(0.75f, strength * 0.95f)), thick);
        } else {
            dl->AddRect({pos.x + 0.5f, pos.y + 0.5f}, {max.x - 0.5f, max.y - 0.5f},
                        ImGui::GetColorU32(outlineColor(0.0f, strength * 0.92f)),
                        rounding, 0, 0.75f + strength * 1.0f);
        }
    }

    ImGui::End();
    ImGui::Render();

    // Clear to the same color ImGui uses for the popup background. With large
    // rounding, hard-coded clear pixels can show through as ghost corners.
    const float bg[4] = {effective.windowBg.x, effective.windowBg.y, effective.windowBg.z, 1.0f};
    m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
    m_context->ClearRenderTargetView(m_renderTarget, bg);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swapChain->Present(1, 0);

    ImGui::SetCurrentContext(prevCtx);
}

// -- Filter strip --------------------------------------------------------------

void PopupWindow::DrawSearchBar() {
    ImGui::SetNextItemWidth(-FLT_MIN);
    if ((m_justOpened && m_focusSearchOnOpen) ||
        (m_searchCapture && !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        ImGui::SetKeyboardFocusHere();
    }
    m_justOpened = false;

    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
    const bool enterPressed = ImGui::InputTextWithHint("##search", "  Search... Shift+Enter for web",
                                                       m_searchBuf, sizeof(m_searchBuf),
                                                       inputFlags);
    const bool searchHovered = ImGui::IsItemHovered();
    const bool searchInputActive = ImGui::IsItemActive();
    if (m_focusSearchOnOpen || ImGui::IsItemClicked()) {
        m_keyboardCapture = true;
        m_searchCapture = true;
    }

    ImGuiIO& popupIo = ImGui::GetIO();
    if (enterPressed && popupIo.KeyShift)
        LaunchWebSearch();

    m_searchActive = searchInputActive || m_searchCapture;
    m_focusSearchOnOpen = false;

    const bool anyItemActive = ImGui::IsAnyItemActive();
    const size_t searchLen = std::strlen(m_searchBuf);
    if (m_searchActive != m_lastSearchActive ||
        searchInputActive != m_lastSearchInputActive ||
        anyItemActive != m_lastAnyItemActive ||
        popupIo.WantTextInput != m_lastWantTextInput ||
        searchLen != m_lastSearchLen) {
        std::snprintf(m_searchDebug, sizeof(m_searchDebug),
                      "Search debug: active=%d capture=%d input=%d hover=%d anyActive=%d wantText=%d len=%zu text=\"%.48s\"",
                      m_searchActive ? 1 : 0,
                      m_searchCapture ? 1 : 0,
                      searchInputActive ? 1 : 0,
                      searchHovered ? 1 : 0,
                      anyItemActive ? 1 : 0,
                      popupIo.WantTextInput ? 1 : 0,
                      searchLen,
                      m_searchBuf);
        std::string line(m_searchDebug);
        line += "\n";
        OutputDebugStringA(line.c_str());
        m_lastSearchActive = m_searchActive;
        m_lastSearchInputActive = searchInputActive;
        m_lastAnyItemActive = anyItemActive;
        m_lastWantTextInput = popupIo.WantTextInput;
        m_lastSearchLen = searchLen;
    }
}

void PopupWindow::DrawTitleBar() {
    const float height = static_cast<float>(kPopupTitleButton);
    const float gap = static_cast<float>(kPopupTitleGap);
    const float knobSize = height;
    const AppearanceSettings effective = m_appearance.customColors ? m_appearance : ThemeDefaults(m_appearance.theme);

    ImGui::PushStyleColor(ImGuiCol_Button, effective.closeButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, effective.closeButtonHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, effective.closeButtonHover);
    ImGui::PushStyleColor(ImGuiCol_Text, effective.closeButtonText);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0.0f, 0.0f});
    if (ImGui::Button("x", {height, height})) {
        Hide();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::SameLine();
    ImGui::InvisibleButton("##popup_adjust_knobs", {knobSize, knobSize});
    const bool knobHovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool opacityHovered = knobHovered && mouse.y < (min.y + max.y) * 0.5f;
    const bool outlineHovered = knobHovered && !opacityHovered;
    if (opacityHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        if (Application* app = Application::Get()) {
            app->SetPopupOpacity(m_opacity + ImGui::GetIO().MouseWheel * 0.05f);
        }
    }
    if (outlineHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        if (Application* app = Application::Get()) {
            app->SetPopupOutlineStrength(m_outlineStrength + ImGui::GetIO().MouseWheel * 0.05f);
        }
    }

    const ImVec2 opacityCenter = {(min.x + max.x) * 0.5f, min.y + knobSize * 0.28f};
    const ImVec2 outlineCenter = {(min.x + max.x) * 0.5f, min.y + knobSize * 0.73f};
    const float radius = knobSize * 0.18f;
    const ImVec4 ring = effective.opacityKnobRing;
    const ImVec4 fill = effective.opacityKnobFill;
    auto brighten = [](ImVec4 c, float amount) {
        c.x = std::min(1.0f, c.x + amount);
        c.y = std::min(1.0f, c.y + amount);
        c.z = std::min(1.0f, c.z + amount);
        return c;
    };
    auto drawKnob = [&](ImVec2 center, float value, bool hovered, ImVec4 knobRing, ImVec4 knobFill) {
        const ImU32 ringColor = ImGui::GetColorU32(hovered ? brighten(knobRing, 0.12f) : knobRing);
        const ImU32 fillColor = ImGui::GetColorU32(hovered ? brighten(knobFill, 0.08f) : knobFill);
        dl->AddCircleFilled(center, radius, fillColor, 18);
        dl->AddCircle(center, radius, ringColor, 18, 1.5f);
        const float angle = -1.5708f + value * 6.28318f;
        dl->AddLine(center,
                    {center.x + std::cos(angle) * radius * 0.78f,
                     center.y + std::sin(angle) * radius * 0.78f},
                    ringColor, 1.5f);
    };
    drawKnob(opacityCenter, m_opacity, opacityHovered, ring, fill);
    drawKnob(outlineCenter, m_outlineStrength, outlineHovered, effective.accent, effective.panelBg);
    if (opacityHovered)
        ImGui::SetTooltip("Window opacity %.0f%%", m_opacity * 100.0f);
    else if (outlineHovered)
        ImGui::SetTooltip("Outline %.0f%%", m_outlineStrength * 100.0f);

    Application* app = Application::Get();
    const ClipboardProfileConfig* active = app ? app->GetActiveClipboardProfile() : nullptr;
    if (active && active->id != m_lastClipboardId) {
        m_lastClipboardId = active->id;
        std::snprintf(m_clipboardName, sizeof(m_clipboardName), "%s", active->name.c_str());
    }

    auto clipboardNameExists = [&]() {
        if (!app || m_clipboardName[0] == '\0')
            return true;
        for (const ClipboardProfileConfig& profile : app->GetClipboardProfiles()) {
            if (profile.name == m_clipboardName)
                return true;
        }
        return false;
    };

    const bool showSave = !clipboardNameExists() && m_clipboardName[0] != '\0';
    const float saveW = std::ceil(ImGui::CalcTextSize("Save").x + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f);
    ImGui::SameLine(0.0f, gap);
    const float controlsAvail = ImGui::GetContentRegionAvail().x;
    const float saveReserve = showSave ? saveW + gap : 0.0f;
    const float inputW = std::max(80.0f, controlsAvail - saveReserve);

    ImGui::SetNextItemWidth(inputW);
    ImGui::InputText("##clipboard_name", m_clipboardName, sizeof(m_clipboardName));
    const bool clipboardNameActive = ImGui::IsItemActive();
    if (clipboardNameActive || ImGui::IsItemClicked()) {
        ActivateKeyboardCapture();
        m_dialogTextCapture = true;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        m_clipboardDropdownOpen = true;
        ImGui::SetKeyboardFocusHere(-1);
    }
    const bool clipboardInputHovered = ImGui::IsItemHovered();
    if (clipboardInputHovered && !clipboardNameActive)
        ImGui::SetTooltip("Click to select or create a clipboard profile");

    const ImVec2 inputMin = ImGui::GetItemRectMin();
    const ImVec2 inputMax = ImGui::GetItemRectMax();

    if (m_clipboardDropdownOpen && app) {
        const float dropW = inputMax.x - inputMin.x;
        ImGui::SetNextWindowPos({inputMin.x, inputMax.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints({dropW, 0.0f}, {dropW, 300.0f});
        ImGui::SetNextWindowBgAlpha(1.0f);
        constexpr ImGuiWindowFlags kDropFlags =
            ImGuiWindowFlags_NoTitleBar        | ImGuiWindowFlags_NoResize     |
            ImGuiWindowFlags_NoMove            | ImGuiWindowFlags_NoScrollbar  |
            ImGuiWindowFlags_NoSavedSettings   | ImGuiWindowFlags_NoNav        |
            ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::Begin("##clipboard_profile_dropdown", nullptr, kDropFlags);
        const bool dropdownHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
            ImGuiHoveredFlags_AllowWhenBlockedByPopup);

        const std::vector<ClipboardProfileConfig>& profiles = app->GetClipboardProfiles();
        for (const ClipboardProfileConfig& profile : profiles) {
            const bool selected = active && profile.id == active->id;
            std::string displayLabel = profile.name;
            if (!profile.processName.empty())
                displayLabel += " (" + profile.processName + ")";
            const std::string selectableId = displayLabel + "##prof_" + profile.id;

            if (ImGui::Selectable(selectableId.c_str(), selected)) {
                app->SetActiveClipboardProfile(profile.id);
                std::snprintf(m_clipboardName, sizeof(m_clipboardName), "%s", profile.name.c_str());
                m_lastClipboardId = profile.id;
                m_dialogTextCapture = false;
                m_clipboardDropdownOpen = false;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();

            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                m_contextMenuProfileId   = profile.id;
                m_contextMenuProfileName = profile.name;
                m_contextMenuX           = ImGui::GetIO().MousePos.x;
                m_contextMenuY           = ImGui::GetIO().MousePos.y;
                m_openProfileContextMenu = true;
            }
        }
        ImGui::End();

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !dropdownHovered && !clipboardInputHovered)
            m_clipboardDropdownOpen = false;
    }

    // Profile context menu — parented to ##popup so input routing works correctly
    if (m_openProfileContextMenu) {
        ImGui::SetNextWindowPos({m_contextMenuX, m_contextMenuY}, ImGuiCond_Always);
        ImGui::OpenPopup("##profile_ctx_menu");
        m_openProfileContextMenu = false;
    }
    if (ImGui::BeginPopup("##profile_ctx_menu")) {
        if (app) {
            if (ImGui::MenuItem("Duplicate")) {
                app->CreateClipboardProfile(m_contextMenuProfileName + " - duplicate");
                if (const ClipboardProfileConfig* dup = app->GetActiveClipboardProfile()) {
                    std::snprintf(m_clipboardName, sizeof(m_clipboardName), "%s", dup->name.c_str());
                    m_lastClipboardId = dup->id;
                }
                m_clipboardDropdownOpen = false;
            }
            ImGui::Separator();
            const bool canDelete = app->GetClipboardProfiles().size() > 1;
            if (!canDelete) ImGui::BeginDisabled();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            if (ImGui::MenuItem("Delete")) {
                m_pendingDeleteProfileId   = m_contextMenuProfileId;
                m_pendingDeleteProfileName = m_contextMenuProfileName;
                m_openDeleteConfirm        = true;
            }
            ImGui::PopStyleColor();
            if (!canDelete) ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }

    // Delete confirmation modal — parented to the main ##popup window
    if (m_openDeleteConfirm) {
        ImGui::OpenPopup("##confirm_delete");
        m_openDeleteConfirm = false;
    }
    if (ImGui::BeginPopupModal("##confirm_delete", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextUnformatted("Delete clipboard profile?");
        ImGui::Spacing();
        ImGui::TextDisabled("%s", m_pendingDeleteProfileName.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("All history for this profile will be permanently lost.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.72f, 0.08f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.14f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.58f, 0.04f, 0.04f, 1.0f));
        if (ImGui::Button("Delete", {110.0f, 0.0f}) && app) {
            app->SetActiveClipboardProfile(m_pendingDeleteProfileId);
            app->DeleteActiveClipboardProfile();
            if (const ClipboardProfileConfig* cur = app->GetActiveClipboardProfile()) {
                std::snprintf(m_clipboardName, sizeof(m_clipboardName), "%s", cur->name.c_str());
                m_lastClipboardId = cur->id;
            }
            m_pendingDeleteProfileId.clear();
            m_clipboardDropdownOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button("Cancel", {110.0f, 0.0f})) {
            m_pendingDeleteProfileId.clear();
            m_clipboardDropdownOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (showSave) {
        ImGui::SameLine(0.0f, gap);
        if (ImGui::Button("Save", {saveW, 0.0f}) && app) {
            app->CreateClipboardProfile(m_clipboardName);
            if (const ClipboardProfileConfig* created = app->GetActiveClipboardProfile()) {
                std::snprintf(m_clipboardName, sizeof(m_clipboardName), "%s", created->name.c_str());
                m_lastClipboardId = created->id;
            }
            m_dialogTextCapture = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create new clipboard profile");
    }

    if (m_dialogTextCapture && !clipboardNameActive && !m_clipboardDropdownOpen)
        m_dialogTextCapture = false;

    ImGui::Spacing();
}

static bool PopupToggleButton(const char* label, bool active, const PopupToggleColors& colors) {
    ImGui::PushStyleColor(ImGuiCol_Button, active ? colors.on : colors.off);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? colors.onHovered : colors.offHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? colors.onActive : colors.offActive);
    const bool clicked = ImGui::SmallButton(label);
    ImGui::PopStyleColor(3);
    return clicked;
}

void PopupWindow::DrawFilterStrip() {
    const PopupToggleColors toggleColors = GetPopupToggleColors(m_appearance);
    struct Btn { const char* label; int mode; };
    static constexpr Btn kFilters[] = {
        {"All",0},{"Text",1},{"Image",2},{"URL",3},{"File",4},
        {"Code",5},{"Secret",6},{"JSON",7},{"Email",8},{"Color",9},{"CMD",10}
    };
    for (const auto& f : kFilters) {
        bool active = (m_filterMode == f.mode);
        if (PopupToggleButton(f.label, active, toggleColors)) {
            ActivateKeyboardCapture();
            m_filterMode = f.mode;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    bool qActive = m_queueMode;
    if (PopupToggleButton("Queue", qActive, toggleColors)) {
        ActivateKeyboardCapture();
        m_queueMode = !m_queueMode;
        m_queue.clear();
    }
    ImGui::SameLine();

    if (m_queueMode && !m_queue.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.f));
        if (ImGui::SmallButton("Paste All")) {
            ActivateKeyboardCapture();
            PasteQueue();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    bool newlineActive = m_appendNewlineAfterPaste;
    if (PopupToggleButton("Newline", newlineActive, toggleColors)) {
        ActivateKeyboardCapture();
        m_appendNewlineAfterPaste = !m_appendNewlineAfterPaste;
    }
    ImGui::SameLine();

    bool moveActive = m_pasteMoveTarget != ClipboardHistory::MoveTarget::None;
    if (PopupToggleButton(PasteMoveLabel(m_pasteMoveTarget), moveActive, toggleColors)) {
        ActivateKeyboardCapture();
        m_pasteMoveTarget = NextPasteMoveTarget(m_pasteMoveTarget);
    }
    ImGui::SameLine();

    if (ImGui::SmallButton(" @ ")) {       // gear placeholder
        OpenSettingsWindow();
    }
    ImGui::Spacing();
}

// -- Item list -----------------------------------------------------------------

bool PopupWindow::ItemPassesFilter(const ClipboardItem& item) const {
    switch (m_filterMode) {
    case 1: return item.IsText();
    case 2: return item.IsImage();
    case 3: return (item.tags & TAG_URL) != 0;
    case 4: return item.type == ContentType::FilePaths || (item.tags & (TAG_FILE | TAG_FOLDER | TAG_PATH)) != 0;
    case 5: return (item.tags & TAG_CODE) != 0;
    case 6: return (item.tags & TAG_SECRET) != 0;
    case 7: return (item.tags & TAG_JSON) != 0;
    case 8: return (item.tags & TAG_EMAIL) != 0;
    case 9:  return (item.tags & TAG_HEX) != 0;
    case 10: return (item.tags & TAG_COMMAND) != 0;
    default: return true;
    }
}

std::vector<size_t> PopupWindow::BuildVisibleHistoryIndices(bool pinnedOnly) const {
    std::vector<size_t> indices;
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return indices;

    const std::string query(m_searchBuf);
    std::string lquery = query;
    std::transform(lquery.begin(), lquery.end(), lquery.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });

    for (size_t i = 0; i < hist->Size(); ++i) {
        const ClipboardItem* item = hist->Get(i);
        if (!item || !ItemPassesFilter(*item)) continue;
        if (item->pinned != pinnedOnly) continue;

        if (!lquery.empty()) {
            std::string lt = item->text;
            std::transform(lt.begin(), lt.end(), lt.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });
            if (lt.find(lquery) == std::string::npos) continue;
        }

        indices.push_back(i);
    }

    return indices;
}

void PopupWindow::DrawItemList() {
    // Image filter mode gets its own dedicated browser UI
    if (m_filterMode == 2) {
        DrawImageBrowser();
        return;
    }

    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGuiWindowFlags itemFlags = m_appearance.showScrollbars
        ? ImGuiWindowFlags_None
        : ImGuiWindowFlags_NoScrollbar;
    ImGui::BeginChild("##items", {0.f, 0.f}, ImGuiChildFlags_None, itemFlags);

    const std::vector<size_t> pinned = BuildVisibleHistoryIndices(true);
    const std::vector<size_t> regular = BuildVisibleHistoryIndices(false);

    auto drawSection = [&](const char* title,
                           const std::vector<size_t>& indices,
                           bool pinnedSection) -> bool {
        if (indices.empty())
            return false;

        if (pinnedSection) {
            char header[128]{};
            std::snprintf(header, sizeof(header), "%s (%zu)", title, indices.size());
            if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Spacing();
                return false;
            }
        } else {
            ImGui::TextDisabled("%s (%zu)", title, indices.size());
            ImGui::Separator();
        }

        for (size_t sectionSlot = 0; sectionSlot < indices.size(); ++sectionSlot) {
            const size_t i = indices[sectionSlot];
            const ClipboardItem* item = hist->Get(i);
            if (!item) continue;

            const std::string key = HotkeyManager::SlotLabelText(static_cast<int>(sectionSlot));

            int qpos = -1;
            for (size_t q = 0; q < m_queue.size(); ++q)
                if (m_queue[q] == item->id) { qpos = (int)q + 1; break; }

            const bool isSecret = (item->tags & TAG_SECRET) != 0;
            if (isSecret)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.42f, 0.42f, 1.f));

            const float rowWidth = ImGui::GetContentRegionAvail().x;
            const int previewChars = std::max(40, static_cast<int>((rowWidth - 92.0f) / 7.0f));
            const std::string preview = item->Preview(static_cast<size_t>(previewChars));

            char label[1024]{};
            const char* pin = pinnedSection ? "[P] " : "";
            if (qpos >= 0)
                std::snprintf(label, sizeof(label), " %s [%d]  %s%s##r%zu",
                              key.c_str(), qpos, pin, preview.c_str(), i);
            else
                std::snprintf(label, sizeof(label), " %s   %s%s##r%zu",
                              key.c_str(), pin, preview.c_str(), i);

            if (ImGui::Selectable(label, qpos >= 0,
                                   ImGuiSelectableFlags_SpanAllColumns)) {
                ActivateKeyboardCapture();
                ReleaseSearchCapture();

                const bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

                if (ctrlHeld) {
                    if (isSecret) ImGui::PopStyleColor();
                    const uint64_t itemId = item->id;
                    PasteItemKeepOpen(*item);
                    hist->MoveItemById(itemId, m_pasteMoveTarget);
                    return true;
                } else if (m_queueMode) {
                    if (qpos >= 0)
                        m_queue.erase(std::remove(m_queue.begin(), m_queue.end(), item->id),
                                       m_queue.end());
                    else
                        m_queue.push_back(item->id);
                } else {
                    if (isSecret) ImGui::PopStyleColor();
                    const uint64_t itemId = item->id;
                    PasteItemKeepOpen(*item);
                    hist->MoveItemById(itemId, m_pasteMoveTarget);
                    return true;
                }
            }

            if (pinnedSection) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                dl->AddCircleFilled({a.x + 7.0f, (a.y + b.y) * 0.5f},
                                    3.0f, IM_COL32(255, 196, 64, 255), 12);
            }

            if (DrawItemContextMenu(*item, qpos)) {
                if (isSecret) ImGui::PopStyleColor();
                return true;
            }
            DrawItemDragDrop(item->id, qpos);
            if (isSecret) ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        return false;
    };

    if (drawSection("Pinned entries", pinned, true) ||
        drawSection("History", regular, false)) {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    if (!pinned.empty() || !regular.empty()) {
        ImGui::InvisibleButton("##drop_end", {ImGui::GetContentRegionAvail().x, 8.0f});
        if (ImGui::BeginDragDropTarget()) {
            if (ImGui::AcceptDragDropPayload("CPP_HISTORY_IDS")) {
                hist->MoveItemsByIdBefore(m_dragIds, 0);
                m_queue.clear();
            }
            ImGui::EndDragDropTarget();
        }
    }

    if (pinned.empty() && regular.empty())
        ImGui::TextDisabled("  No items match.");

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ActivateKeyboardCapture();
        ReleaseSearchCapture();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

bool PopupWindow::DrawItemContextMenu(const ClipboardItem& item, int qpos) {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist)
        return false;

    bool changed = false;
    const uint64_t itemId = item.id;

    if (ImGui::BeginPopupContextItem()) {
        ActivateKeyboardCapture();
        ReleaseSearchCapture();

        ImGui::TextDisabled("Item %llu", static_cast<unsigned long long>(itemId));
        ImGui::Separator();

        if (ImGui::MenuItem("Paste")) {
            PasteItemKeepOpen(item);
            hist->MoveItemById(itemId, m_pasteMoveTarget);
            changed = true;
        }
        if (ImGui::MenuItem("Copy to clipboard")) {
            WriteToClipboard(item);
        }

        ImGui::Separator();
        if (qpos >= 0) {
            if (ImGui::MenuItem("Remove from queue")) {
                m_queue.erase(std::remove(m_queue.begin(), m_queue.end(), itemId),
                              m_queue.end());
                changed = true;
            }
        } else {
            if (ImGui::MenuItem("Add to queue")) {
                m_queue.push_back(itemId);
                changed = true;
            }
        }

        if (ImGui::MenuItem("Move to top")) {
            hist->MoveItemById(itemId, ClipboardHistory::MoveTarget::Top);
            changed = true;
        }
        if (ImGui::MenuItem("Move to bottom")) {
            hist->MoveItemById(itemId, ClipboardHistory::MoveTarget::Bottom);
            changed = true;
        }
        if (ImGui::MenuItem(item.pinned ? "Unpin" : "Pin")) {
            hist->SetPinnedById(itemId, !item.pinned);
            changed = true;
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
            m_queue.erase(std::remove(m_queue.begin(), m_queue.end(), itemId),
                          m_queue.end());
            hist->RemoveItemById(itemId);
            changed = true;
        }

        ImGui::EndPopup();
    }

    return changed;
}

// -- Paste ---------------------------------------------------------------------

void PopupWindow::ReleaseSearchCapture() {
    m_searchCapture = false;
    m_searchActive = false;
    m_focusSearchOnOpen = false;
}

void PopupWindow::ActivateKeyboardCapture() {
    m_keyboardCapture = true;
}

void PopupWindow::NoteExternalMouseDown(POINT screenPoint) {
    RECT popupRect{};
    if (GetWindowRect(m_hwnd, &popupRect) &&
        PtInRect(&popupRect, screenPoint)) {
        return;
    }

    ReleaseSearchCapture();
    m_keyboardCapture = false;

    HWND clicked = WindowFromPoint(screenPoint);
    HWND main = Application::Get() ? Application::Get()->GetHwnd() : nullptr;
    if (clicked) clicked = GetAncestor(clicked, GA_ROOT);
    if (clicked && clicked != m_hwnd && clicked != main) {
        m_prevForeground = clicked;
        if (Application* app = Application::Get())
            app->SyncClipboardForWindow(clicked);
    }
}

// -- Image browser -------------------------------------------------------------

void PopupWindow::ClearThumbCache() {
    for (auto& kv : m_thumbCache)
        if (kv.second.srv) kv.second.srv->Release();
    m_thumbCache.clear();
}

void PopupWindow::DrawImageBrowser() {
    Application* app = Application::Get();
    ImageStore* store = app ? app->GetImageStore() : nullptr;

    if (!store || !store->IsOpen()) {
        ImGui::Spacing();
        ImGui::TextDisabled("  Image store not available.");
        return;
    }

    // -- Sub-filter bar -------------------------------------------------------
    const PopupToggleColors tc = GetPopupToggleColors(m_appearance);
    static const char* kSortLabels[] = {"Newest", "Oldest", "Largest", "Smallest"};
    static const char* kDateLabels[] = {"All time", "Today", "This week", "This month"};
    static const char* kSizeLabels[] = {"Any size", "> 100 KB", "> 500 KB", "> 1 MB"};

    ImGui::SetNextItemWidth(88.0f);
    ImGui::Combo("##img_sort", &m_imgSort, kSortLabels, 4);
    ImGui::SameLine(0, 6.0f);
    ImGui::SetNextItemWidth(88.0f);
    ImGui::Combo("##img_date", &m_imgDateFilter, kDateLabels, 4);
    ImGui::SameLine(0, 6.0f);
    ImGui::SetNextItemWidth(84.0f);
    ImGui::Combo("##img_size", &m_imgSizeFilter, kSizeLabels, 4);
    ImGui::SameLine(0, 6.0f);
    if (ImGui::SmallButton("Refresh")) {
        m_imageListDirty = true;
        ClearThumbCache();
    }
    ImGui::Separator();

    // -- Load or refresh image list -------------------------------------------
    if (m_imageListDirty) {
        m_cachedImageList = store->ListAll();
        m_imageListDirty = false;
    }

    // -- Filter + sort --------------------------------------------------------
    using namespace std::chrono;
    const int64_t nowMs = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    const int64_t kDayMs   = 24LL * 3600 * 1000;
    const int64_t kWeekMs  = 7  * kDayMs;
    const int64_t kMonthMs = 30 * kDayMs;

    std::vector<const ImageRecord*> rows;
    rows.reserve(m_cachedImageList.size());
    for (const auto& r : m_cachedImageList) {
        const int64_t age = nowMs - r.capturedAt;
        if (m_imgDateFilter == 1 && age > kDayMs)   continue;
        if (m_imgDateFilter == 2 && age > kWeekMs)  continue;
        if (m_imgDateFilter == 3 && age > kMonthMs) continue;
        if (m_imgSizeFilter == 1 && r.byteSize < 100  * 1024) continue;
        if (m_imgSizeFilter == 2 && r.byteSize < 500  * 1024) continue;
        if (m_imgSizeFilter == 3 && r.byteSize < 1024 * 1024) continue;

        // Search text: match dimensions or source process
        const std::string query(m_searchBuf);
        if (!query.empty()) {
            const std::string dim = std::to_string(r.width) + "x" + std::to_string(r.height);
            auto lcMatch = [&](const std::string& s) {
                std::string ls = s, lq = query;
                auto lc = [](unsigned char c){ return (char)std::tolower(c); };
                std::transform(ls.begin(), ls.end(), ls.begin(), lc);
                std::transform(lq.begin(), lq.end(), lq.begin(), lc);
                return ls.find(lq) != std::string::npos;
            };
            if (!lcMatch(dim) && !lcMatch(r.sourceProc) && !lcMatch(r.profileId)) continue;
        }
        rows.push_back(&r);
    }

    std::sort(rows.begin(), rows.end(), [&](const ImageRecord* a, const ImageRecord* b) {
        switch (m_imgSort) {
        case 1: return a->capturedAt < b->capturedAt;
        case 2: return a->byteSize   > b->byteSize;
        case 3: return a->byteSize   < b->byteSize;
        default: return a->capturedAt > b->capturedAt;
        }
    });

    ImGui::TextDisabled("  %zu image%s", rows.size(), rows.size() == 1 ? "" : "s");

    // -- Scrollable image list ------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGuiWindowFlags listFlags = m_appearance.showScrollbars
        ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar;

    if (ImGui::BeginChild("##imglist", {0.f, 0.f}, ImGuiChildFlags_None, listFlags)) {
        if (rows.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("  No images match the current filters.");
        }

        bool openCtxMenu = false;

        for (const ImageRecord* r : rows) {
            ImGui::PushID(r->id.c_str());

            // -- Lazy-load thumbnail ------------------------------------------
            ThumbEntry& entry = m_thumbCache[r->id];
            if (!entry.srv && entry.w == 0) {
                int tw = 0, th = 0;
                entry.srv = store->CreateThumbnailSRV(r->id, m_device, 128, tw, th);
                entry.w = tw ? tw : 1;
                entry.h = th ? th : 1;
            }

            // -- Row start ----------------------------------------------------
            const float kThumbH = 78.0f;
            const float kThumbMaxW = 120.0f;
            const float rowPadY = 5.0f;
            ImVec2 rowTL = ImGui::GetCursorScreenPos();

            // Thumbnail display dimensions
            float dispH = kThumbH;
            float dispW = (entry.h > 0)
                ? std::min(kThumbMaxW, dispH * (float)entry.w / (float)entry.h)
                : kThumbH;

            ImGui::Dummy({0.f, rowPadY}); // top padding

            if (entry.srv) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
                ImGui::Image((ImTextureID)(intptr_t)entry.srv, {dispW, dispH});
            } else {
                // Placeholder box while thumb loads (or failed)
                ImVec2 boxTL = ImGui::GetCursorScreenPos();
                boxTL.x += 4.0f;
                ImGui::GetWindowDrawList()->AddRectFilled(
                    boxTL, {boxTL.x + 64.0f, boxTL.y + kThumbH},
                    ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
                ImGui::Dummy({68.0f, kThumbH});
            }
            ImGui::SameLine(0, 10.0f);

            // -- Metadata -----------------------------------------------------
            ImGui::BeginGroup();

            // Line 1: dimensions + format badge
            const char* fmtTag =
                r->storedFormat == StoredFormat::Jpeg   ? "JPEG" :
                r->storedFormat == StoredFormat::RawDib ? "RAW"  : "PNG";
            ImGui::Text("%d \xc3\x97 %d", r->width, r->height);  // × (UTF-8 multiplication sign)
            ImGui::SameLine(0, 6.0f);
            ImGui::TextDisabled("[%s]", fmtTag);

            // Line 2: file size
            char sizeStr[32];
            if (r->byteSize >= 1024 * 1024)
                snprintf(sizeStr, sizeof(sizeStr), "%.2f MB", (double)r->byteSize / (1024.0 * 1024.0));
            else
                snprintf(sizeStr, sizeof(sizeStr), "%.1f KB", (double)r->byteSize / 1024.0);
            ImGui::TextDisabled("%s", sizeStr);

            // Line 3: source process (trimmed to 28 chars)
            if (!r->sourceProc.empty()) {
                const std::string& sp = r->sourceProc;
                if (sp.size() <= 28)
                    ImGui::TextDisabled("%s", sp.c_str());
                else
                    ImGui::TextDisabled("%.28s\xe2\x80\xa6", sp.c_str());  // …
            }

            // Line 4: captured date/time + relative age
            {
                const int64_t ageMs = nowMs - r->capturedAt;
                char relStr[24];
                if (ageMs < 60000)         snprintf(relStr, sizeof(relStr), "%llds ago", (long long)(ageMs/1000));
                else if (ageMs < 3600000)  snprintf(relStr, sizeof(relStr), "%lldm ago", (long long)(ageMs/60000));
                else if (ageMs < 86400000) snprintf(relStr, sizeof(relStr), "%lldh ago", (long long)(ageMs/3600000));
                else                       snprintf(relStr, sizeof(relStr), "%lldd ago", (long long)(ageMs/86400000));

                const time_t t = static_cast<time_t>(r->capturedAt / 1000);
                std::tm tm{};
#ifdef _WIN32
                localtime_s(&tm, &t);
#else
                localtime_r(&t, &tm);
#endif
                char dtStr[32];
                strftime(dtStr, sizeof(dtStr), "%b %d, %I:%M %p", &tm);
                ImGui::TextDisabled("%s  (%s)", dtStr, relStr);
            }

            ImGui::EndGroup();

            ImGui::Dummy({0.f, rowPadY}); // bottom padding

            // -- Click detection over the full row ----------------------------
            ImVec2 rowBR = ImGui::GetCursorScreenPos();
            rowBR.x = rowTL.x + ImGui::GetContentRegionAvail().x + ImGui::GetScrollX();

            const bool hovered = ImGui::IsMouseHoveringRect(
                rowTL, {rowBR.x, rowBR.y > rowTL.y ? rowBR.y : rowTL.y + kThumbH + rowPadY * 2});

            if (hovered) {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    rowTL, {rowBR.x, ImGui::GetCursorScreenPos().y},
                    ImGui::GetColorU32(ImGuiCol_ButtonHovered, 0.4f));
            }

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ClipboardItem fake;
                fake.type         = ContentType::Image;
                fake.imageStoreId = r->id;
                fake.imageW       = r->width;
                fake.imageH       = r->height;
                PasteItemKeepOpen(fake);
            }

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                m_imgCtxMenuId = r->id;
                openCtxMenu = true;
            }

            ImGui::Separator();
            ImGui::PopID();
        }

        if (openCtxMenu)
            ImGui::OpenPopup("##img_ctx");

        if (ImGui::BeginPopup("##img_ctx")) {
            ImGui::TextDisabled("%s", m_imgCtxMenuId.substr(0, 8).c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Paste image")) {
                ClipboardItem fake;
                fake.type         = ContentType::Image;
                fake.imageStoreId = m_imgCtxMenuId;
                PasteItemKeepOpen(fake);
            }
            if (ImGui::MenuItem("Copy to clipboard")) {
                ClipboardItem fake;
                fake.type         = ContentType::Image;
                fake.imageStoreId = m_imgCtxMenuId;
                WriteToClipboard(fake);
            }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.35f, 0.35f, 1.f));
            if (ImGui::MenuItem("Delete from database")) {
                store->Delete(m_imgCtxMenuId);
                m_thumbCache.erase(m_imgCtxMenuId);
                m_imageListDirty = true;
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void PopupWindow::DrawItemDragDrop(uint64_t itemId, int qpos) {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        if (qpos >= 0)
            m_dragIds = m_queue;
        else
            m_dragIds = { itemId };

        const int count = static_cast<int>(m_dragIds.size());
        ImGui::SetDragDropPayload("CPP_HISTORY_IDS", &count, sizeof(count));
        ImGui::Text("%d item%s", count, count == 1 ? "" : "s");
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("CPP_HISTORY_IDS")) {
            hist->MoveItemsByIdBefore(m_dragIds, itemId);
            m_queue.clear();
        }
        ImGui::EndDragDropTarget();
    }
}

void PopupWindow::PasteHistorySlot(int slot, HWND targetWindow) {
    if (slot < 0) return;

    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ClipboardItem item;
    if (!hist->GetRegularCopy(static_cast<size_t>(slot), item)) return;

    m_prevForeground = targetWindow;
    WriteToClipboard(item);
    RestoreFocusAndPaste(targetWindow);
    hist->MoveItemById(item.id, m_pasteMoveTarget);
}

void PopupWindow::PastePinnedSlot(int slot, HWND targetWindow) {
    if (slot < 0) return;

    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ClipboardItem item;
    if (!hist->GetPinnedCopy(static_cast<size_t>(slot), item)) return;

    m_prevForeground = targetWindow;
    WriteToClipboard(item);
    RestoreFocusAndPaste(targetWindow);
    hist->MoveItemById(item.id, ClipboardHistory::MoveTarget::None);
}

void PopupWindow::PasteVisibleSlot(int slot) {
    const std::vector<size_t> regular = BuildVisibleHistoryIndices(false);
    if (slot < 0 || static_cast<size_t>(slot) >= regular.size()) return;
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    ClipboardItem item;
    if (!hist->GetCopy(regular[static_cast<size_t>(slot)], item)) return;

    PasteItemKeepOpen(item);
    hist->MoveItemById(item.id, m_pasteMoveTarget);
}

void PopupWindow::PasteItemKeepOpen(const ClipboardItem& item) {
    WriteToClipboard(item);
    RestoreFocusAndPaste();
}

void PopupWindow::PasteQueue() {
    ClipboardHistory* hist = Application::Get()->GetHistory();
    if (!hist) return;

    std::vector<uint64_t> ids = m_queue;
    Hide();

    for (size_t qi = 0; qi < ids.size(); ++qi) {
        ClipboardItem item;
        if (!hist->GetByIdCopy(ids[qi], item)) continue;
        WriteToClipboard(item);
        RestoreFocusAndPaste();
        hist->MoveItemById(item.id, m_pasteMoveTarget);
        if (qi + 1 < ids.size() && m_queueDelayMs > 0)
            Sleep(static_cast<DWORD>(m_queueDelayMs));
    }
}

void PopupWindow::LaunchWebSearch() {
    LaunchWebSearchForText(m_searchBuf, true);
}

void PopupWindow::LaunchClipboardWebSearch() {
    LaunchWebSearchForText(win32util::ClipboardUnicodeText(), m_visible);
}

bool PopupWindow::LaunchWebSearchForText(const std::string& text, bool hideOnSuccess) {
    const std::string query = TrimAscii(text);
    if (query.empty())
        return false;

    const std::wstring url = L"https://www.google.com/search?q=" + UrlEncodeWide(query);
    HINSTANCE result = ShellExecuteW(nullptr, L"open", url.c_str(),
                                     nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32) {
        if (hideOnSuccess)
            Hide();
        return true;
    }
    return false;
}

void PopupWindow::WriteToClipboard(const ClipboardItem& item) const {
    if (Application::Get() && Application::Get()->GetMonitor())
        Application::Get()->GetMonitor()->SuppressNextUpdate();

    std::string text = item.text;
    const bool isFileDrop = item.type == ContentType::FilePaths || (item.tags & TAG_PATH) != 0;
    if (isFileDrop) {
        std::vector<std::wstring> filePaths = win32util::ExistingPathListUtf8(text);
        if (!filePaths.empty() && win32util::SetClipboardFileDrop(nullptr, filePaths))
            return;
    }

    if (m_appendNewlineAfterPaste && !isFileDrop)
        text += "\r\n";

    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();

    if (item.type == ContentType::Image && !item.imageStoreId.empty()) {
        Application* app = Application::Get();
        ImageStore* store = app ? app->GetImageStore() : nullptr;
        if (store) {
            HGLOBAL hDib = store->GetDibForPaste(item.imageStoreId);
            if (hDib)
                SetClipboardData(CF_DIB, hDib);
        }
    } else {
        int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                        text.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            HGLOBAL hm = GlobalAlloc(GMEM_MOVEABLE,
                                      static_cast<SIZE_T>(wlen) * sizeof(wchar_t));
            if (hm) {
                MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1,
                                    static_cast<wchar_t*>(GlobalLock(hm)), wlen);
                GlobalUnlock(hm);
                SetClipboardData(CF_UNICODETEXT, hm);
            }
        }
    }
    CloseClipboard();
}

HWND PopupWindow::ResolvePasteTarget() const {
    HWND fg = GetForegroundWindow();
    HWND main = Application::Get() ? Application::Get()->GetHwnd() : nullptr;

    if (fg && fg != m_hwnd && fg != main)
        return fg;
    if (m_prevForeground && IsWindow(m_prevForeground))
        return m_prevForeground;
    return nullptr;
}

bool PopupWindow::WaitForForeground(HWND target, DWORD timeoutMs) const {
    if (!target) return false;

    const DWORD started = GetTickCount();
    while (GetForegroundWindow() != target) {
        if (GetTickCount() - started >= timeoutMs)
            return false;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
    return true;
}

void PopupWindow::RestoreFocusAndPaste(HWND preferredTarget) {
    HWND target = preferredTarget ? preferredTarget : ResolvePasteTarget();
    if (target && IsWindow(target)) {
        if (GetForegroundWindow() != target) {
            SetForegroundWindow(target);
            WaitForForeground(target, 100);
        }
    }

    // All injected events carry kClipboardPasteMagic so our LL hook ignores them.
    const bool ctrlDown  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shiftDown = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
    const bool altDown   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

    INPUT in[10]{};
    for (auto& i : in) {
        i.type = INPUT_KEYBOARD;
        i.ki.dwExtraInfo = kClipboardPasteMagic;
    }

    int n = 0;
    if (altDown) {
        in[n].ki.wVk = VK_MENU;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        ++n;
    }
    if (shiftDown) {
        in[n].ki.wVk = VK_SHIFT;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        ++n;
    }
    if (!ctrlDown) {
        in[n].ki.wVk = VK_CONTROL;
        ++n;
    }

    in[n].ki.wVk = 'V';
    ++n;
    in[n].ki.wVk = 'V';
    in[n].ki.dwFlags = KEYEVENTF_KEYUP;
    ++n;

    if (!ctrlDown) {
        in[n].ki.wVk = VK_CONTROL;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        ++n;
    }
    if (shiftDown) {
        in[n].ki.wVk = VK_SHIFT;
        ++n;
    }
    if (altDown) {
        in[n].ki.wVk = VK_MENU;
        ++n;
    }

    SendInput(static_cast<UINT>(n), in, sizeof(INPUT));
}

// -- D3D11 swap chain ----------------------------------------------------------

bool PopupWindow::CreateSwapChain() {
    // Get DXGI factory from the existing device so we share the same adapter
    IDXGIDevice*  dxgiDev  = nullptr;
    IDXGIAdapter* adapter  = nullptr;
    IDXGIFactory* factory  = nullptr;

    if (FAILED(m_device->QueryInterface(__uuidof(IDXGIDevice),
                                         reinterpret_cast<void**>(&dxgiDev))))
        return false;
    dxgiDev->GetAdapter(&adapter);
    adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const UINT clientWidth = std::max<UINT>(1, static_cast<UINT>(rc.right - rc.left));
    const UINT clientHeight = std::max<UINT>(1, static_cast<UINT>(rc.bottom - rc.top));

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = clientWidth;
    sd.BufferDesc.Height                  = clientHeight;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = m_hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT hr = factory->CreateSwapChain(m_device, &sd, &m_swapChain);

    factory->Release();
    adapter->Release();
    dxgiDev->Release();

    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

void PopupWindow::ResizeSwapChainToClient() {
    if (!m_swapChain) return;

    RECT rc{};
    if (!GetClientRect(m_hwnd, &rc)) return;

    const UINT width = std::max<UINT>(1, static_cast<UINT>(rc.right - rc.left));
    const UINT height = std::max<UINT>(1, static_cast<UINT>(rc.bottom - rc.top));

    DestroyRenderTarget();
    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height,
                                            DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr))
        CreateRenderTarget();
}

void PopupWindow::DestroySwapChain() {
    DestroyRenderTarget();
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
}

void PopupWindow::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_device->CreateRenderTargetView(back, nullptr, &m_renderTarget);
        back->Release();
    }
}

void PopupWindow::DestroyRenderTarget() {
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }
}

// -- Helpers -------------------------------------------------------------------

void PopupWindow::PositionAtCursor() {
    POINT pt{};
    GetCursorPos(&pt);
    float x = static_cast<float>(pt.x);
    float y = static_cast<float>(pt.y);

    HMONITOR    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(mon, &mi)) {
        if (x + m_width  > mi.rcWork.right)  x = static_cast<float>(mi.rcWork.right  - m_width);
        if (y + m_height > mi.rcWork.bottom) y = static_cast<float>(mi.rcWork.bottom - m_height);
        if (x < mi.rcWork.left) x = static_cast<float>(mi.rcWork.left);
        if (y < mi.rcWork.top)  y = static_cast<float>(mi.rcWork.top);
    }
    SetWindowPos(m_hwnd, HWND_TOPMOST,
                  static_cast<int>(x), static_cast<int>(y),
                  m_width, m_height,
                  SWP_NOACTIVATE);
}

void PopupWindow::ApplyOpacity() {
    BYTE alpha = static_cast<BYTE>(m_opacity * 255.0f);
    SetLayeredWindowAttributes(m_hwnd, 0, alpha, LWA_ALPHA);
}

void PopupWindow::ApplyWindowCorners() {
    // ImGui rounds only its own drawing. DWM owns the native HWND corners.
    // Windows chooses the exact radius; custom pixel radii require region/per-pixel masks.
    const int pref = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &pref, sizeof(pref));

    const COLORREF noBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR,
                          &noBorder, sizeof(noBorder));

    if (m_maximized) {
        SetWindowRgn(m_hwnd, nullptr, TRUE);
        return;
    }

    RECT rc{};
    if (!GetClientRect(m_hwnd, &rc))
        return;

    const int width = std::max(1, static_cast<int>(rc.right - rc.left));
    const int height = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const float scale = EffectiveUiScale(m_appearance);
    const int radius = static_cast<int>(std::lround(
        std::clamp(m_appearance.popupRounding, 0.0f, 48.0f) * scale * 2.0f));

    if (radius <= 1) {
        SetWindowRgn(m_hwnd, nullptr, TRUE);
        return;
    }

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius);
    if (region && SetWindowRgn(m_hwnd, region, TRUE) == 0)
        DeleteObject(region);
}

// -- Win32 message handler -----------------------------------------------------

void PopupWindow::ToggleMaximized() {
    if (m_maximized) {
        SetWindowPos(m_hwnd, HWND_TOPMOST,
                     m_restoreRect.left, m_restoreRect.top,
                     m_restoreRect.right - m_restoreRect.left,
                     m_restoreRect.bottom - m_restoreRect.top,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        m_maximized = false;
        ResizeSwapChainToClient();
        ApplyWindowCorners();
        return;
    }

    if (!GetWindowRect(m_hwnd, &m_restoreRect))
        return;

    HMONITOR mon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(mon, &mi))
        return;

    const RECT& r = mi.rcWork;
    SetWindowPos(m_hwnd, HWND_TOPMOST,
                 r.left, r.top,
                 r.right - r.left,
                 r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    m_maximized = true;
    ResizeSwapChainToClient();
    ApplyWindowCorners();
}

LRESULT CALLBACK PopupWindow::WndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* pw = reinterpret_cast<PopupWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    // Switch to the popup's own ImGui context so WM_CHAR / WM_KEYDOWN from
    // the keyboard hook land in the right input queue (search bar).
    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    if (pw && pw->m_imguiCtx)
        ImGui::SetCurrentContext(pw->m_imguiCtx);

    bool imgHandled = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0;

    if (pw && pw->m_imguiCtx)
        ImGui::SetCurrentContext(prevCtx);

    if (imgHandled) return TRUE;

    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE)
            return 0;
        break;

    case WM_NCPAINT:
        return 0;

    case WM_NCACTIVATE:
        return TRUE;

    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        RECT rc{};
        GetClientRect(hwnd, &rc);

        const bool onL = pt.x < kPopupResizeBorder;
        const bool onR = pt.x >= rc.right - kPopupResizeBorder;
        const bool onT = pt.y < kPopupResizeBorder;
        const bool onB = pt.y >= rc.bottom - kPopupResizeBorder;

        if (onT && onL) return HTTOPLEFT;
        if (onT && onR) return HTTOPRIGHT;
        if (onB && onL) return HTBOTTOMLEFT;
        if (onB && onR) return HTBOTTOMRIGHT;
        if (onL)        return HTLEFT;
        if (onR)        return HTRIGHT;
        if (onT)        return HTTOP;
        if (onB)        return HTBOTTOM;

        return HTCLIENT;
    }

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        const float dpiScale = win32util::DpiScaleForWindow(hwnd);
        mmi->ptMinTrackSize = {
            static_cast<LONG>(std::lround(360.0f * dpiScale)),
            static_cast<LONG>(std::lround(260.0f * dpiScale))
        };
        return 0;
    }

    case WM_SIZE:
        if (pw && pw->m_swapChain && wParam != SIZE_MINIMIZED) {
            pw->m_width = LOWORD(lParam);
            pw->m_height = HIWORD(lParam);
            pw->ResizeSwapChainToClient();
            pw->ApplyWindowCorners();
        }
        return 0;

    case WM_DPICHANGED:
        if (pw) {
            pw->m_appearance.dpiScale = win32util::DpiScaleForWindow(hwnd);
            ApplyThemeStyle(pw->m_appearance, true);
            if (RECT* suggested = reinterpret_cast<RECT*>(lParam)) {
                SetWindowPos(hwnd, HWND_TOPMOST,
                             suggested->left,
                             suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            }
            pw->ResizeSwapChainToClient();
            pw->ApplyWindowCorners();
        }
        return 0;

    // WS_EX_NOACTIVATE stops Windows from sending paint messages during the
    // internal move/resize modal loop, so our D3D frames stop until mouse-up.
    // Fix: run a ~60fps timer during the modal loop and render each tick.
    case WM_ENTERSIZEMOVE:
        SetTimer(hwnd, kPopupResizeRenderTimerId, 16, nullptr);
        return 0;

    case WM_EXITSIZEMOVE:
        KillTimer(hwnd, kPopupResizeRenderTimerId);
        return 0;

    case WM_TIMER:
        if (pw && wParam == kPopupResizeRenderTimerId) pw->Render();
        return 0;

    case WM_KILLFOCUS:
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
