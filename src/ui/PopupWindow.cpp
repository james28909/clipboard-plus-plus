#include "PopupWindow.h"
#include "../app/Application.h"
#include "../clipboard/ImageStore.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ClipboardMonitor.h"
#include "../clipboard/ContentDetector.h"
#include "../util/Win32Util.h"
#include "Appearance.h"
#include "ImGuiWidgets.h"
#include "PasteDiagnostics.h"

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

using ImGuiWidgets::SmoothScrollCurrentWindow;
#include "ToastWindow.h"

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
#ifndef DWMWA_COLOR_DEFAULT
#define DWMWA_COLOR_DEFAULT 0xFFFFFFFF
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

static bool BlueButton(const char* label, ImVec2 size = {0.0f, 0.0f}) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(77, 145, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(106, 166, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(46, 112, 220, 255));
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
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

static void PLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char msg[512]{};
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    PasteDiagnostics::Log("%s", msg);
}

// Returns "0xHWND(\"Window Title\")" for readable log output.
static std::string WH(HWND h) {
    if (!h) return "null";
    char t[64]{}; GetWindowTextA(h, t, sizeof(t));
    char b[100]; snprintf(b, sizeof(b), "0x%llX(\"%s\")",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h)), t[0] ? t : "?");
    return b;
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
    ApplyDwmFrameSettings();

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
    InvalidateWindowRegion();
    const float dpiScale = EffectiveUiScale(m_appearance);
    m_width = static_cast<int>(std::lround(settings.popupWidth * dpiScale));
    m_height = static_cast<int>(std::lround(settings.popupHeight * dpiScale));
    ApplyOpacity();
    if (m_visible) {
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, m_width, m_height,
                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        ResizeSwapChainToClient();
        ApplyWindowRegion();
    }

    ImGui::SetCurrentContext(prevCtx);
}

void PopupWindow::Destroy() {
    StopPasteTargetTracking();
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
    HWND foreground = GetForegroundWindow();
    m_activePasteTarget = nullptr;
    NotePasteTarget(foreground, "show");
    PLog("[POPUP-SHOW] fg=%s prevFg=%s focusSearch=%d focusTest=%d",
         WH(foreground).c_str(), WH(m_prevForeground).c_str(), focusSearch, m_focusTestMode);
    PositionAtCursor();
    ResizeSwapChainToClient();
    ApplyWindowRegion();
    ApplyOpacity();

    ShowWindow(m_hwnd, m_focusTestMode ? SW_SHOW : SW_SHOWNA);
    m_visible           = true;
    m_justOpened        = true;
    m_focusSearchOnOpen = focusSearch;
    m_searchActive      = false;
    m_searchCapture     = focusSearch;
    m_keyboardCapture   = true;
    m_maximized         = false;
    ClearItemSelection();
    m_selectedImageIds.clear();
    m_imgSelectionAnchorId.clear();
    std::memset(m_searchBuf, 0, sizeof(m_searchBuf));
    m_imageListDirty = true;   // refresh image list on next open
    StartPasteTargetTracking();
}

void PopupWindow::Hide() {
    PLog("[POPUP-HIDE]");
    StopPasteTargetTracking();
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
    InvalidateWindowRegion();
    m_activePasteTarget = nullptr;
    ClearItemSelection();
    m_selectedImageIds.clear();
    m_imgSelectionAnchorId.clear();
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

    // Escape clears an active paste selection first, then closes on the next press.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (HasMultipleSelectedItems()) {
            ClearSelectedItems();
            ImGui::End(); ImGui::Render();
            ImGui::SetCurrentContext(prevCtx);
            return;
        }
        ImGui::End(); ImGui::Render();
        ImGui::SetCurrentContext(prevCtx);
        Hide();
        return;
    }

    DrawSearchBar();
    ImGui::Spacing();
    DrawFilterStrip();
    ImGui::Separator();
    if (m_androidPanelOpen)
        DrawAndroidPanel();
    else if (m_namedSlotsPanelOpen)
        DrawNamedSlots();
    else {
        m_lastAndroidPanelOpen = false;
        DrawItemList();
    }

    AppearanceSettings effective = m_appearance.customColors
        ? m_appearance
        : ThemeDefaults(m_appearance.theme);
    effective.popupOutlineEffect = m_appearance.popupOutlineEffect;
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
        const int outlineEffect = std::clamp(effective.popupOutlineEffect, 0, 3);
        const bool dynamicOutline = outlineEffect != 0 || effective.popupOutlineAnimated;
        auto outlineColor = [&](float offset, float alpha) {
            if (!dynamicOutline || outlineEffect == 2) {
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
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) *
                                                    std::clamp(effective.popupOutlineAnimationSpeed, 0.05f, 5.0f) *
                                                    4.0f);
        const float glowAlpha = outlineEffect == 2
            ? strength * (0.18f + 0.28f * pulse)
            : strength * 0.26f;
        const float softAlpha = outlineEffect == 2
            ? strength * (0.26f + 0.36f * pulse)
            : strength * 0.38f;
        ImVec4 glow = outlineColor(0.0f, glowAlpha);
        glow.w = glowAlpha;
        ImVec4 soft = outlineColor(0.08f, softAlpha);
        dl->AddRect({pos.x + 1.0f, pos.y + 1.0f}, {max.x - 1.0f, max.y - 1.0f},
                    ImGui::GetColorU32(glow), rounding, 0,
                    1.0f + strength * (outlineEffect == 2 ? 5.8f + 2.0f * pulse : 4.5f));
        dl->AddRect({pos.x + 2.0f, pos.y + 2.0f}, {max.x - 2.0f, max.y - 2.0f},
                    ImGui::GetColorU32(soft), rounding, 0,
                    0.75f + strength * (outlineEffect == 2 ? 3.0f + 1.2f * pulse : 2.5f));
        if (dynamicOutline && outlineEffect != 2) {
            const float thick = 0.75f + strength * 1.35f;
            const ImVec2 a{pos.x + 0.5f, pos.y + 0.5f};
            const ImVec2 b{max.x - 0.5f, max.y - 0.5f};
            const float w = std::max(1.0f, b.x - a.x);
            const float h = std::max(1.0f, b.y - a.y);
            const float r = std::clamp(rounding, 0.0f, std::min(w, h) * 0.5f);
            const float pi = 3.14159265358979323846f;
            const float targetStep = 10.0f;

            std::vector<ImVec2> ring;
            ring.reserve(96);

            auto addPoint = [&](ImVec2 p) {
                if (ring.empty() ||
                    std::fabs(ring.back().x - p.x) > 0.01f ||
                    std::fabs(ring.back().y - p.y) > 0.01f) {
                    ring.push_back(p);
                }
            };
            auto addLineSamples = [&](ImVec2 p0, ImVec2 p1) {
                const float dx = p1.x - p0.x;
                const float dy = p1.y - p0.y;
                const int steps = std::max(1, static_cast<int>(std::ceil(std::hypot(dx, dy) / targetStep)));
                for (int i = 0; i <= steps; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(steps);
                    addPoint({p0.x + dx * t, p0.y + dy * t});
                }
            };
            auto addArcSamples = [&](ImVec2 center, float from, float to) {
                const int steps = std::max(3, static_cast<int>(std::ceil((std::abs(to - from) * r) / targetStep)));
                for (int i = 1; i <= steps; ++i) {
                    const float t = static_cast<float>(i) / static_cast<float>(steps);
                    const float angle = from + (to - from) * t;
                    addPoint({center.x + std::cos(angle) * r,
                              center.y + std::sin(angle) * r});
                }
            };

            addLineSamples({a.x + r, a.y}, {b.x - r, a.y});
            addArcSamples({b.x - r, a.y + r}, -pi * 0.5f, 0.0f);
            addLineSamples({b.x, a.y + r}, {b.x, b.y - r});
            addArcSamples({b.x - r, b.y - r}, 0.0f, pi * 0.5f);
            addLineSamples({b.x - r, b.y}, {a.x + r, b.y});
            addArcSamples({a.x + r, b.y - r}, pi * 0.5f, pi);
            addLineSamples({a.x, b.y - r}, {a.x, a.y + r});
            addArcSamples({a.x + r, a.y + r}, pi, pi * 1.5f);

            if (ring.size() >= 2) {
                float perimeter = 0.0f;
                std::vector<float> distance(ring.size() + 1, 0.0f);
                for (size_t i = 0; i < ring.size(); ++i) {
                    const ImVec2 p0 = ring[i];
                    const ImVec2 p1 = ring[(i + 1) % ring.size()];
                    perimeter += std::hypot(p1.x - p0.x, p1.y - p0.y);
                    distance[i + 1] = perimeter;
                }

                if (perimeter > 0.001f) {
                    const float direction = effective.popupOutlineReverse ? -1.0f : 1.0f;
                    const float speed = std::clamp(effective.popupOutlineAnimationSpeed, 0.05f, 5.0f);
                    const float chaseHead = std::fmod(static_cast<float>(ImGui::GetTime()) *
                                                      0.16f * speed * direction, 1.0f);
                    const float chaseTail = std::max(0.04f,
                        0.08f + std::clamp(effective.popupOutlineColorSpread, 0.0f, 2.0f) * 0.18f);
                    const float chasePower = 1.0f + std::clamp(effective.popupOutlineColorSharpness, 0.0f, 1.0f) * 5.0f;
                    for (size_t i = 0; i < ring.size(); ++i) {
                        const float t = (distance[i] + (distance[i + 1] - distance[i]) * 0.5f) / perimeter;
                        float alpha = strength * 0.95f;
                        if (outlineEffect == 3) {
                            float d = std::fabs(t - chaseHead);
                            d = std::min(d, 1.0f - d);
                            alpha *= std::pow(std::max(0.0f, 1.0f - d / chaseTail), chasePower);
                            alpha += strength * 0.10f;
                        }
                        dl->AddLine(ring[i], ring[(i + 1) % ring.size()],
                                    ImGui::GetColorU32(outlineColor(t, std::clamp(alpha, 0.0f, 1.0f))),
                                    outlineEffect == 3 ? thick + strength * 0.9f : thick);
                    }
                }
            }
        } else {
            dl->AddRect({pos.x + 0.5f, pos.y + 0.5f}, {max.x - 0.5f, max.y - 0.5f},
                        ImGui::GetColorU32(outlineColor(0.0f,
                            outlineEffect == 2 ? strength * (0.55f + 0.40f * pulse) : strength * 0.92f)),
                        rounding, 0,
                        0.75f + strength * (outlineEffect == 2 ? 1.4f + 0.9f * pulse : 1.0f));
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

    // Profile context menu - parented to ##popup so input routing works correctly
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

    // Delete confirmation modal - parented to the main ##popup window
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
        if (BlueButton("Save", {saveW, 0.0f}) && app) {
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
    Application* app = Application::Get();

    struct BuiltInFilter { const char* token; const char* label; int mode; };
    static constexpr BuiltInFilter kBuiltIns[] = {
        {"filter:all", "All", 0},       {"filter:text", "Text", 1},
        {"filter:url", "URL", 3},       {"filter:file", "File", 4},
        {"filter:code", "Code", 5},     {"filter:secret", "Secret", 6},
        {"filter:json", "JSON", 7},     {"filter:email", "Email", 8},
        {"filter:color", "Color", 9},   {"filter:cmd", "CMD", 10},
        {"filter:image", "Image", 2},
    };
    static constexpr const char* kDefaultOrder[] = {
        "filter:all", "filter:text", "filter:url", "filter:file", "filter:code",
        "filter:secret", "filter:json", "filter:email", "filter:color", "filter:cmd",
        "break",
        "filter:image", "action:android", "action:slots",
        "action:newline", "action:move", "action:settings",
    };
    const float buttonColumnGap = std::clamp(m_appearance.popupButtonColumnPadding, 0.0f, 16.0f);
    const float buttonRowGap = std::clamp(m_appearance.popupButtonRowPadding, 0.0f, 12.0f);

    auto isBuiltIn = [&](const std::string& token) {
        return std::find_if(std::begin(kBuiltIns), std::end(kBuiltIns),
            [&](const BuiltInFilter& item) { return token == item.token; }) != std::end(kBuiltIns);
    };
    auto customForToken = [&](const std::string& token) -> const CustomFilter* {
        if (!app || token.rfind("custom:", 0) != 0)
            return nullptr;
        const std::string id = token.substr(7);
        const auto& filters = app->GetCustomFilters();
        auto it = std::find_if(filters.begin(), filters.end(),
            [&](const CustomFilter& filter) {
                return filter.id == id && filter.enabled && !filter.pattern.empty();
            });
        return it == filters.end() ? nullptr : &*it;
    };
    auto isAction = [&](const std::string& token) {
        return token == "action:newline" || token == "action:move" ||
               token == "action:android" || token == "action:slots" ||
               token == "action:settings";
    };
    auto cleanupBreaks = [](std::vector<std::string>& order) {
        order.erase(std::remove_if(order.begin(), order.end(), [](const std::string& token) {
            return token.empty();
        }), order.end());
        std::vector<std::string> cleaned;
        bool lastBreak = true;
        for (const std::string& token : order) {
            if (token == "break") {
                if (!lastBreak)
                    cleaned.push_back(token);
                lastBreak = true;
            } else {
                cleaned.push_back(token);
                lastBreak = false;
            }
        }
        while (!cleaned.empty() && cleaned.back() == "break")
            cleaned.pop_back();
        order = std::move(cleaned);
    };

    std::vector<std::string> order = app ? app->GetPopupButtonOrder() : std::vector<std::string>{};
    if (order.empty())
        order.assign(std::begin(kDefaultOrder), std::end(kDefaultOrder));

    std::vector<std::string> normalized;
    auto addUnique = [&](const std::string& token) {
        if (token == "break") {
            normalized.push_back(token);
            return;
        }
        if (std::find(normalized.begin(), normalized.end(), token) == normalized.end())
            normalized.push_back(token);
    };
    for (const std::string& token : order) {
        if (token == "break" || isBuiltIn(token) || isAction(token) || customForToken(token))
            addUnique(token);
    }
    for (const BuiltInFilter& item : kBuiltIns)
        addUnique(item.token);
    static constexpr const char* kActionTokens[] = {
        "action:android", "action:slots", "action:newline", "action:move", "action:settings"
    };
    for (const char* token : kActionTokens)
        addUnique(token);
    if (app) {
        for (const CustomFilter& filter : app->GetCustomFilters())
            if (filter.enabled && !filter.pattern.empty())
                addUnique("custom:" + filter.id);
    }
    cleanupBreaks(normalized);
    if (app && normalized != app->GetPopupButtonOrder())
        app->SetPopupButtonOrder(normalized);
    order = normalized;

    struct PendingPopupButtonMove {
        std::string dragged;
        std::string target;
        bool below{false};
        bool after{false};
        bool requested{false};
    } pendingMove;

    auto moveToken = [&](const std::string& dragged, const std::string& target, bool below, bool after) {
        if (!app || dragged.empty() || target.empty() || dragged == "break")
            return;

        std::vector<std::string> next = order;
        next.erase(std::remove(next.begin(), next.end(), dragged), next.end());
        cleanupBreaks(next);

        auto targetIt = std::find(next.begin(), next.end(), target);
        size_t insertAt = targetIt == next.end()
            ? next.size()
            : static_cast<size_t>(std::distance(next.begin(), targetIt)) + (after ? 1u : 0u);

        if (below) {
            if (targetIt != next.end())
                insertAt = static_cast<size_t>(std::distance(next.begin(), targetIt)) + 1u;
            if (insertAt > next.size())
                insertAt = next.size();
            if (insertAt == next.size() || next[insertAt] != "break")
                next.insert(next.begin() + static_cast<std::ptrdiff_t>(insertAt), "break");
            ++insertAt;
        }

        if (insertAt > next.size())
            insertAt = next.size();
        next.insert(next.begin() + static_cast<std::ptrdiff_t>(insertAt), dragged);
        cleanupBreaks(next);
        app->SetPopupButtonOrder(next);
        order = std::move(next);
    };

    auto requestMove = [&](std::string dragged, std::string target, bool below, bool after) {
        if (dragged.empty() || target.empty())
            return;
        if (dragged == target && !below)
            return;
        pendingMove.dragged = std::move(dragged);
        pendingMove.target = std::move(target);
        pendingMove.below = below;
        pendingMove.after = after;
        pendingMove.requested = true;
    };

    auto attachDragSource = [&](const std::string& token, const char* label) {
        if (token == "break")
            return;
        if (ImGui::BeginDragDropSource()) {
            char payload[128]{};
            strncpy_s(payload, token.c_str(), _TRUNCATE);
            ImGui::SetDragDropPayload("CPP_POPUP_BUTTON", payload, sizeof(payload));
            ImGui::TextUnformatted(label);
            ImGui::EndDragDropSource();
        }
    };

    auto acceptPopupButtonPayload = [&](std::string& dragged) -> const ImGuiPayload* {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
            "CPP_POPUP_BUTTON",
            ImGuiDragDropFlags_AcceptBeforeDelivery |
            ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (!payload)
            return nullptr;
        const char* data = static_cast<const char*>(payload->Data);
        dragged = data ? data : "";
        return payload;
    };

    auto drawBetweenDropZone = [&](const char* id, const std::string& target, bool after) {
        if (target.empty())
            return;
        ImGui::PushID(id);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##popup_between_drop", {buttonColumnGap + 4.0f, ImGui::GetFrameHeight()});
        if (ImGui::BeginDragDropTarget()) {
            std::string dragged;
            if (const ImGuiPayload* payload = acceptPopupButtonPayload(dragged)) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                dl->AddRectFilled({(a.x + b.x) * 0.5f - 1.0f, a.y + 2.0f},
                                  {(a.x + b.x) * 0.5f + 1.0f, b.y - 2.0f},
                                  IM_COL32(77, 145, 255, 150), 2.0f);
                if (payload->IsDelivery())
                    requestMove(dragged, target, false, after);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PopID();
    };

    auto drawRowDropZone = [&](const char* id, const std::string& target) {
        if (target.empty())
            return;
        static std::string armedTarget;
        static double armedAt = 0.0;
        static constexpr double kRowDropDwellSeconds = 0.25;

        ImGui::PushID(id);
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##popup_row_drop", {std::max(24.0f, width), buttonRowGap + 8.0f});
        if (ImGui::BeginDragDropTarget()) {
            std::string dragged;
            if (const ImGuiPayload* payload = acceptPopupButtonPayload(dragged)) {
                const std::string armKey = dragged + "->" + target;
                if (armedTarget != armKey) {
                    armedTarget = armKey;
                    armedAt = ImGui::GetTime();
                }

                const bool armed = (ImGui::GetTime() - armedAt) >= kRowDropDwellSeconds;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                const ImU32 color = armed
                    ? IM_COL32(77, 145, 255, 150)
                    : IM_COL32(77, 145, 255, 60);
                dl->AddRectFilled({a.x, (a.y + b.y) * 0.5f - 1.0f},
                                  {b.x, (a.y + b.y) * 0.5f + 1.0f},
                                  color, 2.0f);

                if (payload->IsDelivery() && armed)
                    requestMove(dragged, target, true, true);
            }
            ImGui::EndDragDropTarget();
        } else if (armedTarget.rfind("->" + target) != std::string::npos) {
            armedTarget.clear();
            armedAt = 0.0;
        }
        ImGui::PopID();
    };

    struct PopupButtonRect {
        std::string token;
        std::string label;
        ImVec2 min;
        ImVec2 max;
        int row{};
    };
    std::vector<PopupButtonRect> buttonRects;
    int drawingRow = 0;

    auto recordButtonRect = [&](const std::string& token, const char* label) {
        PopupButtonRect rect;
        rect.token = token;
        rect.label = label ? label : "";
        rect.min = ImGui::GetItemRectMin();
        rect.max = ImGui::GetItemRectMax();
        rect.row = drawingRow;
        buttonRects.push_back(std::move(rect));
    };

    auto drawToken = [&](const std::string& token) -> bool {
        if (token == "break") {
            ImGui::NewLine();
            return true;
        }

        for (const BuiltInFilter& item : kBuiltIns) {
            if (token != item.token)
                continue;
            const bool active = m_activeCustomFilterId.empty() && m_filterMode == item.mode;
            ImGui::PushID(token.c_str());
            if (PopupToggleButton(item.label, active, toggleColors)) {
                ActivateKeyboardCapture();
                m_androidPanelOpen = false;
                m_namedSlotsPanelOpen = false;
                m_filterMode = item.mode;
                m_activeCustomFilterId.clear();
            }
            recordButtonRect(token, item.label);
            attachDragSource(token, item.label);
            ImGui::PopID();
            return true;
        }

        if (const CustomFilter* filter = customForToken(token)) {
            ImGui::PushID(token.c_str());
            const bool active = m_activeCustomFilterId == filter->id;
            if (PopupToggleButton(filter->name.c_str(), active, toggleColors)) {
                ActivateKeyboardCapture();
                m_androidPanelOpen = false;
                m_namedSlotsPanelOpen = false;
                m_filterMode = 0;
                m_activeCustomFilterId = filter->id;
            }
            recordButtonRect(token, filter->name.c_str());
            attachDragSource(token, filter->name.c_str());
            ImGui::PopID();
            return true;
        }

        ImGui::PushID(token.c_str());
        if (token == "action:newline") {
            if (PopupToggleButton("Newline", m_appendNewlineAfterPaste, toggleColors)) {
                ActivateKeyboardCapture();
                m_appendNewlineAfterPaste = !m_appendNewlineAfterPaste;
            }
            recordButtonRect(token, "Newline");
            attachDragSource(token, "Newline");
            ImGui::PopID();
            return true;
        }
        if (token == "action:android") {
            if (PopupToggleButton("Android", m_androidPanelOpen, toggleColors)) {
                ActivateKeyboardCapture();
                m_androidPanelOpen = !m_androidPanelOpen;
                if (m_androidPanelOpen)
                    m_namedSlotsPanelOpen = false;
            }
            recordButtonRect(token, "Android");
            attachDragSource(token, "Android");
            ImGui::PopID();
            return true;
        }
        if (token == "action:slots") {
            if (PopupToggleButton("Slots", m_namedSlotsPanelOpen, toggleColors)) {
                ActivateKeyboardCapture();
                m_namedSlotsPanelOpen = !m_namedSlotsPanelOpen;
                if (m_namedSlotsPanelOpen)
                    m_androidPanelOpen = false;
            }
            recordButtonRect(token, "Slots");
            attachDragSource(token, "Slots");
            ImGui::PopID();
            return true;
        }
        if (token == "action:move") {
            const bool moveActive = m_pasteMoveTarget != ClipboardHistory::MoveTarget::None;
            if (PopupToggleButton(PasteMoveLabel(m_pasteMoveTarget), moveActive, toggleColors)) {
                ActivateKeyboardCapture();
                m_pasteMoveTarget = NextPasteMoveTarget(m_pasteMoveTarget);
            }
            recordButtonRect(token, PasteMoveLabel(m_pasteMoveTarget));
            attachDragSource(token, PasteMoveLabel(m_pasteMoveTarget));
            ImGui::PopID();
            return true;
        }
        if (token == "action:settings") {
            if (ImGui::SmallButton(" @ "))
                OpenSettingsWindow();
            recordButtonRect(token, "Settings");
            attachDragSource(token, "Settings");
            ImGui::PopID();
            return true;
        }
        ImGui::PopID();
        return false;
    };

    bool rowHasVisibleItem = false;
    std::string lastVisibleToken;
    std::string firstVisibleToken;
    int rowIndex = 0;
    for (const std::string& token : order) {
        if (token == "break") {
            if (rowHasVisibleItem) {
                ImGui::NewLine();
                ImGui::Dummy({1.0f, buttonRowGap});
                ++rowIndex;
                drawingRow = rowIndex;
            }
            rowHasVisibleItem = false;
            lastVisibleToken.clear();
            firstVisibleToken.clear();
            continue;
        }
        drawingRow = rowIndex;
        const bool drawn = drawToken(token);
        if (drawn) {
            if (!rowHasVisibleItem)
                firstVisibleToken = token;
            rowHasVisibleItem = true;
            lastVisibleToken = token;
            ImGui::SameLine(0.0f, buttonColumnGap);
        }
    }
    if (rowHasVisibleItem) {
        ImGui::NewLine();
        ImGui::Dummy({1.0f, buttonRowGap});
    }
    const bool popupButtonDragActive = ImGui::GetDragDropPayload() != nullptr;

    const ImVec2 afterStripCursor = ImGui::GetCursorScreenPos();
    ImVec2 finalStripCursor = afterStripCursor;
    if (!buttonRects.empty() && popupButtonDragActive) {
        ImVec2 stripMin = buttonRects.front().min;
        ImVec2 stripMax = buttonRects.front().max;
        for (const PopupButtonRect& rect : buttonRects) {
            stripMin.x = std::min(stripMin.x, rect.min.x);
            stripMin.y = std::min(stripMin.y, rect.min.y);
            stripMax.x = std::max(stripMax.x, rect.max.x);
            stripMax.y = std::max(stripMax.y, rect.max.y);
        }
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool nearBottomRowDrop = mouse.y >= stripMax.y - 4.0f &&
                                       mouse.y <= stripMax.y + ImGui::GetFrameHeight() + 18.0f;
        if (nearBottomRowDrop) {
            ImGui::SetCursorScreenPos(afterStripCursor);
            ImGui::Dummy({1.0f, ImGui::GetFrameHeight() + 14.0f});
            finalStripCursor = ImGui::GetCursorScreenPos();
        }

        ImGui::SetCursorScreenPos(stripMin);
        ImGui::InvisibleButton("##popup_button_drag_overlay",
                               {std::max(24.0f, stripMax.x - stripMin.x),
                                std::max(24.0f, stripMax.y - stripMin.y + 42.0f)});
        if (ImGui::BeginDragDropTarget()) {
            std::string dragged;
            if (const ImGuiPayload* payload = acceptPopupButtonPayload(dragged)) {
                static std::string bottomArmKey;
                static double bottomArmAt = 0.0;
                static constexpr double kBottomRowDwellSeconds = 0.25;

                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float bottomThreshold = stripMax.y - 4.0f;
                const bool wantsBottomRow = mouse.y >= bottomThreshold;
                ImDrawList* dl = ImGui::GetWindowDrawList();

                if (wantsBottomRow) {
                    const std::string armKey = dragged + "->bottom";
                    if (bottomArmKey != armKey) {
                        bottomArmKey = armKey;
                        bottomArmAt = ImGui::GetTime();
                    }
                    const bool armed = (ImGui::GetTime() - bottomArmAt) >= kBottomRowDwellSeconds;

                    const auto draggedIt = std::find_if(buttonRects.begin(), buttonRects.end(),
                        [&](const PopupButtonRect& rect) { return rect.token == dragged; });
                    const char* ghostLabel = draggedIt != buttonRects.end()
                        ? draggedIt->label.c_str()
                        : "Button";
                    const ImVec2 ghostPos{stripMin.x, stripMax.y + 10.0f};
                    const ImVec2 ghostText = ImGui::CalcTextSize(ghostLabel);
                    const ImVec2 ghostSize{
                        ghostText.x + ImGui::GetStyle().FramePadding.x * 2.0f,
                        ghostText.y + ImGui::GetStyle().FramePadding.y * 2.0f
                    };
                    const ImU32 fill = armed ? IM_COL32(77, 145, 255, 82) : IM_COL32(77, 145, 255, 42);
                    const ImU32 border = armed ? IM_COL32(77, 145, 255, 190) : IM_COL32(77, 145, 255, 110);
                    dl->AddRectFilled(ghostPos, {ghostPos.x + ghostSize.x, ghostPos.y + ghostSize.y},
                                      fill, ImGui::GetStyle().FrameRounding);
                    dl->AddRect(ghostPos, {ghostPos.x + ghostSize.x, ghostPos.y + ghostSize.y},
                                border, ImGui::GetStyle().FrameRounding, 0, 1.5f);
                    dl->AddText({ghostPos.x + ImGui::GetStyle().FramePadding.x,
                                 ghostPos.y + ImGui::GetStyle().FramePadding.y},
                                IM_COL32(230, 240, 255, armed ? 220 : 150), ghostLabel);

                    if (payload->IsDelivery() && armed)
                        requestMove(dragged, lastVisibleToken, true, true);
                } else {
                    bottomArmKey.clear();
                    bottomArmAt = 0.0;

                    struct SlotPreview {
                        std::string target;
                        bool after{false};
                        float x{};
                        float top{};
                        float bottom{};
                        float distance{FLT_MAX};
                    } best;

                    for (const PopupButtonRect& rect : buttonRects) {
                        const bool rowHit = mouse.y >= rect.min.y - 8.0f && mouse.y <= rect.max.y + 8.0f;
                        if (!rowHit)
                            continue;
                        const float beforeDistance = std::fabs(mouse.x - rect.min.x);
                        if (beforeDistance < best.distance) {
                            best = {rect.token, false, rect.min.x, rect.min.y, rect.max.y, beforeDistance};
                        }
                        const float afterDistance = std::fabs(mouse.x - rect.max.x);
                        if (afterDistance < best.distance) {
                            best = {rect.token, true, rect.max.x, rect.min.y, rect.max.y, afterDistance};
                        }
                    }

                    if (!best.target.empty()) {
                        dl->AddRectFilled({best.x - 1.5f, best.top - 2.0f},
                                          {best.x + 1.5f, best.bottom + 2.0f},
                                          IM_COL32(77, 145, 255, 180), 2.0f);
                        if (payload->IsDelivery())
                            requestMove(dragged, best.target, false, best.after);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SetCursorScreenPos(finalStripCursor);
    }

    if (pendingMove.requested)
        moveToken(pendingMove.dragged, pendingMove.target, pendingMove.below, pendingMove.after);
    ImGui::Spacing();
}

// -- Paste ---------------------------------------------------------------------

void PopupWindow::ReleaseSearchCapture() {
    m_searchCapture = false;
    m_searchActive = false;
    m_focusSearchOnOpen = false;
}

void PopupWindow::ActivateKeyboardCapture() {
    if (!m_keyboardCapture)
        PLog("[KB-CAPTURE] enabled (was false)");
    m_keyboardCapture = true;
}

void PopupWindow::ClearSelectedItems() {
    ClearItemSelection();
    ActivateKeyboardCapture();
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

void PopupWindow::ApplyDwmFrameSettings() {
    // ImGui draws the popup chrome; SetWindowRgn below owns the real HWND shape.
    const int pref = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &pref, sizeof(pref));

    const COLORREF noBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR,
                          &noBorder, sizeof(noBorder));
}

void PopupWindow::InvalidateWindowRegion() {
    m_regionCacheValid = false;
}

void PopupWindow::ApplyWindowRegion() {
    if (m_maximized) {
        if (m_regionCacheValid && m_lastRegionMaximized)
            return;

        SetWindowRgn(m_hwnd, nullptr, TRUE);
        m_regionCacheValid = true;
        m_lastRegionWidth = 0;
        m_lastRegionHeight = 0;
        m_lastRegionRadius = 0;
        m_lastRegionMaximized = true;
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

    if (m_regionCacheValid &&
        m_lastRegionWidth == width &&
        m_lastRegionHeight == height &&
        m_lastRegionRadius == radius &&
        !m_lastRegionMaximized) {
        return;
    }

    if (radius <= 1) {
        SetWindowRgn(m_hwnd, nullptr, TRUE);
        m_regionCacheValid = true;
        m_lastRegionWidth = width;
        m_lastRegionHeight = height;
        m_lastRegionRadius = radius;
        m_lastRegionMaximized = false;
        return;
    }

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius);
    if (!region)
        return;

    if (SetWindowRgn(m_hwnd, region, TRUE) == 0) {
        DeleteObject(region);
        return;
    }

    m_regionCacheValid = true;
    m_lastRegionWidth = width;
    m_lastRegionHeight = height;
    m_lastRegionRadius = radius;
    m_lastRegionMaximized = false;
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
        ApplyWindowRegion();
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
    ApplyWindowRegion();
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
        // Always suppress activation on click - fixes close button in focus test mode.
        // ShowWindow(SW_SHOW) still triggers WM_ACTIVATE directly when focus test mode is on.
        return MA_NOACTIVATE;

    case WM_ACTIVATE: {
        const char* act = (LOWORD(wParam) == WA_INACTIVE)    ? "INACTIVE"    :
                          (LOWORD(wParam) == WA_CLICKACTIVE)  ? "CLICKACTIVE" : "ACTIVE";
        PLog("[WM-ACTIVATE] %s focusTest=%d",
             act, pw ? pw->m_focusTestMode : 0);
        if (pw && pw->m_focusTestMode && LOWORD(wParam) != WA_INACTIVE) {
            PLog("[WM-ACTIVATE] restoring prevFg=%s",
                 pw ? WH(pw->m_prevForeground).c_str() : "null");
            if (pw->m_prevForeground && IsWindow(pw->m_prevForeground))
                SetForegroundWindow(pw->m_prevForeground);
        }
        break;
    }

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
            pw->ApplyWindowRegion();
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
            pw->ApplyWindowRegion();
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
        PLog("[WM-KILLFOCUS] popup lost keyboard focus");
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
