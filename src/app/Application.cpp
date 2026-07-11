#include "Application.h"
#include "IconPatcher.h"
#include "TrayIcon.h"
#include "../android/AndroidDeviceClient.h"
#include "../android/AndroidSyncServer.h"
#include "../ui/MainWindow.h"
#include "../ui/PopupWindow.h"
#include "../ui/TrayPopupWindow.h"
#include "../ui/TextEditorWindow.h"
#include "../ui/DebugWindow.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ClipboardHistoryStore.h"
#include "../clipboard/ClipboardMonitor.h"
#include "../clipboard/ContentDetector.h"
#include "../clipboard/ImageStore.h"
#include "../clipboard/ScreenshotTracker.h"
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
#include <shlobj.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <ctime>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>

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

std::string ScreenshotDescription(const std::filesystem::path& path, int width, int height) {
    return "[Screenshot] " + path.filename().u8string() + " " +
           std::to_string(width) + "x" + std::to_string(height) +
           "\n" + path.u8string();
}

int ScaledPx(float value, const AppearanceSettings& appearance) {
    return static_cast<int>(std::lround(value * EffectiveUiScale(appearance)));
}

std::string EditorExtensionForMode(int mode) {
    switch (mode) {
    case 1: return ".ps1";
    case 2: return ".cmd";
    case 3: return ".json";
    case 4: return ".md";
    default: return ".txt";
    }
}

std::string EditorModeName(int mode) {
    switch (mode) {
    case 1: return "powershell";
    case 2: return "batch";
    case 3: return "json";
    case 4: return "markdown";
    default: return "text";
    }
}

std::filesystem::path BundledIdePath() {
    wchar_t exePath[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
        return {};

    const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    const std::filesystem::path sameDir = exeDir / "clipboardpp_ide.exe";
    std::error_code ec;
    if (std::filesystem::exists(sameDir, ec))
        return sameDir;

    const std::filesystem::path buildSibling =
        exeDir.parent_path() / "clipboardpp_ide" / "clipboardpp_ide.exe";
    if (std::filesystem::exists(buildSibling, ec))
        return buildSibling;

    return sameDir;
}

std::string NormalizeExtension(std::string value, int mode) {
    if (value.empty())
        value = EditorExtensionForMode(mode);
    if (value.front() != '.')
        value.insert(value.begin(), '.');
    for (char& c : value) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    return value;
}

std::filesystem::path EditorTempPath(const EditorSettings& settings) {
    std::filesystem::path dir = ConfigStore::Directory() / "editor";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ostringstream name;
    name << "scratch-" << std::hex << GetTickCount64()
         << NormalizeExtension(settings.externalTempExtension, settings.mode);
    return dir / name.str();
}

bool HotkeyKeysReleased() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0 &&
           (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0 &&
           (GetAsyncKeyState(VK_MENU) & 0x8000) == 0 &&
           (GetAsyncKeyState('Z') & 0x8000) == 0;
}

void SendCtrlC() {
    INPUT in[4]{};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_CONTROL;
    in[0].ki.dwExtraInfo = kClipboardPasteMagic;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = 'C';
    in[1].ki.dwExtraInfo = kClipboardPasteMagic;
    in[2].type = INPUT_KEYBOARD;
    in[2].ki.wVk = 'C';
    in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[2].ki.dwExtraInfo = kClipboardPasteMagic;
    in[3].type = INPUT_KEYBOARD;
    in[3].ki.wVk = VK_CONTROL;
    in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.dwExtraInfo = kClipboardPasteMagic;
    SendInput(4, in, sizeof(INPUT));
}

bool WriteUtf8File(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

std::string ReadUtf8File(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::wstring QuoteArg(std::wstring value) {
    std::wstring out = L"\"";
    for (wchar_t c : value) {
        if (c == L'"')
            out += L'\\';
        out += c;
    }
    out += L"\"";
    return out;
}

void ReplaceAll(std::wstring& text, const std::wstring& from, const std::wstring& to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
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

    SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(m_hwnd);
    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
    SetFocus(m_hwnd);
}

bool Application::InsertExternalClipboardText(const std::string& text,
                                              const std::string& sourceProcess) {
    if (!m_history || text.empty())
        return false;

    ClipboardItem item;
    item.type = ContentType::Text;
    item.text = text;
    item.sourceProcess = sourceProcess.empty() ? "external" : sourceProcess;
    item.tags = ContentDetector::DetectTags(item.text);
    m_history->Push(std::move(item));
    return true;
}

bool Application::AddAndroidClipboardText(const std::string& text,
                                          const std::string& source) {
    if (text.empty())
        return false;

    std::lock_guard<std::mutex> lock(m_androidClipboardMutex);
    auto existing = std::find_if(m_androidClipboardEntries.begin(), m_androidClipboardEntries.end(),
        [&](const AndroidClipboardEntry& entry) {
            return entry.text == text;
        });

    if (existing != m_androidClipboardEntries.end()) {
        AndroidClipboardEntry entry = std::move(*existing);
        entry.source = source.empty() ? entry.source : source;
        entry.capturedAt = std::chrono::system_clock::now();
        m_androidClipboardEntries.erase(existing);
        m_androidClipboardEntries.insert(m_androidClipboardEntries.begin(), std::move(entry));
        return false;
    }

    AndroidClipboardEntry entry;
    entry.id = m_nextAndroidClipboardEntryId++;
    entry.text = text;
    entry.source = source.empty() ? "android" : source;
    entry.capturedAt = std::chrono::system_clock::now();
    m_androidClipboardEntries.insert(m_androidClipboardEntries.begin(), std::move(entry));
    return true;
}

std::vector<AndroidClipboardEntry> Application::GetAndroidClipboardEntries() const {
    std::lock_guard<std::mutex> lock(m_androidClipboardMutex);
    return m_androidClipboardEntries;
}

bool Application::RemoveAndroidClipboardEntry(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_androidClipboardMutex);
    auto it = std::find_if(m_androidClipboardEntries.begin(), m_androidClipboardEntries.end(),
        [&](const AndroidClipboardEntry& entry) {
            return entry.id == id;
        });
    if (it == m_androidClipboardEntries.end())
        return false;
    m_androidClipboardEntries.erase(it);
    return true;
}

bool Application::SetAndroidClipboardEntryPinned(uint64_t id, bool pinned) {
    std::lock_guard<std::mutex> lock(m_androidClipboardMutex);
    auto it = std::find_if(m_androidClipboardEntries.begin(), m_androidClipboardEntries.end(),
        [&](const AndroidClipboardEntry& entry) {
            return entry.id == id;
        });
    if (it == m_androidClipboardEntries.end())
        return false;
    it->pinned = pinned;
    std::stable_sort(m_androidClipboardEntries.begin(), m_androidClipboardEntries.end(),
        [](const AndroidClipboardEntry& a, const AndroidClipboardEntry& b) {
            if (a.pinned != b.pinned)
                return a.pinned && !b.pinned;
            return a.capturedAt > b.capturedAt;
        });
    return true;
}

bool Application::SendTextItemsToAndroid(const std::vector<std::string>& texts, std::string* error) {
    return androidsync::SendItemsToAndroid(m_androidDeviceEndpoint, texts, error);
}

void Application::SendSelectionToAndroidClipboard() {
    if (m_androidDeviceEndpoint.empty()) {
        AddDeveloperEvent("send selection to Android failed: endpoint not set");
        return;
    }

    std::thread([this]() {
        for (int i = 0; i < 60 && !HotkeyKeysReleased(); ++i)
            Sleep(25);

        const bool hadText = IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
        const std::string previousText = hadText ? win32util::ClipboardUnicodeText() : std::string{};
        const DWORD beforeSeq = GetClipboardSequenceNumber();

        if (m_monitor)
            m_monitor->SuppressNextUpdate();
        SendCtrlC();

        bool changed = false;
        for (int i = 0; i < 40; ++i) {
            Sleep(25);
            if (GetClipboardSequenceNumber() != beforeSeq) {
                changed = true;
                break;
            }
        }

        const std::string selectedText = win32util::ClipboardUnicodeText();
        if (hadText) {
            const std::wstring wide = win32util::Utf8ToWide(previousText);
            if (m_monitor)
                m_monitor->SuppressNextUpdate();
            win32util::SetClipboardUnicodeText(nullptr, wide.c_str(), wide.size());
        }

        if (!changed || selectedText.empty()) {
            AddDeveloperEvent("send selection to Android skipped: no selected text copied");
            return;
        }

        std::string error;
        if (SendTextItemsToAndroid({selectedText}, &error))
            AddDeveloperEvent("sent selected text to Android clipboard");
        else
            AddDeveloperEvent("send selection to Android failed: " +
                              (error.empty() ? std::string("unknown error") : error));
    }).detach();
}

void Application::SetAndroidDeviceEndpoint(const std::string& endpoint) {
    m_androidDeviceEndpoint = endpoint;
    while (!m_androidDeviceEndpoint.empty() &&
           std::isspace(static_cast<unsigned char>(m_androidDeviceEndpoint.front())))
        m_androidDeviceEndpoint.erase(m_androidDeviceEndpoint.begin());
    while (!m_androidDeviceEndpoint.empty() &&
           std::isspace(static_cast<unsigned char>(m_androidDeviceEndpoint.back())))
        m_androidDeviceEndpoint.pop_back();
    if (!m_androidDeviceEndpoint.empty() &&
        m_androidDeviceEndpoint.rfind("http://", 0) != 0 &&
        m_androidDeviceEndpoint.rfind("https://", 0) != 0) {
        m_androidDeviceEndpoint = "http://" + m_androidDeviceEndpoint;
    }
    m_config.android.deviceEndpoint = m_androidDeviceEndpoint;
    SaveConfig();
}

bool Application::RequestAndroidSyncToWindows(std::string* error) {
    return androidsync::RequestAndroidSyncToWindows(m_androidDeviceEndpoint, error);
}

bool Application::CheckAndroidDeviceHealth(std::string* error) {
    return androidsync::CheckAndroidHealth(m_androidDeviceEndpoint, error);
}

bool Application::IsAndroidSyncServerRunning() const {
    return m_androidSyncServer && m_androidSyncServer->IsRunning();
}

unsigned short Application::AndroidSyncServerPort() const {
    return m_androidSyncServer ? m_androidSyncServer->Port() : 8766;
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

void Application::ShowEditorPopup() {
    if (!m_config.editor.enabled)
        return;
    if (m_config.editor.provider == 1) {
        if (LaunchExternalEditor())
            return;
        LogDebug("ShowEditorPopup: external editor launch failed; falling back to built-in editor");
    }
    if (m_editor)
        m_editor->Show();
}

bool Application::LaunchExternalEditor() {
    const EditorSettings settings = m_config.editor;
    std::filesystem::path editorPath = settings.externalPath.empty()
        ? BundledIdePath()
        : std::filesystem::path(win32util::Utf8ToWide(settings.externalPath));
    if (editorPath.empty())
        return false;

    const std::filesystem::path tempPath = EditorTempPath(settings);
    const std::string text = settings.openWithClipboard
        ? win32util::ClipboardUnicodeText()
        : std::string{};
    if (!WriteUtf8File(tempPath, text)) {
        LogDebug("LaunchExternalEditor: failed to write temp file " + tempPath.u8string());
        return false;
    }

    const std::wstring exe = editorPath.wstring();
    std::wstring args = win32util::Utf8ToWide(
        settings.externalArguments.empty() ? std::string("{file}") : settings.externalArguments);
    ReplaceAll(args, L"{file}", QuoteArg(tempPath.wstring()));
    ReplaceAll(args, L"{filePath}", tempPath.wstring());
    ReplaceAll(args, L"{mode}", win32util::Utf8ToWide(EditorModeName(settings.mode)));

    std::wstring command = QuoteArg(exe);
    if (!args.empty()) {
        command += L" ";
        command += args;
    }

    std::filesystem::path workingDir;
    std::error_code ec;
    if (editorPath.has_parent_path() && std::filesystem::exists(editorPath.parent_path(), ec))
        workingDir = editorPath.parent_path();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> commandBuf(command.begin(), command.end());
    commandBuf.push_back(L'\0');
    std::wstring workingDirText = workingDir.wstring();

    const BOOL ok = CreateProcessW(
        nullptr,
        commandBuf.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        workingDirText.empty() ? nullptr : workingDirText.c_str(),
        &si,
        &pi);

    if (!ok) {
        LogDebug("LaunchExternalEditor: CreateProcess failed for " + editorPath.u8string() +
                 " gle=" + std::to_string(GetLastError()));
        return false;
    }

    CloseHandle(pi.hThread);
    const bool shouldWait = settings.externalWaitForExit || settings.externalReadBackToClipboard;
    if (shouldWait) {
        std::thread([process = pi.hProcess,
                     tempPath,
                     readBack = settings.externalReadBackToClipboard]() {
            WaitForSingleObject(process, INFINITE);
            CloseHandle(process);
            if (readBack) {
                const std::string edited = ReadUtf8File(tempPath);
                const std::wstring wide = win32util::Utf8ToWide(edited);
                win32util::SetClipboardUnicodeText(nullptr, wide.c_str(), wide.size());
            }
        }).detach();
    } else {
        CloseHandle(pi.hProcess);
    }

    LogDebug("LaunchExternalEditor: opened " + tempPath.u8string() +
             " with " + editorPath.u8string());
    return true;
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
    {
        std::ostringstream out;
        out << "RequestAppearance: begin"
            << " fontPath=\"" << settings.fontPath << "\""
            << " fontSize=" << settings.fontSize
            << " uiScale=windows";
        LogDebug(out.str());
    }
    m_appearance = settings;
    m_appearance.uiScale = 1.0f;
    m_appearance.dpiScale = win32util::DpiScaleForWindow(m_hwnd);
    std::filesystem::path imported = ConfigStore::ImportFontFile(m_appearance.fontPath);
    if (!imported.empty()) {
        LogDebug("RequestAppearance: imported font to \"" + imported.u8string() + "\"");
        m_appearance.fontPath = imported.u8string();
    }
    CommitAppearanceChange();
    {
        std::ostringstream out;
        out << "RequestAppearance: queued apply"
            << " fontPath=\"" << m_appearance.fontPath << "\""
            << " fontSize=" << m_appearance.fontSize;
        LogDebug(out.str());
    }
}

void Application::SetPopupOpacity(float opacity) {
    m_appearance.popupOpacity = std::clamp(opacity, 0.1f, 1.0f);
    LogDebug("SetPopupOpacity: " + std::to_string(m_appearance.popupOpacity));
    CommitAppearanceChange();
}

void Application::SetPopupOutlineStrength(float strength) {
    m_appearance.popupOutlineStrength = std::clamp(strength, 0.0f, 1.0f);
    LogDebug("SetPopupOutlineStrength: " + std::to_string(m_appearance.popupOutlineStrength));
    CommitAppearanceChange();
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

void Application::SetUiSettings(const UiSettings& settings) {
    m_config.ui = settings;
    SaveConfig();
    m_appearanceDirty = true;
}

void Application::SetEditorSettings(const EditorSettings& settings) {
    m_config.editor = settings;
    if (m_editor)
        m_editor->ApplySettings(settings);
    SaveConfig();
}

void Application::SetCustomFilters(const std::vector<CustomFilter>& filters) {
    m_config.customFilters = filters;
    ClearCustomFilterRegexCache();
    SaveConfig();
}

void Application::SetPopupButtonOrder(const std::vector<std::string>& order) {
    m_config.popupButtonOrder = order;
    SaveConfig();
}

void Application::SetHidePopupOnOutsideClick(bool value) {
    m_config.hidePopupOnOutsideClick = value;
    SaveConfig();
}

void Application::SetImageSettings(const ImageSettings& settings) {
    m_config.images = settings;
    if (m_imageStore)
        m_imageStore->SetSettings(settings);
    SaveConfig();
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
    m_histories.push_back(std::make_unique<ClipboardHistory>(kMaxClipboardHistoryItems));
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

void Application::CommitAppearanceChange() {
    m_config.appearance = m_appearance;
    m_appearanceDirty = true;
    SaveConfig();
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

void Application::AddScreenshotPair(ClipboardHistory* history,
                                    const std::filesystem::path& path,
                                    ClipboardItem imageItem,
                                    bool newAtTop) {
    if (!history || path.empty() || imageItem.type != ContentType::Image)
        return;

    const std::string pathText = path.u8string();
    ClipboardItem pathItem;
    pathItem.type = ContentType::FilePaths;
    pathItem.tags = TAG_PATH | TAG_FILE | TAG_IMAGE_FILE;
    pathItem.text = pathText;
    pathItem.sourceKind = "screenshot-path";
    pathItem.sourceFilePath = pathText;
    pathItem.sourceProcess = imageItem.sourceProcess;

    imageItem.sourceKind = "screenshot";
    imageItem.sourceFilePath = pathText;
    imageItem.text = "[Screenshot CF_DIB] " + path.filename().u8string() + " " +
                     std::to_string(imageItem.imageW) + "x" +
                     std::to_string(imageItem.imageH);

    if (newAtTop) {
        history->Push(std::move(imageItem));
        history->Push(std::move(pathItem));
    } else {
        history->Push(std::move(pathItem));
        history->Push(std::move(imageItem));
    }

    AddDeveloperEvent("added screenshot path + CF_DIB rows: " + pathText);
}

void Application::ScheduleScreenshotPairAdd(ClipboardHistory* history,
                                            ClipboardItem imageItem,
                                            bool newAtTop) {
    if (!history || imageItem.sourcePixelHash == 0)
        return;

    if (!imageItem.sourceFilePath.empty()) {
        AddScreenshotPair(history, std::filesystem::path(imageItem.sourceFilePath),
                          std::move(imageItem), newAtTop);
        return;
    }

    std::thread([history, imageItem = std::move(imageItem), newAtTop]() mutable {
        for (int attempt = 0; attempt < 12; ++attempt) {
            Sleep(attempt == 0 ? 150 : 300);
            std::filesystem::path path =
                ScreenshotTracker::Instance().FindRecentScreenshotFile(
                    imageItem.imageW, imageItem.imageH, imageItem.sourcePixelHash);
            if (path.empty())
                continue;

            if (Application* app = Application::Get())
                app->AddScreenshotPair(history, path, std::move(imageItem), newAtTop);
            return;
        }
        if (Application* app = Application::Get())
            app->AddDeveloperEvent("screenshot dropped: no matching file path found");
    }).detach();
}

void Application::ApplyLoadedConfig(const AppConfig& config) {
    m_config = config;
    m_androidDeviceEndpoint = m_config.android.deviceEndpoint;
    m_appearance = m_config.appearance;
    m_appearance.uiScale = 1.0f;
    m_appearance.dpiScale = win32util::DpiScaleForWindow(m_hwnd);
    m_hotkeySettings = m_config.hotkeys;
    m_appearanceDirty = true;
    RebuildClipboardHistories();

    if (m_history)
        m_history->SetNewItemsAtTop(m_config.newItemsAtTop);
    if (m_popup) {
        m_popup->SetAppendNewlineAfterPaste(m_config.appendNewlineAfterPaste);
        m_popup->SetPasteMoveTarget(GetPasteMoveTarget());
    }
    if (m_editor)
        m_editor->ApplySettings(m_config.editor);
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

SIZE Application::MainWindowCurrentSize() const {
    if (m_hwnd) {
        RECT rc{};
        if (GetWindowRect(m_hwnd, &rc)) {
            const float dpiScale = win32util::DpiScaleForWindow(m_hwnd);
            return {
                static_cast<LONG>(std::lround((rc.right - rc.left) / dpiScale)),
                static_cast<LONG>(std::lround((rc.bottom - rc.top) / dpiScale))
            };
        }
    }
    return {m_appearance.mainWindowWidth, m_appearance.mainWindowHeight};
}

void Application::UseCurrentMainWindowSizeAsDefault() {
    SIZE size = MainWindowCurrentSize();
    size.cx = std::max<LONG>(800, size.cx);
    size.cy = std::max<LONG>(500, size.cy);

    m_appearance.mainWindowWidth = static_cast<int>(size.cx);
    m_appearance.mainWindowHeight = static_cast<int>(size.cy);
    CommitAppearanceChange();
    AddDeveloperEvent("saved settings window size as default: " +
                      std::to_string(m_appearance.mainWindowWidth) + "x" +
                      std::to_string(m_appearance.mainWindowHeight));
}

SIZE Application::PopupCurrentSize() const {
    if (m_popup) {
        SIZE size = m_popup->GetCurrentSize();
        const float dpiScale = win32util::DpiScaleForWindow(m_popup->GetHwnd());
        return {
            static_cast<LONG>(std::lround(size.cx / dpiScale)),
            static_cast<LONG>(std::lround(size.cy / dpiScale))
        };
    }
    return {m_appearance.popupWidth, m_appearance.popupHeight};
}

void Application::UseCurrentPopupSizeAsDefault() {
    SIZE size = PopupCurrentSize();
    size.cx = std::max<LONG>(360, size.cx);
    size.cy = std::max<LONG>(260, size.cy);

    m_appearance.popupWidth = static_cast<int>(size.cx);
    m_appearance.popupHeight = static_cast<int>(size.cy);
    CommitAppearanceChange();
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
        auto history = std::make_unique<ClipboardHistory>(kMaxClipboardHistoryItems);
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

ClipboardHistory* Application::HistoryForProfile(const std::string& profileId) const {
    for (size_t i = 0; i < m_config.clipboards.size() && i < m_histories.size(); ++i) {
        if (m_config.clipboards[i].id == profileId)
            return m_histories[i].get();
    }
    return nullptr;
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
        index = static_cast<size_t>(std::clamp(cmd->position, 1, kMaxClipboardHistoryItems) - 1);
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
    wc.hIcon         = LoadIconW(m_hInstance, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm       = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                           GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                           LR_DEFAULTCOLOR);
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32514)); // IDC_ARROW
    wc.hbrBackground = nullptr; // D3D owns the background - prevents white flash on resize
    wc.lpszClassName = L"ClipboardPlusPlus_Main";
    RegisterClassExW(&wc);

    // WS_POPUP removes all native chrome so ImGui, the D3D client area, and the
    // Win32 mouse coordinates share the same origin. WS_THICKFRAME keeps resize
    // behavior available through WM_NCHITTEST.
    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"ClipboardPlusPlus_Main",
        L"Clipboard++",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        ScaledPx(static_cast<float>(std::max(800, m_appearance.mainWindowWidth)), m_appearance),
        ScaledPx(static_cast<float>(std::max(500, m_appearance.mainWindowHeight)), m_appearance),
        nullptr, nullptr, m_hInstance, nullptr
    );

    if (!m_hwnd) return false;
    m_appearance.dpiScale = win32util::DpiScaleForWindow(m_hwnd);

    // Set window icon explicitly so it appears in the taskbar and title bar
    if (HICON hBig = LoadIconW(m_hInstance, MAKEINTRESOURCEW(1)))
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hBig));
    if (HICON hSm = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                       GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                       LR_DEFAULTCOLOR))
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSm));

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

    // Image store
    m_imageStore = std::make_unique<ImageStore>();
    m_imageStore->Open(ConfigStore::Directory() / "images.db");
    m_imageStore->SetSettings(m_config.images);

    // Clipboard histories + monitor
    RebuildClipboardHistories();
    m_monitor = std::make_unique<ClipboardMonitor>();
    m_monitor->SetImageStore(m_imageStore.get());
    m_monitor->SetProfileIdGetter([this]() -> std::string {
        if (const ClipboardProfileConfig* p = GetActiveClipboardProfile())
            return p->id;
        return "default";
    });
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

            ClipboardHistory* routeHistory = nullptr;
            std::string routeProfileId;
            std::string routeName;
            bool routeMove = false;
            for (const CustomFilter& filter : m_config.customFilters) {
                if (!filter.enabled || !filter.routeToProfile || filter.routeProfileId.empty())
                    continue;
                if (!CustomFilterMatches(filter, item))
                    continue;
                routeHistory = HistoryForProfile(filter.routeProfileId);
                if (!routeHistory)
                    continue;
                routeProfileId = filter.routeProfileId;
                routeName = filter.name;
                routeMove = filter.routeMove;
                break;
            }

            const bool screenshotNeedsPair =
                item.type == ContentType::Image &&
                item.sourceKind == "screenshot" &&
                item.sourcePixelHash != 0;
            if (screenshotNeedsPair) {
                if (routeHistory) {
                    ScheduleScreenshotPairAdd(routeHistory, item, m_config.newItemsAtTop);
                    AddDeveloperEvent("routed screenshot by filter \"" + routeName +
                                      "\" to profile " + routeProfileId +
                                      (routeMove ? " (move)" : " (copy)"));
                }
                if (!routeHistory || (!routeMove && routeHistory != m_history))
                    ScheduleScreenshotPairAdd(m_history, std::move(item), m_config.newItemsAtTop);
            } else {
                if (routeHistory) {
                    routeHistory->Push(item);
                    AddDeveloperEvent("routed item by filter \"" + routeName +
                                      "\" to profile " + routeProfileId +
                                      (routeMove ? " (move)" : " (copy)"));
                }
                if (!routeHistory || (!routeMove && routeHistory != m_history))
                    m_history->Push(std::move(item));
            }
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

    m_editor = std::make_unique<TextEditorWindow>();
    if (!m_editor->Create(m_hInstance, m_d3dDevice, m_d3dContext))
        return false;
    m_editor->ApplyAppearance(m_appearance);
    m_editor->ApplySettings(m_config.editor);

    m_debugWindow = std::make_unique<DebugWindow>();
    if (!m_debugWindow->Create(m_hInstance, m_d3dDevice, m_d3dContext))
        return false;
    m_debugWindow->ApplyAppearance(m_appearance);
    LogDebug("debug window initialized; toggle with Alt+Shift+D");

    m_hotkeys = std::make_unique<HotkeyManager>();
    m_hotkeys->Install(m_hwnd);
    m_hotkeys->ApplySettings(m_hotkeySettings);

    m_androidSyncServer = std::make_unique<AndroidSyncServer>(*this);
    if (m_androidSyncServer->Start())
        LogDebug("Android sync server listening on port 8766");
    else
        LogDebug("Android sync server failed to start on port 8766");

    // TODO (Milestone 5): only show on first launch
    ShowMainWindow();

    m_running = true;
    return true;
}

void Application::Shutdown() {
    if (m_androidSyncServer) {
        m_androidSyncServer->Stop();
        m_androidSyncServer.reset();
    }
    if (m_hotkeys) m_hotkeys->Uninstall();
    if (m_debugWindow) m_debugWindow->Destroy();
    if (m_editor) m_editor->Destroy();
    if (m_trayPopup) m_trayPopup->Destroy();
    if (m_popup)   m_popup->Destroy();
    if (m_monitor) m_monitor->Stop();
    if (m_tray)    m_tray->Destroy();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (m_appIconSrv) { m_appIconSrv->Release(); m_appIconSrv = nullptr; }
    DestroyD3D();

    // Patch the exe icon if needed. PowerShell is skipped when nothing has changed:
    //   - Custom path set       → always apply it (user explicitly chose it)
    //   - Custom path empty     → apply the theme-rendered icon, but ONLY if the theme
    //                             colors changed since the last patch (hash mismatch) or
    //                             if a custom icon was previously applied (hash is "")
    {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        DWORD myPid = GetCurrentProcessId();

        std::wstring icoToApply;
        bool shouldPatch = false;

        if (!m_config.appearance.exeIconPath.empty()) {
            icoToApply  = std::filesystem::path(m_config.appearance.exeIconPath).wstring();
            shouldPatch = true;
            m_config.appearance.exeIconThemeHash = ""; // custom overrides theme tracking
        } else {
            std::string currentHash = TrayIcon::ThemeIconHash(m_config.appearance);
            if (currentHash != m_config.appearance.exeIconThemeHash) {
                std::wstring themeIco = (ConfigStore::Directory() / "theme_icon.ico").wstring();
                if (TrayIcon::WriteThemeIco(m_config.appearance, themeIco)) {
                    icoToApply  = themeIco;
                    shouldPatch = true;
                    m_config.appearance.exeIconThemeHash = currentHash;
                }
            }
        }

        if (shouldPatch) {
            SaveConfig();

            std::wstring cmdLine = L"\"" + std::wstring(exePath) + L"\" --patch-icon \""
                                 + std::wstring(exePath) + L"\" \""
                                 + icoToApply + L"\" "
                                 + std::to_wstring(myPid);

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
            buf.push_back(L'\0');
            CreateProcessW(nullptr, buf.data(), nullptr, nullptr,
                           FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
            if (pi.hThread)  CloseHandle(pi.hThread);
            if (pi.hProcess) CloseHandle(pi.hProcess);
        }
    }

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
    if (m_editor) m_editor->Render();
    if (m_debugWindow) m_debugWindow->Render();
}

bool Application::HasRenderableUi() const {
    const bool mainRenderable = m_mainVisible && m_hwnd && !IsIconic(m_hwnd);
    const bool popupRenderable = m_popup && m_popup->IsVisible();
    const bool trayRenderable = m_trayPopup && m_trayPopup->IsVisible();
    const bool editorRenderable = m_editor && m_editor->IsVisible();
    const bool debugRenderable = m_debugWindow && m_debugWindow->IsVisible();
    return mainRenderable || popupRenderable || trayRenderable || editorRenderable || debugRenderable;
}

void Application::ApplyAppearanceNow() {
    m_appearanceDirty = false;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    ApplyThemeStyle(m_appearance, false);
    const float helperDelay =
        static_cast<float>(std::clamp(m_config.ui.helperDelayMs, 0, 5000)) / 1000.0f;
    ImGui::GetStyle().HoverDelayShort = helperDelay;
    ImGui::GetStyle().HoverDelayNormal = helperDelay;
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
    if (m_editor)
        m_editor->ApplyAppearance(m_appearance);
    if (m_debugWindow)
        m_debugWindow->ApplyAppearance(m_appearance);
    if (m_tray)
        m_tray->ApplyTheme(m_appearance);
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

    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            ClearMainInputState();
            MainWindow::RequestFocus();
        }
        break;

    // -- Remove all native non-client area so we own every pixel --------------
    case WM_NCCALCSIZE:
        // Returning 0 for wParam==TRUE discards the NC area — entire window rect
        // becomes the client rect.  WM_GETMINMAXINFO already constrains the
        // maximized rect to the work area, so no thin strip or taskbar overlap.
        if (wParam == TRUE) return 0;
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
        const int  btnZoneX  = rc.right - ScaledPx(static_cast<float>(MainWindow::kTitleBtnWidth) * 4.0f,
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

        // Title bar - drag region excludes the four button slots on the right
        if (pt.y >= 0 && pt.y < titleH && pt.x < btnZoneX)
            return HTCAPTION;

        return HTCLIENT;
    }

    // -- Minimum size + maximized size capped to work area -------------------
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        const AppearanceSettings appearance = app ? app->m_appearance : AppearanceSettings{};
        mmi->ptMinTrackSize = {ScaledPx(800.0f, appearance), ScaledPx(500.0f, appearance)};
        HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfo(hMon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left;
            mmi->ptMaxPosition.y = mi.rcWork.top;
            mmi->ptMaxSize.x     = mi.rcWork.right  - mi.rcWork.left;
            mmi->ptMaxSize.y     = mi.rcWork.bottom - mi.rcWork.top;
        }
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

    case WM_DPICHANGED:
        if (app) {
            app->m_appearance.dpiScale = win32util::DpiScaleForWindow(hwnd);
            app->m_appearanceDirty = true;
            if (RECT* suggested = reinterpret_cast<RECT*>(lParam)) {
                SetWindowPos(hwnd, nullptr,
                             suggested->left,
                             suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            app->LogDebug("main window DPI changed; scale=" +
                          std::to_string(app->m_appearance.dpiScale));
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
        switch (wParam & 0xFFF0) {
        case SC_MINIMIZE: ShowWindow(hwnd, SW_MINIMIZE); return 0;
        case SC_MAXIMIZE: ShowWindow(hwnd, SW_MAXIMIZE); return 0;
        case SC_RESTORE:  ShowWindow(hwnd, SW_RESTORE);  return 0;
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
            if (app->m_tray)
                app->m_tray->SetIncognito(!app->m_tray->IsIncognito());
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
        case HotkeyAction::ToggleEditorWindow:
            app->ShowEditorPopup();
            break;
        case HotkeyAction::SendSelectionToAndroid:
            app->SendSelectionToAndroidClipboard();
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

// -- Icon texture (cached, loaded on first use) --------------------------------

ID3D11ShaderResourceView* Application::GetAppIconSrv() {
    if (m_appIconSrv) return m_appIconSrv;
    if (!m_d3dDevice) return nullptr;

    constexpr int kSize = 64;
    HICON hIcon = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(1),
                                    IMAGE_ICON, kSize, kSize, LR_DEFAULTCOLOR);
    if (!hIcon) return nullptr;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdc       = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = kSize;
    bmi.bmiHeader.biHeight      = -kSize; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void*   pixels = nullptr;
    HBITMAP hDIB   = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (hDIB && pixels) {
        memset(pixels, 0, kSize * kSize * 4);
        HGDIOBJ old = SelectObject(hdc, hDIB);
        DrawIconEx(hdc, 0, 0, hIcon, kSize, kSize, 0, nullptr, DI_NORMAL);
        GdiFlush();
        SelectObject(hdc, old);

        // GDI gives BGRA; D3D11 RGBA needs bytes 0↔2 swapped
        auto* p = static_cast<uint8_t*>(pixels);
        for (int i = 0; i < kSize * kSize; ++i, p += 4)
            std::swap(p[0], p[2]);

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width            = kSize;
        desc.Height           = kSize;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem     = pixels;
        init.SysMemPitch = kSize * 4;

        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(m_d3dDevice->CreateTexture2D(&desc, &init, &tex))) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels       = 1;
            m_d3dDevice->CreateShaderResourceView(tex, &srvDesc, &m_appIconSrv);
            tex->Release();
        }
        DeleteObject(hDIB);
    }

    DeleteDC(hdc);
    ReleaseDC(nullptr, hdcScreen);
    DestroyIcon(hIcon);
    return m_appIconSrv;
}
