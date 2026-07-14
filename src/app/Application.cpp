#include "Application.h"
#include "IconPatcher.h"
#include "StartupRegistration.h"
#include "TrayIcon.h"
#include "../ui/MainWindow.h"
#include "../ui/PopupWindow.h"
#include "../ui/TrayPopupWindow.h"
#include "../ui/TextEditorWindow.h"
#include "../ui/DebugWindow.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ClipboardMonitor.h"
#include "../clipboard/ContentDetector.h"
#include "../clipboard/ImageStore.h"
#include "../clipboard/ScreenshotTracker.h"
#include "../hotkeys/HotkeyManager.h"
#include "../security/BackupRestore.h"
#include "../security/StatePackage.h"
#include "../util/Win32Util.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <json.hpp>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <climits>
#include <ctime>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>
#include <unordered_map>

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

std::string Hex32(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << value;
    return out.str();
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

template<typename Values, typename Name>
std::string UniqueImportedName(const std::string& requested,
                               const Values& values, Name name) {
    auto exists = [&](const std::string& candidate) {
        const std::string folded = Lowercase(candidate);
        return std::any_of(values.begin(), values.end(), [&](const auto& value) {
            return Lowercase(name(value)) == folded;
        });
    };
    if (!exists(requested)) return requested;
    for (int suffix = 2;; ++suffix) {
        const std::string candidate = requested + " (imported " +
                                      std::to_string(suffix) + ")";
        if (!exists(candidate)) return candidate;
    }
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

void SecureDeleteEditorScratch(const std::filesystem::path& path);

std::filesystem::path EditorTempPath(const EditorSettings& settings) {
    std::filesystem::path dir = ConfigStore::Directory() / "editor";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto staleBefore = std::filesystem::file_time_type::clock::now() -
                             std::chrono::hours(24);
    for (std::filesystem::directory_iterator it(dir, ec), end;
         !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().string();
        std::error_code timeError;
        const auto modified = std::filesystem::last_write_time(it->path(), timeError);
        if (!timeError && name.rfind("scratch-", 0) == 0 && modified < staleBefore)
            SecureDeleteEditorScratch(it->path());
    }
    std::ostringstream name;
    name << "scratch-" << std::hex << GetTickCount64()
         << NormalizeExtension(settings.externalTempExtension, settings.mode);
    return dir / name.str();
}

void SecureDeleteEditorScratch(const std::filesystem::path& path) {
    // Best effort only: flash storage and filesystem journaling can retain old
    // blocks. Overwrite the visible file before deletion so ordinary recovery
    // does not leave the editor plaintext behind.
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (!ec && size > 0) {
        std::ofstream output(path, std::ios::binary | std::ios::in | std::ios::out);
        if (output) {
            std::array<char, 64 * 1024> zeros{};
            uintmax_t remaining = size;
            while (remaining > 0 && output) {
                const auto count = static_cast<std::streamsize>(
                    std::min<uintmax_t>(remaining, zeros.size()));
                output.write(zeros.data(), count);
                remaining -= static_cast<uintmax_t>(count);
            }
            output.flush();
        }
    }
    std::filesystem::remove(path, ec);
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

Application::Application(HINSTANCE hInstance, bool safeModeRequested)
    : m_hInstance(hInstance),
      m_safeModeRequested(safeModeRequested),
      m_safeMode(safeModeRequested),
      m_androidIntegration(std::make_unique<AndroidIntegration>(*this))
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

    if (GetForegroundWindow() != m_hwnd) {
        const DWORD currentThread = GetCurrentThreadId();
        const HWND foreground = GetForegroundWindow();
        const DWORD foregroundThread = foreground
            ? GetWindowThreadProcessId(foreground, nullptr)
            : 0;
        const bool attached = foregroundThread != 0 &&
                              foregroundThread != currentThread &&
                              AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;

        // Briefly raising the window through the topmost band makes it visible even
        // when Windows declines the first foreground request. It is immediately
        // returned to the normal z-order and never remains always-on-top.
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        BringWindowToTop(m_hwnd);
        SetForegroundWindow(m_hwnd);
        SetActiveWindow(m_hwnd);
        SetFocus(m_hwnd);

        if (attached)
            AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
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
    return m_androidIntegration &&
           m_androidIntegration->AddClipboardText(text, source);
}

std::vector<AndroidClipboardEntry> Application::GetAndroidClipboardEntries() const {
    return m_androidIntegration
        ? m_androidIntegration->ClipboardEntries()
        : std::vector<AndroidClipboardEntry>{};
}

bool Application::RemoveAndroidClipboardEntry(uint64_t id) {
    return m_androidIntegration &&
           m_androidIntegration->RemoveClipboardEntry(id);
}

bool Application::SetAndroidClipboardEntryPinned(uint64_t id, bool pinned) {
    return m_androidIntegration &&
           m_androidIntegration->SetClipboardEntryPinned(id, pinned);
}

const std::string& Application::GetAndroidDeviceEndpoint() const {
    static const std::string empty;
    return m_androidIntegration ? m_androidIntegration->DeviceEndpoint() : empty;
}

bool Application::SendTextItemsToAndroid(const std::vector<std::string>& texts,
                                         std::string* error) {
    return m_androidIntegration &&
           m_androidIntegration->SendTextItems(texts, error);
}

void Application::SendSelectionToAndroidClipboard() {
    if (m_androidIntegration)
        m_androidIntegration->SendSelectionToDevice();
}

void Application::SetAndroidDeviceEndpoint(const std::string& endpoint) {
    if (!m_androidIntegration)
        return;
    m_androidIntegration->SetDeviceEndpoint(endpoint);
    m_config.android.deviceEndpoint = m_androidIntegration->DeviceEndpoint();
    SaveConfig();
}

bool Application::RequestAndroidSyncToWindows(std::string* error) {
    return m_androidIntegration &&
           m_androidIntegration->RequestSyncToWindows(error);
}

bool Application::CheckAndroidDeviceHealth(std::string* error) {
    return m_androidIntegration &&
           m_androidIntegration->CheckDeviceHealth(error);
}

bool Application::IsAndroidSyncServerRunning() const {
    return m_androidIntegration && m_androidIntegration->IsServerRunning();
}

unsigned short Application::AndroidSyncServerPort() const {
    return m_androidIntegration ? m_androidIntegration->ServerPort() : 8766;
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
    if (!m_trayPopup) {
        m_trayPopup = std::make_unique<TrayPopupWindow>();
        if (!m_trayPopup->Create(m_hInstance, m_d3dDevice, m_d3dContext)) {
            m_trayPopup.reset();
            return;
        }
        m_trayPopup->ApplyAppearance(m_appearance);
    }
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
    if (!m_editor) {
        m_editor = std::make_unique<TextEditorWindow>();
        if (!m_editor->Create(m_hInstance, m_d3dDevice, m_d3dContext)) {
            m_editor.reset();
            return;
        }
        m_editor->ApplyAppearance(m_appearance);
        m_editor->ApplySettings(m_config.editor);
    }
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
        LogDebug("LaunchExternalEditor: failed to write scratch file");
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
        SecureDeleteEditorScratch(tempPath);
        LogDebug("LaunchExternalEditor: CreateProcess failed gle=" +
                 std::to_string(GetLastError()));
        return false;
    }

    CloseHandle(pi.hThread);
    const bool shouldWait = settings.externalWaitForExit ||
                            settings.externalReadBackToClipboard;
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
            SecureDeleteEditorScratch(tempPath);
        }).detach();
    } else {
        // Many GUI launchers exit after handing the file to an existing process;
        // deleting here would race that editor. The next launch removes scratch
        // files older than 24 hours instead.
        CloseHandle(pi.hProcess);
    }

    LogDebug("LaunchExternalEditor: opened protected scratch workflow");
    return true;
}

void Application::ToggleDebugWindow() {
    if (!m_debugWindow) {
        m_debugWindow = std::make_unique<DebugWindow>();
        if (!m_debugWindow->Create(m_hInstance, m_d3dDevice, m_d3dContext)) {
            m_debugWindow.reset();
            return;
        }
        m_debugWindow->ApplyAppearance(m_appearance);
        LogDebug("debug window initialized on demand");
    }

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

void Application::SetIncognito(bool enabled) {
    if (m_incognito == enabled)
        return;

    m_incognito = enabled;
    if (m_monitor)
        m_monitor->SetCaptureEnabled(!enabled);
    if (m_tray)
        m_tray->SetIncognito(enabled);

    LogDebug(enabled
        ? "Incognito mode enabled; clipboard capture suspended"
        : "Incognito mode disabled; clipboard capture resumed");
}

void Application::ToggleIncognito() {
    SetIncognito(!m_incognito);
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
    SyncCustomActionHotkeys();
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

void Application::SetPopupSettings(const PopupSettings& settings) {
    m_config.popup = settings;
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

void Application::RecordGeneratedPaste(const std::string& sourceProcess,
                                       const std::string& destinationProcess) {
    m_lastGeneratedPasteSource = sourceProcess;
    m_lastGeneratedPasteDestination = destinationProcess;
    AddDeveloperEvent("generated paste recorded (source and destination available in inspector)");
}

void Application::SetNewItemsAtTop(bool value) {
    m_config.newItemsAtTop = value;
    if (m_clipboardProfiles)
        m_clipboardProfiles->SetNewItemsAtTop(value);
    SaveConfig();
}

void Application::SetActiveHistoryLimit(int value) {
    value = std::clamp(value, 1, kMaxClipboardHistoryItems);
    if (m_config.activeHistoryLimit == value) return;
    m_config.activeHistoryLimit = value;
    if (m_clipboardProfiles)
        m_clipboardProfiles->SetHistoryLimit(value);
    SaveConfig();
}

void Application::SetHistoryDeduplicationEnabled(bool enabled) {
    if (m_config.deduplicateHistory == enabled) return;
    m_config.deduplicateHistory = enabled;
    if (m_clipboardProfiles)
        m_clipboardProfiles->SetDeduplicationEnabled(enabled);
    SaveConfig();
}

void Application::SetVaultLimit(bool unlimited, int limitMB) {
    m_config.vaultUnlimited = unlimited;
    m_config.vaultLimitMB = std::clamp(limitMB, 1, 102400);
    if (m_clipboardProfiles)
        m_clipboardProfiles->ApplyVaultLimit();
    SaveConfig();
}

size_t Application::GetVaultCount() const {
    return m_clipboardProfiles ? m_clipboardProfiles->VaultCount() : 0;
}

std::vector<ClipboardVaultEntry> Application::SearchVault(
    const std::string& query) const {
    std::vector<ClipboardVaultEntry> entries;
    if (m_clipboardProfiles)
        m_clipboardProfiles->SearchVault(query, entries);
    return entries;
}

bool Application::PromoteVaultItem(int64_t archiveId) {
    return m_clipboardProfiles && m_clipboardProfiles->PromoteVaultItem(archiveId);
}

bool Application::DeleteVaultItem(int64_t archiveId) {
    return m_clipboardProfiles && m_clipboardProfiles->DeleteVaultItem(archiveId);
}

std::vector<NamedClipboardSlot> Application::GetNamedSlots() const {
    if (!m_namedSlotsCached) {
        m_namedSlotsCache.clear();
        if (m_clipboardProfiles &&
            m_clipboardProfiles->LoadNamedSlots(m_namedSlotsCache))
            m_namedSlotsCached = true;
    }
    return m_namedSlotsCache;
}

bool Application::SaveNamedSlot(NamedClipboardSlot& slot) {
    const bool saved = m_clipboardProfiles && m_clipboardProfiles->SaveNamedSlot(slot);
    if (saved) m_namedSlotsCached = false;
    return saved;
}

bool Application::DeleteNamedSlot(int64_t slotId) {
    if (!m_clipboardProfiles || !m_clipboardProfiles->DeleteNamedSlot(slotId))
        return false;
    m_namedSlotsCached = false;
    m_config.hotkeys.bindings.erase(std::remove_if(
        m_config.hotkeys.bindings.begin(), m_config.hotkeys.bindings.end(),
        [&](const KeyBinding& binding) {
            return binding.action == HotkeyAction::PasteNamedSlot &&
                   binding.data == static_cast<int>(slotId);
        }), m_config.hotkeys.bindings.end());
    RequestHotkeySettings(m_config.hotkeys);
    return true;
}

std::vector<RegexTransformDefinition> Application::GetRegexTransforms() const {
    if (!m_regexTransformsCached) {
        m_regexTransformsCache.clear();
        if (m_clipboardProfiles &&
            m_clipboardProfiles->LoadRegexTransforms(m_regexTransformsCache))
            m_regexTransformsCached = true;
    }
    return m_regexTransformsCache;
}

bool Application::SaveRegexTransform(RegexTransformDefinition& transform) {
    const bool saved = m_clipboardProfiles &&
        m_clipboardProfiles->SaveRegexTransform(transform);
    if (saved) m_regexTransformsCached = false;
    return saved;
}

bool Application::DeleteRegexTransform(int64_t transformId) {
    const bool deleted = m_clipboardProfiles &&
        m_clipboardProfiles->DeleteRegexTransform(transformId);
    if (deleted) m_regexTransformsCached = false;
    return deleted;
}

std::vector<PasteTemplateDefinition> Application::GetPasteTemplates() const {
    if (!m_pasteTemplatesCached) {
        m_pasteTemplatesCache.clear();
        if (m_clipboardProfiles &&
            m_clipboardProfiles->LoadPasteTemplates(m_pasteTemplatesCache))
            m_pasteTemplatesCached = true;
    }
    return m_pasteTemplatesCache;
}

bool Application::SavePasteTemplate(PasteTemplateDefinition& value) {
    const bool saved = m_clipboardProfiles &&
        m_clipboardProfiles->SavePasteTemplate(value);
    if (saved) m_pasteTemplatesCached = false;
    return saved;
}

bool Application::DeletePasteTemplate(int64_t templateId) {
    const bool deleted = m_clipboardProfiles &&
        m_clipboardProfiles->DeletePasteTemplate(templateId);
    if (deleted) m_pasteTemplatesCached = false;
    return deleted;
}

std::vector<CustomActionDefinition> Application::GetCustomActions() const {
    if (!m_customActionsCached) {
        m_customActionsCache.clear();
        if (m_clipboardProfiles &&
            m_clipboardProfiles->LoadCustomActions(m_customActionsCache))
            m_customActionsCached = true;
    }
    return m_customActionsCache;
}

bool Application::SaveCustomAction(CustomActionDefinition& action) {
    const bool saved = m_clipboardProfiles &&
                       m_clipboardProfiles->SaveCustomAction(action);
    if (saved) {
        m_customActionsCached = false;
        SyncCustomActionHotkeys();
    }
    return saved;
}

bool Application::DeleteCustomAction(int64_t actionId) {
    const bool deleted = m_clipboardProfiles &&
                         m_clipboardProfiles->DeleteCustomAction(actionId);
    if (deleted) {
        m_customActionsCached = false;
        SyncCustomActionHotkeys();
    }
    return deleted;
}

bool Application::ImportCustomAction(const std::string& payload,
                                     std::string* error) {
    CustomActionDefinition action;
    if (!DeserializeCustomAction(payload, action, error))
        return false;
    action.actionId = 0;
    action.createdAtMs = 0;
    action.updatedAtMs = 0;
    if (!SaveCustomAction(action)) {
        if (error) *error = "Could not import the action. Its label may already exist.";
        return false;
    }
    return true;
}

void Application::SyncCustomActionHotkeys() {
    if (!m_hotkeys)
        return;
    HotkeySettings runtime = m_hotkeySettings;
    runtime.bindings.erase(std::remove_if(
        runtime.bindings.begin(), runtime.bindings.end(),
        [](const KeyBinding& binding) {
            return binding.action == HotkeyAction::RunCustomAction;
        }), runtime.bindings.end());
    for (const CustomActionDefinition& action : GetCustomActions()) {
        if (!action.enabled || !action.hotkeyEnabled || action.hotkey.vkey == 0 ||
            action.actionId <= 0 || action.actionId > INT_MAX)
            continue;
        KeyBinding binding = action.hotkey;
        binding.action = HotkeyAction::RunCustomAction;
        binding.data = static_cast<int>(action.actionId);
        runtime.bindings.push_back(binding);
    }
    m_hotkeys->ApplySettings(runtime);
}

bool Application::CopyTextToClipboard(const std::string& text) {
    const std::wstring wide = win32util::Utf8ToWide(text);
    if (m_monitor)
        m_monitor->BeginSelfWrite();
    const bool written = win32util::SetClipboardUnicodeText(m_hwnd, wide.c_str(), wide.size());
    if (m_monitor)
        m_monitor->EndSelfWrite();
    return written;
}

bool Application::IsStartWithWindowsEnabled() const {
    return StartupRegistration::IsEnabled();
}

bool Application::SetStartWithWindowsEnabled(bool enabled) {
    LSTATUS error = ERROR_SUCCESS;
    if (!StartupRegistration::SetEnabled(enabled, &error)) {
        AddDeveloperEvent("failed to update Start with Windows: error=" +
                          std::to_string(error));
        return false;
    }
    AddDeveloperEvent(std::string("Start with Windows: ") +
                      (enabled ? "on" : "off"));
    return true;
}

bool Application::HasPendingEncryptedRestore() const {
    return backup_restore::HasPendingRestore(ConfigStore::Directory());
}

bool Application::CancelPendingEncryptedRestore(std::string& message) {
    if (!backup_restore::CancelPendingRestore(ConfigStore::Directory(), &message))
        return false;
    message = "Pending restore canceled. Current data was not changed.";
    return true;
}

std::string Application::LastEncryptedRestoreStatus() const {
    return backup_restore::ReadLastRestoreStatus(ConfigStore::Directory());
}

bool Application::RestartForEncryptedRestore() {
    wchar_t executable[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, executable, MAX_PATH)) return false;
    std::wstring command = L"\"" + std::wstring(executable) +
        L"\" --clipboardpp-restart " + std::to_wstring(GetCurrentProcessId());
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable, buffer.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                        nullptr, nullptr, &startup, &process))
        return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    PostMessageW(m_hwnd, WM_DESTROY, 0, 0);
    return true;
}

bool Application::QuarantineStorageAndRestart(std::string& message) {
    if (!m_safeMode) {
        message = "Storage quarantine is available only while safe mode is active.";
        return false;
    }
    const auto dataDirectory = ConfigStore::Directory();
    const auto quarantine = dataDirectory / "recovery" /
        ("Unavailable storage " + std::to_string(GetTickCount64()));
    std::error_code ec;
    std::filesystem::create_directories(quarantine, ec);
    if (ec) {
        message = "Could not create the recovery folder: " + ec.message();
        return false;
    }
    std::filesystem::copy_file(ConfigStore::Path(), quarantine / "config.json",
        std::filesystem::copy_options::overwrite_existing, ec);
    ec.clear();
    const std::vector<std::string> components = {
        "clipboard.db", "clipboard.db.key", "clipboard.db-wal",
        "clipboard.db-shm", "clipboard.db-journal",
        "images.db", "images.db.key", "images.db-wal",
        "images.db-shm", "images.db-journal"};
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moved;
    for (const std::string& component : components) {
        const auto source = dataDirectory / component;
        if (!std::filesystem::exists(source, ec)) { ec.clear(); continue; }
        const auto destination = quarantine / component;
        std::filesystem::rename(source, destination, ec);
        if (ec) {
            for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
                std::error_code rollbackError;
                std::filesystem::rename(it->second, it->first, rollbackError);
            }
            message = "Could not quarantine " + component + ": " + ec.message();
            return false;
        }
        moved.emplace_back(source, destination);
    }
    if (moved.empty()) {
        message = "No clipboard database files were found to quarantine.";
        return false;
    }
    m_config.profilesStoredInDatabase = false;
    m_config.activeClipboardId = "default";
    if (m_config.clipboards.empty()) {
        const std::string now = NowIsoLocal();
        m_config.clipboards.push_back({"default", "Default", now, now, ""});
    }
    if (!ConfigStore::Save(m_config)) {
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
            std::error_code rollbackError;
            std::filesystem::rename(it->second, it->first, rollbackError);
        }
        message = "Could not save the fresh-start configuration; storage quarantine was canceled.";
        return false;
    }
    message = "Unavailable storage was retained in " + quarantine.u8string();
    if (!RestartForEncryptedRestore()) {
        message += ", but Clipboard++ could not restart automatically.";
        return false;
    }
    return true;
}

bool Application::ExportStatePackage(const std::filesystem::path& path,
                                     bool encrypted,
                                     std::string& message) const {
    state_package::Data data;
    {
        std::ifstream input(ConfigStore::Path(), std::ios::binary);
        if (input)
            data.configurationJson.assign(std::istreambuf_iterator<char>(input),
                                          std::istreambuf_iterator<char>());
    }
    data.profiles = GetClipboardProfiles();
    data.namedSlots = GetNamedSlots();
    data.transforms = GetRegexTransforms();
    data.templates = GetPasteTemplates();
    data.actions = GetCustomActions();
    const state_package::Result result =
        state_package::Write(path, data, encrypted);
    message = result.message;
    return result.ok;
}

bool Application::ImportStatePackage(
    const std::filesystem::path& path, state_package::ConflictPolicy policy,
    bool& restartRequired, std::string& message) {
    restartRequired = false;
    state_package::Data data;
    const state_package::Result loaded = state_package::Read(path, data);
    if (!loaded.ok) { message = loaded.message; return false; }

    int imported = 0;
    int replaced = 0;
    int skipped = 0;
    bool failed = false;
    std::unordered_map<int64_t, int64_t> namedSlotIdMap;
    std::unordered_map<std::string, std::string> profileIdMap;
    const ClipboardProfileConfig* activeBeforeImport = GetActiveClipboardProfile();
    const std::string activeProfileBeforeImport = activeBeforeImport
        ? activeBeforeImport->id : std::string{};

    auto profiles = GetClipboardProfiles();
    for (const auto& incoming : data.profiles) {
        const auto found = std::find_if(profiles.begin(), profiles.end(),
            [&](const auto& value) {
                return Lowercase(value.name) == Lowercase(incoming.name);
            });
        if (found != profiles.end()) {
            profileIdMap[incoming.id] = found->id;
            if (policy == state_package::ConflictPolicy::Replace) {
                if (m_clipboardProfiles && m_clipboardProfiles->UpdateProfileDefinition(
                        found->id, incoming.name, incoming.processName)) ++replaced;
                else failed = true;
            } else if (policy == state_package::ConflictPolicy::KeepBoth) {
                const std::string name = UniqueImportedName(incoming.name, profiles,
                    [](const auto& value) { return value.name; });
                CreateClipboardProfile(name, incoming.processName);
                profiles = GetClipboardProfiles(); ++imported;
                if (const auto* created = GetActiveClipboardProfile())
                    profileIdMap[incoming.id] = created->id;
            } else ++skipped;
        } else {
            CreateClipboardProfile(incoming.name, incoming.processName);
            profiles = GetClipboardProfiles(); ++imported;
            if (const auto* created = GetActiveClipboardProfile())
                profileIdMap[incoming.id] = created->id;
        }
    }
    if (!activeProfileBeforeImport.empty())
        SetActiveClipboardProfile(activeProfileBeforeImport);

    auto slots = GetNamedSlots();
    for (auto incoming : data.namedSlots) {
        const int64_t sourceId = incoming.slotId;
        const auto found = std::find_if(slots.begin(), slots.end(),
            [&](const auto& value) { return Lowercase(value.name) == Lowercase(incoming.name); });
        if (found != slots.end()) {
            if (policy == state_package::ConflictPolicy::Skip) {
                namedSlotIdMap[sourceId] = found->slotId; ++skipped; continue;
            }
            if (policy == state_package::ConflictPolicy::Replace) {
                incoming.slotId = found->slotId; ++replaced;
            } else {
                incoming.slotId = 0;
                incoming.name = UniqueImportedName(incoming.name, slots,
                    [](const auto& value) { return value.name; });
                ++imported;
            }
        } else { incoming.slotId = 0; ++imported; }
        if (!SaveNamedSlot(incoming)) { failed = true; continue; }
        namedSlotIdMap[sourceId] = incoming.slotId;
        slots = GetNamedSlots();
    }

    auto transforms = GetRegexTransforms();
    for (auto incoming : data.transforms) {
        const auto found = std::find_if(transforms.begin(), transforms.end(),
            [&](const auto& value) { return Lowercase(value.name) == Lowercase(incoming.name); });
        if (found != transforms.end()) {
            if (policy == state_package::ConflictPolicy::Skip) { ++skipped; continue; }
            if (policy == state_package::ConflictPolicy::Replace) {
                incoming.transformId = found->transformId; ++replaced;
            } else {
                incoming.transformId = 0;
                incoming.name = UniqueImportedName(incoming.name, transforms,
                    [](const auto& value) { return value.name; }); ++imported;
            }
        } else { incoming.transformId = 0; ++imported; }
        if (!SaveRegexTransform(incoming)) failed = true;
        transforms = GetRegexTransforms();
    }

    auto templates = GetPasteTemplates();
    for (auto incoming : data.templates) {
        const auto found = std::find_if(templates.begin(), templates.end(),
            [&](const auto& value) { return Lowercase(value.name) == Lowercase(incoming.name); });
        if (found != templates.end()) {
            if (policy == state_package::ConflictPolicy::Skip) { ++skipped; continue; }
            if (policy == state_package::ConflictPolicy::Replace) {
                incoming.templateId = found->templateId; ++replaced;
            } else {
                incoming.templateId = 0;
                incoming.name = UniqueImportedName(incoming.name, templates,
                    [](const auto& value) { return value.name; }); ++imported;
            }
        } else { incoming.templateId = 0; ++imported; }
        if (!SavePasteTemplate(incoming)) failed = true;
        templates = GetPasteTemplates();
    }

    auto actions = GetCustomActions();
    for (auto incoming : data.actions) {
        const auto found = std::find_if(actions.begin(), actions.end(),
            [&](const auto& value) { return Lowercase(value.label) == Lowercase(incoming.label); });
        if (found != actions.end()) {
            if (policy == state_package::ConflictPolicy::Skip) { ++skipped; continue; }
            if (policy == state_package::ConflictPolicy::Replace) {
                incoming.actionId = found->actionId; ++replaced;
            } else {
                incoming.actionId = 0;
                incoming.label = UniqueImportedName(incoming.label, actions,
                    [](const auto& value) { return value.label; }); ++imported;
            }
        } else { incoming.actionId = 0; ++imported; }
        if (!SaveCustomAction(incoming)) failed = true;
        actions = GetCustomActions();
    }

    if (!data.configurationJson.empty()) {
        if (policy == state_package::ConflictPolicy::Replace ||
            !std::filesystem::exists(ConfigStore::Path())) {
            try {
                nlohmann::json config = nlohmann::json::parse(data.configurationJson);
                if (config.contains("hotkeys") && config["hotkeys"].contains("bindings")) {
                    for (auto& binding : config["hotkeys"]["bindings"]) {
                        if (binding.value("action", -1) ==
                            static_cast<int>(HotkeyAction::PasteNamedSlot)) {
                            const int64_t source = binding.value("data", int64_t{});
                            const auto remapped = namedSlotIdMap.find(source);
                            if (remapped != namedSlotIdMap.end())
                                binding["data"] = remapped->second;
                        }
                    }
                }
                const std::string importedActive =
                    config.value("activeClipboardId", std::string{});
                const auto remappedActive = profileIdMap.find(importedActive);
                if (remappedActive != profileIdMap.end())
                    config["activeClipboardId"] = remappedActive->second;
                if (config.contains("customFilters") &&
                    config["customFilters"].is_array()) {
                    for (auto& filter : config["customFilters"]) {
                        const std::string source =
                            filter.value("routeProfileId", std::string{});
                        const auto remapped = profileIdMap.find(source);
                        if (remapped != profileIdMap.end())
                            filter["routeProfileId"] = remapped->second;
                    }
                }
                std::string configError;
                if (!state_package::StageConfigurationImport(
                        ConfigStore::Directory(), config.dump(2), &configError)) {
                    message = "Definitions imported, but configuration staging failed: " + configError;
                    return false;
                }
                restartRequired = true; ++replaced;
            } catch (const std::exception& ex) {
                message = std::string("Definitions imported, but configuration is invalid: ") + ex.what();
                return false;
            }
        } else ++skipped;
    }

    std::ostringstream summary;
    summary << (failed ? "Import completed with persistence errors: " : "State package imported: ")
            << imported << " added, " << replaced << " replaced, "
            << skipped << " skipped.";
    if (restartRequired) summary << " Restart to apply imported app settings.";
    message = summary.str();
    return !failed;
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
    return m_clipboardProfiles ? m_clipboardProfiles->ActiveProfile() : nullptr;
}

bool Application::IsClipboardProfileLoaded(const std::string& id) const {
    return m_clipboardProfiles && m_clipboardProfiles->IsHistoryLoaded(id);
}

const std::vector<ClipboardProfileConfig>& Application::GetClipboardProfiles() const {
    return m_clipboardProfiles ? m_clipboardProfiles->Profiles() : m_config.clipboards;
}

bool Application::CanDeleteActiveClipboardProfile() const {
    return m_clipboardProfiles && m_clipboardProfiles->CanDeleteActiveProfile();
}

const std::vector<std::string>& Application::GetHistoryPersistenceErrors() const {
    static const std::vector<std::string> empty;
    return m_clipboardProfiles ? m_clipboardProfiles->PersistenceErrors() : empty;
}

RuntimeTelemetry Application::GetRuntimeTelemetry() const {
    RuntimeTelemetry telemetry;
    if (m_history) {
        const auto [historyBytes, formatBytes] = m_history->EstimatedMemoryBytes();
        telemetry.historyBytes = historyBytes;
        telemetry.formatBytes = formatBytes;
    }
    if (m_popup)
        telemetry.thumbnailBytes = m_popup->ThumbnailMemoryBytes();
    if (m_clipboardProfiles)
        telemetry.databaseQueryMs = m_clipboardProfiles->LastDatabaseQueryMs();
    telemetry.renderFrameMs = m_renderFrameMs;
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::minutes(1);
    while (!m_clipboardEventTimes.empty() &&
           m_clipboardEventTimes.front() < cutoff)
        m_clipboardEventTimes.pop_front();
    telemetry.clipboardEventsLastMinute = m_clipboardEventTimes.size();
    return telemetry;
}

void Application::SetActiveClipboardProfile(const std::string& id) {
    if (m_clipboardProfiles)
        m_clipboardProfiles->SetActiveProfile(id);
}

void Application::SelectClipboardProfileSlot(int slot) {
    if (m_clipboardProfiles)
        m_clipboardProfiles->SelectProfileSlot(slot);
}

void Application::CreateClipboardProfile(const std::string& name,
                                         const std::string& processName) {
    if (m_clipboardProfiles)
        m_clipboardProfiles->CreateProfile(name, processName);
}

void Application::RenameActiveClipboardProfile(const std::string& name) {
    if (m_clipboardProfiles)
        m_clipboardProfiles->RenameActiveProfile(name);
}

bool Application::DeleteActiveClipboardProfile() {
    return m_clipboardProfiles && m_clipboardProfiles->DeleteActiveProfile();
}

void Application::CreateClipboardFromForegroundProcess() {
#ifdef NDEBUG
    return;
#else
    const std::string process = ForegroundProcessName();
    if (m_clipboardProfiles)
        m_clipboardProfiles->CreateFromForegroundProcess(process);
#endif
}

void Application::BindActiveClipboardToForegroundProcess() {
#ifdef NDEBUG
    return;
#else
    const std::string process = ForegroundProcessName();
    if (m_clipboardProfiles)
        m_clipboardProfiles->BindActiveToProcess(process);
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
    if (m_clipboardProfiles)
        m_clipboardProfiles->SaveHistory(profileId);
}

void Application::SaveActiveClipboardHistory() {
    if (m_clipboardProfiles)
        m_clipboardProfiles->SaveActiveHistory();
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

    AddDeveloperEvent("added screenshot path + CF_DIB rows");
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

            if (Application* app = Application::Get(); app && !app->IsIncognito())
                app->AddScreenshotPair(history, path, std::move(imageItem), newAtTop);
            return;
        }
        if (Application* app = Application::Get())
            app->AddDeveloperEvent("screenshot dropped: no matching file path found");
    }).detach();
}

void Application::ApplyLoadedConfig(const AppConfig& config, bool rebuildHistories) {
    m_config = config;
    if (!m_clipboardProfiles) {
        m_clipboardProfiles = std::make_unique<ClipboardProfileManager>(
            m_config,
            [this]() { SaveConfig(); },
            [this](const std::string& event) { AddDeveloperEvent(event); },
            [this]() { return ForegroundProcessName(); },
            [this](ClipboardHistory* history) { m_history = history; },
            [this](const std::string& name, double durationMs) {
                m_startupProfiler.RecordDuration(name, durationMs);
            }, m_safeModeRequested);
    }
    if (m_androidIntegration)
        m_androidIntegration->SetDeviceEndpoint(m_config.android.deviceEndpoint);
    m_appearance = m_config.appearance;
    m_appearance.uiScale = 1.0f;
    m_appearance.dpiScale = win32util::DpiScaleForWindow(m_hwnd);
    m_hotkeySettings = m_config.hotkeys;
    m_appearanceDirty = true;
    if (rebuildHistories)
        RebuildClipboardHistories();

    if (m_history)
        m_history->SetNewItemsAtTop(m_config.newItemsAtTop);
    if (m_popup) {
        m_popup->SetAppendNewlineAfterPaste(m_config.appendNewlineAfterPaste);
        m_popup->SetPasteMoveTarget(GetPasteMoveTarget());
    }
    if (m_editor)
        m_editor->ApplySettings(m_config.editor);
    SyncCustomActionHotkeys();
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
    if (m_clipboardProfiles) {
        m_clipboardProfiles->Rebuild();
        InvalidateDatabaseCaches();
    }
}

void Application::InvalidateDatabaseCaches() {
    m_namedSlotsCached = false;
    m_regexTransformsCached = false;
    m_pasteTemplatesCached = false;
    m_customActionsCached = false;
    m_namedSlotsCache.clear();
    m_regexTransformsCache.clear();
    m_pasteTemplatesCache.clear();
    m_customActionsCache.clear();
}

ClipboardHistory* Application::HistoryForProfile(const std::string& profileId) {
    return m_clipboardProfiles
        ? m_clipboardProfiles->HistoryForProfile(profileId)
        : nullptr;
}

void Application::SwitchClipboardForProcess(const std::string& processName) {
#ifdef NDEBUG
    (void)processName;
#else
    if (m_clipboardProfiles)
        m_clipboardProfiles->SwitchForProcess(processName);
#endif
}

bool Application::HandleClipboardTextCommand(const COPYDATASTRUCT& cds) {
    if (!m_history || cds.dwData != CD_CLIPBOARD_TEXT ||
        !cds.lpData ||
        cds.cbData < sizeof(ClipboardTextCommand) + sizeof(wchar_t))
        return false;

    ClipboardTextCommand command{};
    std::memcpy(&command, cds.lpData, sizeof(command));
    const size_t headerBytes = sizeof(ClipboardTextCommand);
    const size_t textBytes = cds.cbData - headerBytes;
    if (textBytes < sizeof(wchar_t) || textBytes % sizeof(wchar_t) != 0)
        return false;

    const auto* text = reinterpret_cast<const wchar_t*>(
        static_cast<const unsigned char*>(cds.lpData) + headerBytes);
    const size_t maxChars = textBytes / sizeof(wchar_t);
    const size_t chars = wcsnlen_s(text, maxChars);
    if (chars == 0 || chars == maxChars)
        return false;

    const std::vector<std::wstring> filePaths = win32util::ExistingPathList(text);

    int utf8Bytes = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(chars),
                                        nullptr, 0, nullptr, nullptr);
    if (utf8Bytes <= 0)
        return false;

    ClipboardItem item;
    item.type = filePaths.empty() ? ContentType::Text : ContentType::FilePaths;
    item.text.resize(static_cast<size_t>(utf8Bytes));
    WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(chars),
                        item.text.data(), utf8Bytes, nullptr, nullptr);
    item.sourceProcess = "clipboardpp.exe";
    item.tags = ContentDetector::DetectTags(item.text);

    size_t index = 0;
    if (command.position == -1) {
        index = m_history->Size();
    } else if (command.position > 0) {
        index = static_cast<size_t>(std::clamp(command.position, 1, kMaxClipboardHistoryItems) - 1);
    }

    if (command.setSystemClipboard) {
        if (m_monitor)
            m_monitor->BeginSelfWrite();
        if (!filePaths.empty())
            win32util::SetClipboardFileDrop(m_hwnd, filePaths);
        else
            win32util::SetClipboardUnicodeText(m_hwnd, text, chars);
        if (m_monitor)
            m_monitor->EndSelfWrite();
    }

    m_history->Insert(std::move(item), index);
    return true;
}

bool Application::HandleHistoryMutationCommand(const COPYDATASTRUCT& cds) {
    if (cds.dwData != CD_HISTORY_MUTATION || !cds.lpData)
        return false;
    const size_t headerBytes = sizeof(HistoryMutationCommand);
    if (cds.cbData < headerBytes + sizeof(wchar_t))
        return false;

    HistoryMutationCommand command{};
    std::memcpy(&command, cds.lpData, sizeof(command));
    if (command.version != kHistoryMutationVersion)
        return false;
    const size_t profileBytes = cds.cbData - headerBytes;
    if (profileBytes % sizeof(wchar_t) != 0)
        return false;
    const auto* profileIdText = reinterpret_cast<const wchar_t*>(
        static_cast<const unsigned char*>(cds.lpData) + headerBytes);
    const size_t maxChars = profileBytes / sizeof(wchar_t);
    const size_t chars = wcsnlen_s(profileIdText, maxChars);
    if (chars == 0 || chars == maxChars)
        return false;

    const std::string profileId = win32util::WideToUtf8(
        profileIdText, static_cast<int>(chars));
    ClipboardHistory* history = HistoryForProfile(profileId);
    if (!history)
        return false;

    bool changed = false;
    switch (static_cast<HistoryMutationOperation>(command.operation)) {
    case HistoryMutationOperation::Delete:
        changed = command.itemId != 0 && history->RemoveItemById(command.itemId);
        break;
    case HistoryMutationOperation::Pin:
        changed = command.itemId != 0 && history->SetPinnedById(command.itemId, true);
        break;
    case HistoryMutationOperation::Unpin:
        changed = command.itemId != 0 && history->SetPinnedById(command.itemId, false);
        break;
    case HistoryMutationOperation::Clear:
        history->Clear();
        changed = true;
        break;
    default:
        return false;
    }
    if (changed)
        AddDeveloperEvent("CLI history mutation applied to profile: " + profileId);
    return changed;
}

// -- Private: initialisation ---------------------------------------------------

bool Application::Init() {
    if (backup_restore::HasPendingRestore(ConfigStore::Directory())) {
        const backup_restore::Result restored =
            backup_restore::ApplyPendingRestore(ConfigStore::Directory());
        if (!restored.ok) {
            const std::wstring message = win32util::Utf8ToWide(restored.message);
            MessageBoxW(nullptr, message.c_str(), L"Clipboard++ restore",
                        MB_OK | MB_ICONERROR | MB_TOPMOST);
            if (!restored.safeToContinue)
                return false;
        }
    }
    std::string configurationImportError;
    if (!state_package::ApplyPendingConfigurationImport(
            ConfigStore::Directory(), &configurationImportError)) {
        const std::wstring message = win32util::Utf8ToWide(
            "A pending configuration import could not be applied: " +
            configurationImportError);
        MessageBoxW(nullptr, message.c_str(), L"Clipboard++ state import",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
    auto stageStarted = m_startupProfiler.BeginStage();
    AppConfig loadedConfig = ConfigStore::Load();
    m_startupProfiler.FinishStage("config load", stageStarted);

    stageStarted = m_startupProfiler.BeginStage();
    ApplyLoadedConfig(loadedConfig, false);
    m_startupProfiler.FinishStage("config apply (storage deferred)", stageStarted);

    stageStarted = m_startupProfiler.BeginStage();
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
    m_startupProfiler.FinishStage("main window class + HWND", stageStarted);

    stageStarted = m_startupProfiler.BeginStage();
    if (!CreateD3D()) {
        DestroyD3D();
        UnregisterClassW(L"ClipboardPlusPlus_Main", m_hInstance);
        return false;
    }
    m_startupProfiler.FinishStage("Direct3D device + swap chain", stageStarted);

    // ImGui context
    stageStarted = m_startupProfiler.BeginStage();
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
    m_startupProfiler.FinishStage("ImGui context + font atlas", stageStarted);

    stageStarted = m_startupProfiler.BeginStage();
    m_tray = std::make_unique<TrayIcon>(m_hwnd, m_hInstance);
    if (!m_tray->Create()) return false;
    m_startupProfiler.FinishStage("tray icon creation", stageStarted);

    stageStarted = m_startupProfiler.BeginStage();
    m_popup = std::make_unique<PopupWindow>();
    if (!m_popup->Create(m_hInstance, m_d3dDevice, m_d3dContext))
        return false;
    m_popup->ApplyAppearance(m_appearance);
    m_popup->SetAppendNewlineAfterPaste(m_config.appendNewlineAfterPaste);
    m_popup->SetPasteMoveTarget(GetPasteMoveTarget());
    m_startupProfiler.FinishStage("quick-paste popup creation", stageStarted);

    stageStarted = m_startupProfiler.BeginStage();
    m_hotkeys = std::make_unique<HotkeyManager>();
    m_hotkeys->Install(m_hwnd);
    SyncCustomActionHotkeys();
    m_startupProfiler.FinishStage("global hotkey installation", stageStarted);

    // TODO (Milestone 5): only show on first launch
    stageStarted = m_startupProfiler.BeginStage();
    ShowMainWindow();
    m_startupProfiler.FinishStage("show main window", stageStarted);

    // The initial atlas already reflects the loaded appearance. Avoid rebuilding
    // it again in the first frame; only later user changes mark appearance dirty.
    ImGui::GetStyle().HoverDelayShort =
        static_cast<float>(std::clamp(m_config.ui.helperDelayMs, 0, 5000)) / 1000.0f;
    ImGui::GetStyle().HoverDelayNormal = ImGui::GetStyle().HoverDelayShort;
    if (m_tray)
        m_tray->ApplyTheme(m_appearance);
    m_appearanceDirty = false;

    m_running = true;
    return true;
}

void Application::PollBackgroundPersistenceFailure() {
    if (m_clipboardProfiles && !m_safeMode) {
        std::string persistenceFailure;
        if (m_clipboardProfiles->ConsumeBackgroundPersistenceFailure(
                persistenceFailure)) {
            if (m_monitor) m_monitor->Stop();
            m_clipboardProfiles->EnterSafeMode(
                "Safe mode: " + persistenceFailure);
            m_safeMode = true;
            AddDeveloperEvent("safe mode entered after a background persistence failure");
            OpenSettingsWindow();
        }
    }
}

void Application::AdvanceDeferredStartup() {
    const auto started = m_startupProfiler.BeginStage();
    switch (m_deferredStartupPhase) {
    case DeferredStartupPhase::ProfileMetadata:
        if (!m_clipboardProfiles) {
            m_deferredStartupPhase = DeferredStartupPhase::ActiveHistory;
            break;
        }
        if (!m_clipboardProfiles->CanInitializeMetadataAsync()) {
            if (!m_clipboardProfiles->InitializeProfileMetadata() &&
                m_clipboardProfiles->IsSafeMode())
                m_safeMode = true;
            InvalidateDatabaseCaches();
            SyncCustomActionHotkeys();
            m_startupProfiler.FinishStage("deferred profile metadata", started);
            m_deferredStartupPhase = DeferredStartupPhase::ActiveHistory;
            break;
        }
        if (!m_profileMetadataLoad.valid()) {
            const std::filesystem::path databasePath =
                ConfigStore::Directory() / "clipboard.db";
            m_profileMetadataLoadStarted = m_startupProfiler.BeginStage();
            m_profileMetadataLoad = std::async(
                std::launch::async,
                [databasePath]() {
                    return ClipboardProfileManager::LoadProfileMetadataDetached(
                        databasePath);
                });
            break;
        }
        if (m_profileMetadataLoad.wait_for(std::chrono::milliseconds(0)) !=
            std::future_status::ready)
            break;
        {
            ClipboardProfileMetadataLoadResult result =
                m_profileMetadataLoad.get();
            m_startupProfiler.RecordDuration(
                "encrypted clipboard DB + profile metadata", result.durationMs);
            if (!m_clipboardProfiles->InstallDetachedProfileMetadata(
                    std::move(result))) {
                m_safeMode = true;
                AddDeveloperEvent("safe mode entered after clipboard database startup failure");
            }
        }
        InvalidateDatabaseCaches();
        SyncCustomActionHotkeys();
        m_startupProfiler.FinishStage(
            "deferred profile metadata", m_profileMetadataLoadStarted);
        m_deferredStartupPhase = DeferredStartupPhase::ActiveHistory;
        break;
    case DeferredStartupPhase::ActiveHistory:
        if (!m_clipboardProfiles) {
            m_deferredStartupPhase = DeferredStartupPhase::ImageStoreAndMonitor;
            break;
        }
        if (!m_clipboardProfiles->CanLoadHistoryAsync()) {
            m_clipboardProfiles->LoadActiveHistory();
            m_startupProfiler.FinishStage("deferred active profile hydration", started);
            m_deferredStartupPhase = DeferredStartupPhase::ImageStoreAndMonitor;
            break;
        }
        if (!m_activeHistoryLoad.valid()) {
            const std::string profileId = m_config.activeClipboardId;
            const std::filesystem::path databasePath =
                ConfigStore::Directory() / "clipboard.db";
            m_activeHistoryLoadStarted = m_startupProfiler.BeginStage();
            m_activeHistoryLoad = std::async(
                std::launch::async,
                [databasePath, profileId]() {
                    return ClipboardProfileManager::LoadHistoryDetached(
                        databasePath, profileId);
                });
            break;
        }
        if (m_activeHistoryLoad.wait_for(std::chrono::milliseconds(0)) !=
            std::future_status::ready)
            break;
        {
            ClipboardHistoryLoadResult result = m_activeHistoryLoad.get();
            m_startupProfiler.RecordDuration(
                "active history deserialization", result.durationMs);
            if (!m_clipboardProfiles->InstallDetachedHistory(std::move(result))) {
                m_clipboardProfiles->EnterSafeMode(
                    "Safe mode: the active encrypted history could not be read. Clipboard capture and storage writes are disabled until recovery.");
                m_safeMode = true;
            }
        }
        m_startupProfiler.FinishStage(
            "deferred active profile hydration", m_activeHistoryLoadStarted);
        m_deferredStartupPhase = DeferredStartupPhase::ImageStoreAndMonitor;
        break;
    case DeferredStartupPhase::ImageStoreAndMonitor:
        InitializeImageStoreAndMonitor();
        m_startupProfiler.FinishStage("deferred images + clipboard listener", started);
        m_deferredStartupPhase = DeferredStartupPhase::AndroidIntegration;
        break;
    case DeferredStartupPhase::AndroidIntegration:
        if (m_androidIntegration && m_androidIntegration->StartServer())
            LogDebug("Android sync server listening on port 8766");
        else
            LogDebug("Android sync server failed to start on port 8766");
        m_startupProfiler.FinishStage("deferred Android integration", started);
        m_deferredStartupPhase = DeferredStartupPhase::Maintenance;
        break;
    case DeferredStartupPhase::Maintenance:
        if (m_clipboardProfiles)
            m_clipboardProfiles->ApplyVaultLimit();
        m_startupProfiler.RecordMetric(
            "profile count", std::to_string(m_config.clipboards.size()));
        m_startupProfiler.RecordMetric(
            "active history item count",
            std::to_string(m_history ? m_history->Size() : 0));
        {
            std::error_code error;
            const auto clipboardDb = ConfigStore::Directory() / "clipboard.db";
            const auto imageDb = ConfigStore::Directory() / "images.db";
            const uintmax_t clipboardDbBytes =
                std::filesystem::file_size(clipboardDb, error);
            m_startupProfiler.RecordMetric(
                "clipboard.db bytes",
                error ? "unavailable" : std::to_string(clipboardDbBytes));
            error.clear();
            const uintmax_t imageDbBytes =
                std::filesystem::file_size(imageDb, error);
            m_startupProfiler.RecordMetric(
                "images.db bytes",
                error ? "unavailable" : std::to_string(imageDbBytes));
        }
        if (m_imageStore)
            m_startupProfiler.RecordMetric(
                "image count", std::to_string(m_imageStore->ListAll().size()));
        m_startupProfiler.RecordMetric(
            "active vault item count", std::to_string(GetVaultCount()));
        m_startupProfiler.FinishStage("deferred vault pruning", started);
        m_deferredStartupPhase = DeferredStartupPhase::Complete;
        m_startupProfiler.WriteReport(
            ConfigStore::Directory() / "startup_profile.log");
        LogDebug("Deferred startup complete");
        break;
    case DeferredStartupPhase::AwaitFirstFrame:
    case DeferredStartupPhase::Complete:
        break;
    }
}

void Application::InitializeImageStoreAndMonitor() {
    if (m_safeMode) {
        LogDebug("Safe mode: image storage and clipboard monitoring were not started");
        return;
    }
    if (!m_imageStore) {
        m_imageStore = std::make_unique<ImageStore>();
        if (!m_imageStore->Open(ConfigStore::Directory() / "images.db")) {
            m_imageStore.reset();
            m_safeMode = true;
            if (m_clipboardProfiles)
                m_clipboardProfiles->EnterSafeMode(
                    "Safe mode: encrypted image storage could not be opened. Clipboard capture and storage writes are disabled until recovery.");
            LogDebug("Safe mode entered after image database startup failure");
            return;
        }
        m_imageStore->SetSettings(m_config.images);
        m_imageStore->SetProtectedImageIdsProvider([this]() {
            return m_clipboardProfiles
                ? m_clipboardProfiles->ReferencedImageIds()
                : std::unordered_set<std::string>{};
        });
    }
    if (m_monitor)
        return;

    m_monitor = std::make_unique<ClipboardMonitor>();
    m_monitor->SetImageStore(m_imageStore.get());
    m_monitor->SetProfileIdGetter([this]() -> std::string {
        if (const ClipboardProfileConfig* p = GetActiveClipboardProfile())
            return p->id;
        return "default";
    });
    m_monitor->Start(m_hInstance, [this](ClipboardItem item) {
        m_clipboardEventTimes.push_back(std::chrono::steady_clock::now());
        if (!item.sourceProcess.empty())
            m_lastForegroundProcess = item.sourceProcess;
        SwitchClipboardForProcess(item.sourceProcess);
        if (!m_history)
            return;

        AddDeveloperEvent("captured " + std::string(ContentTypeName(item.type)) +
                          " tags=" + Hex32(item.tags) +
                          " bytes=" + std::to_string(item.text.size()) +
                          " formats=" + std::to_string(item.formats.size()));

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
    });
}

void Application::Shutdown() {
    if (m_androidIntegration)
        m_androidIntegration->StopServer();
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
    const auto runtimeFrameStarted = std::chrono::steady_clock::now();
    const auto firstFrameStarted = m_startupProfiler.BeginStage();
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

        if (!m_startupProfileComplete) {
            m_startupProfiler.FinishStage("first frame render", firstFrameStarted);
            const double totalMs = m_startupProfiler.ElapsedMs();
            m_startupProfiler.RecordDuration("total to first rendered frame", totalMs);
            m_startupProfileComplete = true;
            const std::filesystem::path reportPath =
                ConfigStore::Directory() / "startup_profile.log";
            m_startupProfiler.WriteReport(reportPath);
            for (const StartupTiming& timing : m_startupProfiler.Timings()) {
                std::ostringstream line;
                line << "Startup: " << timing.name
                     << " duration=" << std::fixed << std::setprecision(3)
                     << timing.durationMs << "ms"
                     << " completed=" << timing.completedAtMs << "ms";
                LogDebug(line.str());
            }
            LogDebug("Startup profile written to " + reportPath.u8string());
            m_deferredStartupPhase = DeferredStartupPhase::ProfileMetadata;
        }
    }

    // Popup has its own context + swap chain - rendered separately
    if (m_popup) m_popup->Render();
    if (m_trayPopup) m_trayPopup->Render();
    if (m_editor) m_editor->Render();
    if (m_debugWindow) m_debugWindow->Render();

    if (m_startupProfileComplete)
        PollBackgroundPersistenceFailure();
    if (m_startupProfileComplete &&
        m_deferredStartupPhase != DeferredStartupPhase::Complete)
        AdvanceDeferredStartup();
    const double frameMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - runtimeFrameStarted).count();
    m_renderFrameMs = m_renderFrameMs == 0.0
        ? frameMs : (m_renderFrameMs * 0.9 + frameMs * 0.1);
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
        // Returning 0 for wParam==TRUE discards the NC area - entire window rect
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
        if (app) {
            const auto& cds = *reinterpret_cast<COPYDATASTRUCT*>(lParam);
            if (cds.dwData == CD_CLIPBOARD_TEXT)
                return app->HandleClipboardTextCommand(cds) ? TRUE : FALSE;
            if (cds.dwData == CD_HISTORY_MUTATION)
                return app->HandleHistoryMutationCommand(cds) ? TRUE : FALSE;
        }
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
            app->ToggleIncognito();
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
        case HotkeyAction::PasteSelectedItems:
            if (app->m_popup)
                app->m_popup->PasteSelectedItems();
            break;
        case HotkeyAction::ClearSelectedItems:
            if (app->m_popup)
                app->m_popup->ClearSelectedItems();
            break;
        case HotkeyAction::PasteNamedSlot: {
            HWND target = GetForegroundWindow();
            if (app->m_popup)
                app->m_popup->PasteNamedSlot(data, target);
            break;
        }
        case HotkeyAction::RunCustomAction: {
            HWND target = GetForegroundWindow();
            app->SyncClipboardForWindow(target);
            if (app->m_popup)
                app->m_popup->RunCustomAction(data, target);
            break;
        }
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
