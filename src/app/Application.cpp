#include "Application.h"
#include "TrayIcon.h"
#include "../ui/MainWindow.h"
#include "../ui/PopupWindow.h"
#include "../ui/TrayPopupWindow.h"
#include "../ui/DebugWindow.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ClipboardHistoryStore.h"
#include "../clipboard/ClipboardMonitor.h"
#include "../clipboard/ContentDetector.h"
#include "../hotkeys/HotkeyManager.h"
#include "../util/Win32Util.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

Application* Application::s_instance = nullptr;
static constexpr UINT_PTR kResizeRenderTimerId = 1;
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

namespace {

void ClearMainInputState() {
    ImGui::ClearActiveID();
    ImGuiIO& io = ImGui::GetIO();
    io.ClearInputKeys();
    for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); ++i)
        io.MouseDown[i] = false;
}

std::string NowIsoLocal() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string MakeClipboardId() {
    static unsigned int sequence = 0;
    std::ostringstream out;
    out << "cb-" << std::hex << GetTickCount64() << "-" << ++sequence;
    return out.str();
}

std::string Hex32(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << value;
    return out.str();
}

int ScaledPx(float value, const AppearanceSettings& appearance) {
    return static_cast<int>(std::lround(value * EffectiveUiScale(appearance)));
}

} // namespace

// -- Construction / destruction ------------------------------------------------

Application::Application(HINSTANCE hInstance)
    : m_hInstance(hInstance)
{
    s_instance = this;
}

Application::~Application() {
    Shutdown();
    s_instance = nullptr;
}

// -- Public --------------------------------------------------------------------

int Application::Run() {
    if (!Init()) return 1;

    MSG msg{};
    while (m_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                m_running = false;
        }
        if (!m_running) break;
        if (HasRenderableUi() || m_appearanceDirty)
            RenderFrame();
        else
            WaitMessage();
    }
    return 0;
}

void Application::ShowMainWindow() {
    LogDebug("ShowMainWindow: requested");
    ClearMainInputState();
    m_mainVisible = true;
    MainWindow::RequestFocus();

    if (IsIconic(m_hwnd))
        ShowWindow(m_hwnd, SW_RESTORE);
    else
        ShowWindow(m_hwnd, SW_SHOWNORMAL);

    BringWindowToTop(m_hwnd);
    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
    SetFocus(m_hwnd);
}

void Application::OpenSettingsWindow() {
    if (m_popup) {
        m_popup->OpenSettingsWindow();
        return;
    }

    ShowMainWindow();
}

void Application::HideMainWindow() {
    LogDebug("HideMainWindow: requested");
    ClearMainInputState();
    m_mainVisible = false;
    ShowWindow(m_hwnd, SW_HIDE);
}

void Application::ShowPopup() {
    LogDebug("ShowPopup: requested");
    SyncClipboardForForegroundProcess();
    if (m_popup) m_popup->Show();
}

void Application::ShowTrayPopup() {
    if (m_trayPopup)
        m_trayPopup->ShowAtCursor();
}

void Application::ToggleDebugWindow() {
    if (!m_debugWindow)
        return;

    m_debugWindow->Toggle();
    LogDebug(std::string("debug window ") +
             (m_debugWindow->IsVisible() ? "shown" : "hidden"));
}

void Application::LogDebug(const std::string& event) {
    std::string line = NowIsoLocal();
    line += "  ";
    line += event;
    line += "\n";
    OutputDebugStringA(line.c_str());
    if (m_debugWindow)
        m_debugWindow->AddLine(line);
}

void Application::RequestAppearance(const AppearanceSettings& settings) {
    const float oldScale = EffectiveUiScale(m_appearance);
    {
        std::ostringstream out;
        out << "RequestAppearance: begin"
            << " fontPath=\"" << settings.fontPath << "\""
            << " fontSize=" << settings.fontSize
            << " uiScale=" << settings.uiScale
            << " oldEffectiveScale=" << oldScale;
        LogDebug(out.str());
    }
    m_appearance = settings;
    std::filesystem::path imported = ConfigStore::ImportFontFile(m_appearance.fontPath);
    if (!imported.empty()) {
        LogDebug("RequestAppearance: imported font to \"" + imported.u8string() + "\"");
        m_appearance.fontPath = imported.u8string();
    }
    const float newScale = EffectiveUiScale(m_appearance);
    if (std::fabs(newScale - oldScale) > 0.001f)
        ResizeMainWindowForScale(oldScale, newScale);
    m_config.appearance = m_appearance;
    m_appearanceDirty = true;
    SaveConfig();
    {
        std::ostringstream out;
        out << "RequestAppearance: queued apply"
            << " fontPath=\"" << m_appearance.fontPath << "\""
            << " fontSize=" << m_appearance.fontSize
            << " newEffectiveScale=" << newScale;
        LogDebug(out.str());
    }
}

void Application::SetPopupOpacity(float opacity) {
    m_appearance.popupOpacity = std::clamp(opacity, 0.1f, 1.0f);
    LogDebug("SetPopupOpacity: " + std::to_string(m_appearance.popupOpacity));
    m_config.appearance = m_appearance;
    m_appearanceDirty = true;
    SaveConfig();
}

void Application::SetPopupOutlineStrength(float strength) {
    m_appearance.popupOutlineStrength = std::clamp(strength, 0.0f, 1.0f);
    LogDebug("SetPopupOutlineStrength: " + std::to_string(m_appearance.popupOutlineStrength));
    m_config.appearance = m_appearance;
    m_appearanceDirty = true;
    SaveConfig();
}

void Application::RequestHotkeySettings(const HotkeySettings& settings) {
    m_hotkeySettings = settings;
    m_config.hotkeys = settings;
    if (m_hotkeys)
        m_hotkeys->ApplySettings(m_hotkeySettings);
    SaveConfig();
}

void Application::SetDeveloperSettings(const DeveloperSettings& settings) {
#ifdef NDEBUG
    (void)settings;
    return;
#else
    const bool logWasEnabled = m_config.developer.eventLogEnabled;
    m_config.developer = settings;
    SaveConfig();
    if (!logWasEnabled && settings.eventLogEnabled)
        AddDeveloperEvent("developer event log enabled");
#endif
}

void Application::AddDeveloperEvent(const std::string& event) {
    LogDebug("DeveloperEvent: " + event);
#ifdef NDEBUG
    (void)event;
    return;
#else
    if (!m_config.developer.eventLogEnabled)
        return;

    std::string line = NowIsoLocal();
    line += "  ";
    line += event;
    m_developerEvents.push_back(std::move(line));
    if (m_developerEvents.size() > 300)
        m_developerEvents.erase(m_developerEvents.begin(),
                                m_developerEvents.begin() +
                                    static_cast<std::ptrdiff_t>(m_developerEvents.size() - 300));
#endif
}

void Application::SetNewItemsAtTop(bool value) {
    m_config.newItemsAtTop = value;
    for (auto& history : m_histories) {
        if (history)
            history->SetNewItemsAtTop(value);
    }
    SaveConfig();
}

void Application::SetAppendNewlineAfterPaste(bool value) {
    m_config.appendNewlineAfterPaste = value;
    if (m_popup)
        m_popup->SetAppendNewlineAfterPaste(value);
    SaveConfig();
}

ClipboardHistory::MoveTarget Application::GetPasteMoveTarget() const {
    switch (m_config.pasteMoveTarget) {
    case 1: return ClipboardHistory::MoveTarget::Top;
    case 2: return ClipboardHistory::MoveTarget::Bottom;
    default: return ClipboardHistory::MoveTarget::None;
    }
}

void Application::SetPasteMoveTarget(ClipboardHistory::MoveTarget target) {
    switch (target) {
    case ClipboardHistory::MoveTarget::Top:    m_config.pasteMoveTarget = 1; break;
    case ClipboardHistory::MoveTarget::Bottom: m_config.pasteMoveTarget = 2; break;
    default:                                   m_config.pasteMoveTarget = 0; break;
    }
    if (m_popup)
        m_popup->SetPasteMoveTarget(target);
    SaveConfig();
}

const ClipboardProfileConfig* Application::GetActiveClipboardProfile() const {
    auto it = std::find_if(m_config.clipboards.begin(), m_config.clipboards.end(),
        [&](const ClipboardProfileConfig& c) { return c.id == m_config.activeClipboardId; });
    return it == m_config.clipboards.end() ? nullptr : &(*it);
}

void Application::SetActiveClipboardProfile(const std::string& id) {
    auto it = std::find_if(m_config.clipboards.begin(), m_config.clipboards.end(),
        [&](const ClipboardProfileConfig& c) { return c.id == id; });
    if (it == m_config.clipboards.end())
        return;

    m_config.activeClipboardId = id;
    m_history = HistoryForActiveClipboard();
    m_manualClipboardProcessOverride = ForegroundProcessName();
    SaveConfig();
    AddDeveloperEvent("selected clipboard profile: " + it->name + " (" + it->id + ")");
}

void Application::SelectClipboardProfileSlot(int slot) {
    if (slot < 0)
        return;

    const size_t index = static_cast<size_t>(slot);
    if (index >= m_config.clipboards.size())
        return;

    SetActiveClipboardProfile(m_config.clipboards[index].id);
}

void Application::CreateClipboardProfile(const std::string& name, const std::string& processName) {
    const std::string now = NowIsoLocal();
    ClipboardProfileConfig profile;
    profile.id = MakeClipboardId();
    profile.name = name.empty() ? "New Clipboard" : name;
    profile.createdAt = now;
    profile.updatedAt = now;
    profile.processName = processName;

    m_config.clipboards.push_back(std::move(profile));
    m_histories.push_back(std::make_unique<ClipboardHistory>(500));
    m_histories.back()->SetNewItemsAtTop(m_config.newItemsAtTop);
    ClipboardHistoryStore::Load(m_config.clipboards.back().id, *m_histories.back());
    const std::string savedId = m_config.clipboards.back().id;
    m_histories.back()->SetChangedCallback([this, savedId]() {
        SaveClipboardHistory(savedId);
    });
    m_config.activeClipboardId = m_config.clipboards.back().id;
    m_history = m_histories.back().get();
    m_manualClipboardProcessOverride = ForegroundProcessName();
    SaveConfig();
    SaveActiveClipboardHistory();
    AddDeveloperEvent("created clipboard profile: " + m_config.clipboards.back().name);
}

void Application::RenameActiveClipboardProfile(const std::string& name) {
    if (name.empty())
        return;

    for (ClipboardProfileConfig& profile : m_config.clipboards) {
        if (profile.id == m_config.activeClipboardId) {
            profile.name = name;
            profile.updatedAt = NowIsoLocal();
            SaveConfig();
            AddDeveloperEvent("renamed clipboard profile: " + profile.name);
            return;
        }
    }
}

bool Application::DeleteActiveClipboardProfile() {
    if (m_config.clipboards.size() <= 1)
        return false;

    const std::string deletedId = m_config.activeClipboardId;
    auto it = std::find_if(m_config.clipboards.begin(), m_config.clipboards.end(),
        [&](const ClipboardProfileConfig& c) { return c.id == deletedId; });
    if (it == m_config.clipboards.end())
        return false;

    const size_t index = static_cast<size_t>(std::distance(m_config.clipboards.begin(), it));
    m_config.clipboards.erase(it);
    if (index < m_histories.size())
        m_histories.erase(m_histories.begin() + static_cast<std::ptrdiff_t>(index));

    std::error_code ec;
    std::filesystem::remove(ClipboardHistoryStore::PathForProfile(deletedId), ec);

    const size_t nextIndex = std::min(index, m_config.clipboards.size() - 1);
    m_config.activeClipboardId = m_config.clipboards[nextIndex].id;
    m_history = HistoryForActiveClipboard();
    SaveConfig();
    AddDeveloperEvent("deleted clipboard profile: " + deletedId);
    return true;
}

void Application::CreateClipboardFromForegroundProcess() {
#ifdef NDEBUG
    return;
#else
    const std::string process = ForegroundProcessName();
    if (process.empty())
        return;

    if (ClipboardProfileConfig* existing = FindClipboardForProcess(process)) {
        SetActiveClipboardProfile(existing->id);
        return;
    }

    CreateClipboardProfile(process, process);
#endif
}

void Application::BindActiveClipboardToForegroundProcess() {
#ifdef NDEBUG
    return;
#else
    const std::string process = ForegroundProcessName();
    if (process.empty())
        return;

    for (ClipboardProfileConfig& profile : m_config.clipboards) {
        if (profile.id == m_config.activeClipboardId) {
            profile.processName = process;
            profile.updatedAt = NowIsoLocal();
            SaveConfig();
            AddDeveloperEvent("bound active clipboard to process: " + process);
            return;
        }
    }
#endif
}

void Application::SetAutoSwitchClipboardByProcess(bool value) {
#ifdef NDEBUG
    (void)value;
    m_config.autoSwitchClipboardByProcess = false;
    SaveConfig();
    return;
#else
    m_config.autoSwitchClipboardByProcess = value;
    SaveConfig();
    AddDeveloperEvent(std::string("auto-switch clipboard by process: ") + (value ? "on" : "off"));
#endif
}

void Application::SetAutoCreateClipboardByProcess(bool value) {
#ifdef NDEBUG
    (void)value;
    m_config.autoCreateClipboardByProcess = false;
    SaveConfig();
    return;
#else
    m_config.autoCreateClipboardByProcess = value;
    SaveConfig();
    AddDeveloperEvent(std::string("auto-create clipboard by process: ") + (value ? "on" : "off"));
#endif
}

void Application::SaveConfig() {
    ConfigStore::Save(m_config);
}

void Application::SaveClipboardHistory(const std::string& profileId) {
    for (size_t i = 0; i < m_config.clipboards.size() && i < m_histories.size(); ++i) {
        if (m_config.clipboards[i].id == profileId && m_histories[i]) {
            ClipboardHistoryStore::Save(profileId, *m_histories[i]);
            return;
        }
    }
}

void Application::SaveActiveClipboardHistory() {
    SaveClipboardHistory(m_config.activeClipboardId);
}

void Application::ResizeMainWindowForScale(float oldScale, float newScale) {
    if (!m_hwnd || oldScale <= 0.0f || newScale <= 0.0f)
        return;
    if (IsIconic(m_hwnd) || IsZoomed(m_hwnd))
        return;

    RECT rc{};
    if (!GetWindowRect(m_hwnd, &rc))
        return;

    const float ratio = newScale / oldScale;
    int width = std::max(ScaledPx(800.0f, m_appearance),
                         static_cast<int>(std::lround((rc.right - rc.left) * ratio)));
    int height = std::max(ScaledPx(500.0f, m_appearance),
                          static_cast<int>(std::lround((rc.bottom - rc.top) * ratio)));

    HMONITOR mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (GetMonitorInfoW(mon, &mi)) {
        width = std::min(width, static_cast<int>(mi.rcWork.right - mi.rcWork.left));
        height = std::min(height, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top));
    }

    const int centerX = rc.left + (rc.right - rc.left) / 2;
    const int centerY = rc.top + (rc.bottom - rc.top) / 2;
    int x = centerX - width / 2;
    int y = centerY - height / 2;

    if (GetMonitorInfoW(mon, &mi)) {
        x = std::clamp(x, static_cast<int>(mi.rcWork.left),
                       static_cast<int>(mi.rcWork.right) - width);
        y = std::clamp(y, static_cast<int>(mi.rcWork.top),
                       static_cast<int>(mi.rcWork.bottom) - height);
    }

    SetWindowPos(m_hwnd, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void Application::ApplyLoadedConfig(const AppConfig& config) {
    m_config = config;
    m_appearance = m_config.appearance;
    m_hotkeySettings = m_config.hotkeys;
    m_appearanceDirty = true;
    RebuildClipboardHistories();

    if (m_history)
        m_history->SetNewItemsAtTop(m_config.newItemsAtTop);
    if (m_popup) {
        m_popup->SetAppendNewlineAfterPaste(m_config.appendNewlineAfterPaste);
        m_popup->SetPasteMoveTarget(GetPasteMoveTarget());
    }
    if (m_hotkeys)
        m_hotkeys->ApplySettings(m_hotkeySettings);
}

std::string Application::ForegroundProcessName() const {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == m_hwnd)
        return m_lastForegroundProcess;
    if (m_popup && fg == m_popup->GetHwnd())
        return m_lastForegroundProcess;

    m_lastForegroundProcess = win32util::ProcessNameFromWindow(fg);
    return m_lastForegroundProcess;
}

std::string Application::ExecutablePath() const {
    return win32util::ModulePath();
}

std::string Application::WorkingDirectory() const {
    return win32util::CurrentDirectory();
}

SIZE Application::PopupCurrentSize() const {
    if (m_popup)
        return m_popup->GetCurrentSize();
    return {m_appearance.popupWidth, m_appearance.popupHeight};
}

void Application::UseCurrentPopupSizeAsDefault() {
    SIZE size = PopupCurrentSize();
    size.cx = std::max<LONG>(360, size.cx);
    size.cy = std::max<LONG>(260, size.cy);

    m_appearance.popupWidth = static_cast<int>(size.cx);
    m_appearance.popupHeight = static_cast<int>(size.cy);
    m_config.appearance = m_appearance;
    SaveConfig();
    AddDeveloperEvent("saved popup size as default: " +
                      std::to_string(m_appearance.popupWidth) + "x" +
                      std::to_string(m_appearance.popupHeight));
}

void Application::SyncClipboardForForegroundProcess() {
#ifdef NDEBUG
    return;
#else
    SwitchClipboardForProcess(ForegroundProcessName());
#endif
}

void Application::SyncClipboardForWindow(HWND hwnd) {
#ifdef NDEBUG
    (void)hwnd;
    return;
#else
    if (!hwnd || hwnd == m_hwnd)
        return;
    if (m_popup && hwnd == m_popup->GetHwnd())
        return;

    std::string process = win32util::ProcessNameFromWindow(hwnd);
    if (process.empty())
        return;

    m_lastForegroundProcess = process;
    SwitchClipboardForProcess(process);
#endif
}

void Application::RebuildClipboardHistories() {
    if (m_config.clipboards.empty()) {
        const std::string now = NowIsoLocal();
        m_config.clipboards.push_back({"default", "Default", now, now, ""});
        m_config.activeClipboardId = "default";
    }

    m_histories.clear();
    m_histories.reserve(m_config.clipboards.size());
    for (size_t i = 0; i < m_config.clipboards.size(); ++i) {
        auto history = std::make_unique<ClipboardHistory>(500);
        history->SetNewItemsAtTop(m_config.newItemsAtTop);
        ClipboardHistoryStore::Load(m_config.clipboards[i].id, *history);
        const std::string savedId = m_config.clipboards[i].id;
        history->SetChangedCallback([this, savedId]() {
            SaveClipboardHistory(savedId);
        });
        m_histories.push_back(std::move(history));
    }

    m_history = HistoryForActiveClipboard();
}

ClipboardHistory* Application::HistoryForActiveClipboard() const {
    for (size_t i = 0; i < m_config.clipboards.size() && i < m_histories.size(); ++i) {
        if (m_config.clipboards[i].id == m_config.activeClipboardId)
            return m_histories[i].get();
    }
    return m_histories.empty() ? nullptr : m_histories.front().get();
}

ClipboardProfileConfig* Application::FindClipboardForProcess(const std::string& processName) {
    if (processName.empty())
        return nullptr;

    auto it = std::find_if(m_config.clipboards.begin(), m_config.clipboards.end(),
        [&](const ClipboardProfileConfig& c) {
            return !c.processName.empty() &&
                   _stricmp(c.processName.c_str(), processName.c_str()) == 0;
        });
    return it == m_config.clipboards.end() ? nullptr : &(*it);
}

void Application::CreateClipboardForProcess(const std::string& processName) {
    if (processName.empty())
        return;

    CreateClipboardProfile(processName, processName);
}

void Application::SwitchClipboardForProcess(const std::string& processName) {
#ifdef NDEBUG
    (void)processName;
    return;
#else
    if (!m_config.autoSwitchClipboardByProcess || processName.empty())
        return;

    if (!m_manualClipboardProcessOverride.empty()) {
        if (_stricmp(m_manualClipboardProcessOverride.c_str(), processName.c_str()) == 0)
            return;
        m_manualClipboardProcessOverride.clear();
    }

    ClipboardProfileConfig* profile = FindClipboardForProcess(processName);
    if (!profile && m_config.autoCreateClipboardByProcess) {
        CreateClipboardForProcess(processName);
        return;
    }

    if (!profile || profile->id == m_config.activeClipboardId)
        return;

    m_config.activeClipboardId = profile->id;
    m_history = HistoryForActiveClipboard();
    SaveConfig();
#endif
}

bool Application::HandleClipboardTextCommand(const COPYDATASTRUCT& cds) {
    if (!m_history || cds.dwData != CD_CLIPBOARD_TEXT ||
        cds.cbData < sizeof(ClipboardTextCommand))
        return false;

    const auto* cmd = static_cast<const ClipboardTextCommand*>(cds.lpData);
    const size_t headerBytes = offsetof(ClipboardTextCommand, text);
    const size_t textBytes = cds.cbData - headerBytes;
    if (textBytes < sizeof(wchar_t))
        return false;

    const size_t maxChars = textBytes / sizeof(wchar_t);
    const size_t chars = wcsnlen_s(cmd->text, maxChars);
    if (chars == 0)
        return false;

    const std::vector<std::wstring> filePaths = win32util::ExistingPathList(cmd->text);

    int utf8Bytes = WideCharToMultiByte(CP_UTF8, 0, cmd->text, static_cast<int>(chars),
                                        nullptr, 0, nullptr, nullptr);
    if (utf8Bytes <= 0)
        return false;

    ClipboardItem item;
    item.type = filePaths.empty() ? ContentType::Text : ContentType::FilePaths;
    item.text.resize(static_cast<size_t>(utf8Bytes));
    WideCharToMultiByte(CP_UTF8, 0, cmd->text, static_cast<int>(chars),
                        item.text.data(), utf8Bytes, nullptr, nullptr);
    item.sourceProcess = "clipboardpp.exe";
    item.tags = ContentDetector::DetectTags(item.text);

    size_t index = 0;
    if (cmd->position == -1) {
        index = m_history->Size();
    } else if (cmd->position > 0) {
        index = static_cast<size_t>(std::clamp(cmd->position, 1, 500) - 1);
    }

    if (cmd->setSystemClipboard) {
        if (m_monitor)
            m_monitor->SuppressNextUpdate();
        if (!filePaths.empty())
            win32util::SetClipboardFileDrop(m_hwnd, filePaths);
        else
            win32util::SetClipboardUnicodeText(m_hwnd, cmd->text, chars);
    }

    m_history->Insert(std::move(item), index);
    return true;
}

// -- Private: initialisation ---------------------------------------------------

bool Application::Init() {
    ApplyLoadedConfig(ConfigStore::Load());

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_hInstance;
    wc.hIcon         = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));   // IDI_APPLICATION
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32514)); // IDC_ARROW
    wc.hbrBackground = nullptr; // D3D owns the background - prevents white flash on resize
    wc.lpszClassName = L"ClipboardPlusPlus_Main";
    RegisterClassExW(&wc);

    // WS_POPUP removes native chrome; WS_THICKFRAME keeps resize hit-testing.
    // WS_EX_APPWINDOW ensures we still appear in the taskbar.
    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"ClipboardPlusPlus_Main",
        L"Clipboard++",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        ScaledPx(1200.0f, m_appearance), ScaledPx(750.0f, m_appearance),
        nullptr, nullptr, m_hInstance, nullptr
    );

    if (!m_hwnd) return false;

    // DWM drop-shadow for borderless window (1px inset on all sides is enough)
    MARGINS shadow = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(m_hwnd, &shadow);
    const COLORREF noBorder = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR,
                          &noBorder, sizeof(noBorder));

    if (!CreateD3D()) {
        DestroyD3D();
        UnregisterClassW(L"ClipboardPlusPlus_Main", m_hInstance);
        return false;
    }

    // ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename  = nullptr;

    ApplyThemeStyle(m_appearance, false);

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_d3dDevice, m_d3dContext);

    RebuildFontAtlas(io, m_appearance);

    m_tray = std::make_unique<TrayIcon>(m_hwnd, m_hInstance);
    if (!m_tray->Create()) return false;

    // Clipboard histories + monitor
    RebuildClipboardHistories();
    m_monitor = std::make_unique<ClipboardMonitor>();
    m_monitor->Start(m_hInstance, [this](ClipboardItem item) {
        if (!item.sourceProcess.empty())
            m_lastForegroundProcess = item.sourceProcess;
        SwitchClipboardForProcess(item.sourceProcess);
        if (m_history) {
            const std::string source = item.sourceProcess.empty() ? "unknown" : item.sourceProcess;
            const std::string preview = item.Preview(80);
            AddDeveloperEvent("captured " + std::string(ContentTypeName(item.type)) +
                              " from " + source +
                              " tags=" + Hex32(item.tags) +
                              " preview=\"" + preview + "\"");
            m_history->Push(std::move(item));
        }
    });

    m_popup = std::make_unique<PopupWindow>();
    if (!m_popup->Create(m_hInstance, m_d3dDevice, m_d3dContext))
        return false;
    m_popup->ApplyAppearance(m_appearance);
    m_popup->SetAppendNewlineAfterPaste(m_config.appendNewlineAfterPaste);
    m_popup->SetPasteMoveTarget(GetPasteMoveTarget());

    m_trayPopup = std::make_unique<TrayPopupWindow>();
    if (!m_trayPopup->Create(m_hInstance, m_d3dDevice, m_d3dContext))
        return false;
    m_trayPopup->ApplyAppearance(m_appearance);

    m_debugWindow = std::make_unique<DebugWindow>();
    if (!m_debugWindow->Create(m_hInstance, m_d3dDevice, m_d3dContext))
        return false;
    m_debugWindow->ApplyAppearance(m_appearance);
    LogDebug("debug window initialized; toggle with Alt+Shift+D");

    m_hotkeys = std::make_unique<HotkeyManager>();
    m_hotkeys->Install(m_hwnd);
    m_hotkeys->ApplySettings(m_hotkeySettings);

    // TODO (Milestone 5): only show on first launch
    ShowMainWindow();

    m_running = true;
    return true;
}

void Application::Shutdown() {
    if (m_hotkeys) m_hotkeys->Uninstall();
    if (m_debugWindow) m_debugWindow->Destroy();
    if (m_trayPopup) m_trayPopup->Destroy();
    if (m_popup)   m_popup->Destroy();
    if (m_monitor) m_monitor->Stop();
    if (m_tray)    m_tray->Destroy();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    DestroyD3D();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        UnregisterClassW(L"ClipboardPlusPlus_Main", m_hInstance);
        m_hwnd = nullptr;
    }
}

// -- Private: render -----------------------------------------------------------

void Application::RenderFrame() {
    if (m_appearanceDirty)
        ApplyAppearanceNow();

    const bool renderMain = m_mainVisible && m_hwnd && !IsIconic(m_hwnd);
    if (renderMain) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        MainWindow::Draw(m_mainVisible);

        ImGui::Render();

        constexpr float bg[4] = {0.118f, 0.118f, 0.118f, 1.0f}; // #1e1e1e
        m_d3dContext->OMSetRenderTargets(1, &m_renderTarget, nullptr);
        m_d3dContext->ClearRenderTargetView(m_renderTarget, bg);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        m_swapChain->Present(1, 0);
    }

    // Popup has its own context + swap chain - rendered separately
    if (m_popup) m_popup->Render();
    if (m_trayPopup) m_trayPopup->Render();
    if (m_debugWindow) m_debugWindow->Render();
}

bool Application::HasRenderableUi() const {
    const bool mainRenderable = m_mainVisible && m_hwnd && !IsIconic(m_hwnd);
    const bool popupRenderable = m_popup && m_popup->IsVisible();
    const bool trayRenderable = m_trayPopup && m_trayPopup->IsVisible();
    const bool debugRenderable = m_debugWindow && m_debugWindow->IsVisible();
    return mainRenderable || popupRenderable || trayRenderable || debugRenderable;
}

void Application::ApplyAppearanceNow() {
    m_appearanceDirty = false;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ApplyThemeStyle(m_appearance, false);
    ImGui_ImplDX11_InvalidateDeviceObjects();
    const bool mainFontOk = RebuildFontAtlas(ImGui::GetIO(), m_appearance);
    ImGui_ImplDX11_CreateDeviceObjects();
    {
        ImGuiIO& io = ImGui::GetIO();
        std::ostringstream out;
        out << "ApplyAppearanceNow: main font rebuild"
            << " ok=" << (mainFontOk ? "true" : "false")
            << " fonts=" << io.Fonts->Fonts.Size
            << " requestedSize=" << m_appearance.fontSize
            << " globalScale=" << io.FontGlobalScale
            << " fontPath=\"" << m_appearance.fontPath << "\"";
        LogDebug(out.str());
    }

    if (m_popup)
        m_popup->ApplyAppearance(m_appearance);
    if (m_trayPopup)
        m_trayPopup->ApplyAppearance(m_appearance);
    if (m_debugWindow)
        m_debugWindow->ApplyAppearance(m_appearance);
    ImGui::SetCurrentContext(prevCtx);
}

// -- Private: D3D11 -----------------------------------------------------------

bool Application::CreateD3D() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = m_hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, levels, 2, D3D11_SDK_VERSION,
        &sd, &m_swapChain, &m_d3dDevice, &level, &m_d3dContext);

    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            0, levels, 2, D3D11_SDK_VERSION,
            &sd, &m_swapChain, &m_d3dDevice, &level, &m_d3dContext);

    if (FAILED(hr)) return false;

    CreateRenderTarget();
    return true;
}

void Application::DestroyD3D() {
    DestroyRenderTarget();
    if (m_swapChain)  { m_swapChain->Release();  m_swapChain  = nullptr; }
    if (m_d3dContext) { m_d3dContext->Release(); m_d3dContext = nullptr; }
    if (m_d3dDevice)  { m_d3dDevice->Release();  m_d3dDevice  = nullptr; }
}

void Application::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        m_d3dDevice->CreateRenderTargetView(back, nullptr, &m_renderTarget);
        back->Release();
    }
}

void Application::DestroyRenderTarget() {
    if (m_renderTarget) { m_renderTarget->Release(); m_renderTarget = nullptr; }
}

// -- Win32 message handler -----------------------------------------------------

LRESULT CALLBACK Application::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_MOUSEACTIVATE)
        return MA_ACTIVATE;

    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return TRUE;

    Application* app = Application::Get();

    switch (msg) {

    // -- Remove all native non-client area so we own every pixel --------------
    case WM_NCCALCSIZE:
        if (wParam == TRUE) {
            // When maximized, Windows inflates the window rect by the border
            // thickness so it overlaps the taskbar. Compensate by shrinking
            // the client rect back to the monitor work area.
            if (IsZoomed(hwnd)) {
                NCCALCSIZE_PARAMS* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                int bx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int by = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                p->rgrc[0].left   += bx;
                p->rgrc[0].right  -= bx;
                p->rgrc[0].top    += by;
                p->rgrc[0].bottom -= by;
            }
            return 0;
        }
        break;

    // -- Tell Windows which part of our window each pixel belongs to ----------
    case WM_NCPAINT:
        return 0;

    case WM_NCACTIVATE:
        return TRUE;

    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);

        const bool maximized = IsZoomed(hwnd) != 0;
        const int  border    = maximized ? 0 : ScaledPx(8.0f, app ? app->m_appearance : AppearanceSettings{});
        const int  titleH    = ScaledPx(static_cast<float>(MainWindow::kTitleBarHeight),
                                        app ? app->m_appearance : AppearanceSettings{});
        const int  btnZoneX  = rc.right - ScaledPx(static_cast<float>(MainWindow::kTitleBtnWidth) * 3.0f,
                                                   app ? app->m_appearance : AppearanceSettings{});

        if (!maximized) {
            const bool onL = pt.x < border;
            const bool onR = pt.x >= rc.right  - border;
            const bool onT = pt.y < border;
            const bool onB = pt.y >= rc.bottom - border;

            if (onT && onL) return HTTOPLEFT;
            if (onT && onR) return HTTOPRIGHT;
            if (onB && onL) return HTBOTTOMLEFT;
            if (onB && onR) return HTBOTTOMRIGHT;
            if (onL)        return HTLEFT;
            if (onR)        return HTRIGHT;
            if (onT)        return HTTOP;
            if (onB)        return HTBOTTOM;
        }

        // Title bar - drag region excludes the three button slots on the right
        if (pt.y >= 0 && pt.y < titleH && pt.x < btnZoneX)
            return HTCAPTION;

        return HTCLIENT;
    }

    // -- Enforce a minimum window size ----------------------------------------
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        const AppearanceSettings appearance = app ? app->m_appearance : AppearanceSettings{};
        mmi->ptMinTrackSize = {ScaledPx(800.0f, appearance), ScaledPx(500.0f, appearance)};
        return 0;
    }

    case WM_ERASEBKGND:
        return TRUE; // prevent white flash during resize

    case WM_SIZE:
        if (app && app->m_d3dDevice && wParam != SIZE_MINIMIZED) {
            app->DestroyRenderTarget();
            app->m_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam),
                                             DXGI_FORMAT_UNKNOWN, 0);
            app->CreateRenderTarget();
        }
        return 0;

    case WM_ENTERSIZEMOVE:
        SetTimer(hwnd, kResizeRenderTimerId, 16, nullptr);
        return 0;

    case WM_EXITSIZEMOVE:
        KillTimer(hwnd, kResizeRenderTimerId);
        return 0;

    case WM_TIMER:
        if (app && wParam == kResizeRenderTimerId)
            app->RenderFrame();
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_MINIMIZE) {
            if (app) app->HideMainWindow();
            return 0;
        }
        break;

    case WM_CLOSE:
        if (app) app->HideMainWindow();
        return 0;

    case WM_TRAYICON:
        if (app && app->m_tray)
            app->m_tray->HandleMessage(wParam, lParam);
        return 0;

    case WM_SHOWCPP_MAIN:
        if (app) app->OpenSettingsWindow();
        return 0;

    case WM_SHOWPOPUP:
        if (app) app->ShowPopup();
        return 0;

    case WM_SHOWTRAYPOPUP:
        if (app) app->ShowTrayPopup();
        return 0;

    case WM_RELOAD_CONFIG:
        if (app) app->ApplyLoadedConfig(ConfigStore::Load());
        return 0;

    case WM_COPYDATA:
        if (app)
            return app->HandleClipboardTextCommand(*reinterpret_cast<COPYDATASTRUCT*>(lParam)) ? TRUE : FALSE;
        return FALSE;

    case WM_HOTKEYACTION: {
        if (!app) return 0;
        auto action = static_cast<HotkeyAction>(wParam);
        int  data   = static_cast<int>(lParam);

        switch (action) {
        case HotkeyAction::TogglePopup:
            if (app->m_popup) {
                if (app->m_popup->IsVisible()) app->m_popup->Hide();
                else                           app->m_popup->Show(false);
            }
            break;
        case HotkeyAction::ShowPopupSearch:
            if (app->m_popup) {
                if (!app->m_popup->IsVisible())
                    app->m_popup->Show(true);
                else
                    app->m_popup->RequestSearchFocus();
            }
            break;
        case HotkeyAction::OpenSettings:
            app->OpenSettingsWindow();
            break;
        case HotkeyAction::Incognito:
            // TODO Milestone 9
            break;
        case HotkeyAction::PasteHistorySlot: {
            // Direct paste - no popup shown.
            // Capture foreground window now, before any focus changes.
            HWND target = GetForegroundWindow();
            app->SyncClipboardForWindow(target);
            if (app->m_popup)
                app->m_popup->PasteHistorySlot(data, target);
            break;
        }
        case HotkeyAction::PastePinnedSlot: {
            HWND target = GetForegroundWindow();
            app->SyncClipboardForWindow(target);
            if (app->m_popup)
                app->m_popup->PastePinnedSlot(data, target);
            break;
        }
        case HotkeyAction::SelectClipboardProfileSlot:
            app->SelectClipboardProfileSlot(data);
            break;
        case HotkeyAction::LaunchWebSearch:
            if (app->m_popup)
                app->m_popup->LaunchWebSearch();
            break;
        case HotkeyAction::LaunchClipboardWebSearch:
            if (app->m_popup)
                app->m_popup->LaunchClipboardWebSearch();
            break;
        case HotkeyAction::ToggleDebugWindow:
            app->ToggleDebugWindow();
            break;
        case HotkeyAction::PasteVisibleSlot:
            if (app->m_popup)
                app->m_popup->PasteVisibleSlot(data);
            break;
        default: break;
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
