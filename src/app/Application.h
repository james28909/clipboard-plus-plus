#pragma once
#include <windows.h>
#include <d3d11.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "ConfigStore.h"
#include "../android/AndroidIntegration.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ClipboardProfileManager.h"
#include "../ui/Appearance.h"
#include "../hotkeys/HotkeyManager.h"

class TrayIcon;
class ClipboardMonitor;
class ImageStore;
class PopupWindow;
class TrayPopupWindow;
class TextEditorWindow;
class DebugWindow;
enum class HotkeyAction : WPARAM;

// Message IDs used across the app
constexpr UINT WM_TRAYICON      = WM_APP + 1;
constexpr UINT WM_SHOWCPP_MAIN  = WM_APP + 2;
constexpr UINT WM_SHOWPOPUP     = WM_APP + 3;
constexpr UINT WM_HOTKEYACTION  = WM_APP + 4;
constexpr UINT WM_RELOAD_CONFIG = WM_APP + 5;
constexpr UINT WM_SHOWTRAYPOPUP = WM_APP + 6;

constexpr ULONG_PTR CD_CLIPBOARD_TEXT = 0x43505031; // "CPP1"

struct ClipboardTextCommand {
    int position; // 0=top, -1=bottom, 1..kMaxClipboardHistoryItems=one-based history slot
    BOOL setSystemClipboard;
    wchar_t text[1];
};

class Application {
public:
    explicit Application(HINSTANCE hInstance);
    ~Application();

    int  Run();
    void ShowMainWindow();
    void OpenSettingsWindow();
    void HideMainWindow();
    void ShowPopup();
    void ShowTrayPopup();
    void ShowEditorPopup();
    void ToggleDebugWindow();
    void LogDebug(const std::string& event);

    static Application* Get()  { return s_instance; }
    HWND GetHwnd()             const { return m_hwnd; }
    ID3D11Device*        GetDevice()  const { return m_d3dDevice; }
    ID3D11DeviceContext* GetContext() const { return m_d3dContext; }
    ClipboardHistory*    GetHistory() const { return m_history; }
    ClipboardMonitor*    GetMonitor() const { return m_monitor.get(); }
    PopupWindow*         GetPopup()   const { return m_popup.get(); }
    TextEditorWindow*    GetEditor()  const { return m_editor.get(); }
    ImageStore*          GetImageStore() const { return m_imageStore.get(); }
    TrayIcon*            GetTray()    const { return m_tray.get(); }
    HotkeyManager*       GetHotkeys() const { return m_hotkeys.get(); }
    const AppConfig&          GetConfig()     const { return m_config; }
    const AppearanceSettings& GetAppearance() const { return m_appearance; }
    void RequestAppearance(const AppearanceSettings& settings);
    void SetImageSettings(const ImageSettings& settings);
    void SaveConfig();
    void SetPopupOpacity(float opacity);
    void SetPopupOutlineStrength(float strength);
    const HotkeySettings& GetHotkeySettings() const { return m_hotkeySettings; }
    void RequestHotkeySettings(const HotkeySettings& settings);
    const DeveloperSettings& GetDeveloperSettings() const { return m_config.developer; }
    void SetDeveloperSettings(const DeveloperSettings& settings);
    const UiSettings& GetUiSettings() const { return m_config.ui; }
    void SetUiSettings(const UiSettings& settings);
    const EditorSettings& GetEditorSettings() const { return m_config.editor; }
    void SetEditorSettings(const EditorSettings& settings);
    const std::vector<CustomFilter>& GetCustomFilters() const { return m_config.customFilters; }
    void SetCustomFilters(const std::vector<CustomFilter>& filters);
    const std::vector<std::string>& GetPopupButtonOrder() const { return m_config.popupButtonOrder; }
    void SetPopupButtonOrder(const std::vector<std::string>& order);
    bool GetHidePopupOnOutsideClick() const { return m_config.hidePopupOnOutsideClick; }
    void SetHidePopupOnOutsideClick(bool value);
    void AddDeveloperEvent(const std::string& event);
    const std::vector<std::string>& GetDeveloperEvents() const { return m_developerEvents; }
    void ClearDeveloperEvents() { m_developerEvents.clear(); }
    bool GetNewItemsAtTop() const { return m_config.newItemsAtTop; }
    void SetNewItemsAtTop(bool value);
    bool IsStartWithWindowsEnabled() const;
    bool SetStartWithWindowsEnabled(bool enabled);
    bool GetAppendNewlineAfterPaste() const { return m_config.appendNewlineAfterPaste; }
    void SetAppendNewlineAfterPaste(bool value);
    ClipboardHistory::MoveTarget GetPasteMoveTarget() const;
    void SetPasteMoveTarget(ClipboardHistory::MoveTarget target);
    const std::vector<ClipboardProfileConfig>& GetClipboardProfiles() const;
    const ClipboardProfileConfig* GetActiveClipboardProfile() const;
    void SetActiveClipboardProfile(const std::string& id);
    void SelectClipboardProfileSlot(int slot);
    void CreateClipboardProfile(const std::string& name, const std::string& processName = {});
    void RenameActiveClipboardProfile(const std::string& name);
    bool DeleteActiveClipboardProfile();
    bool CanDeleteActiveClipboardProfile() const;
    const std::vector<std::string>& GetHistoryPersistenceErrors() const;
    void CreateClipboardFromForegroundProcess();
    void BindActiveClipboardToForegroundProcess();
    bool GetAutoSwitchClipboardByProcess() const { return m_config.autoSwitchClipboardByProcess; }
    void SetAutoSwitchClipboardByProcess(bool value);
    bool GetAutoCreateClipboardByProcess() const { return m_config.autoCreateClipboardByProcess; }
    void SetAutoCreateClipboardByProcess(bool value);
    std::string ForegroundProcessName() const;
    std::string ExecutablePath() const;
    std::string WorkingDirectory() const;
    DWORD ProcessId() const { return GetCurrentProcessId(); }
    SIZE MainWindowCurrentSize() const;
    void UseCurrentMainWindowSizeAsDefault();
    SIZE PopupCurrentSize() const;
    void UseCurrentPopupSizeAsDefault();
    void SyncClipboardForForegroundProcess();
    void SyncClipboardForWindow(HWND hwnd);
    void SendSelectionToAndroidClipboard();
    ID3D11ShaderResourceView* GetAppIconSrv();
    bool InsertExternalClipboardText(const std::string& text,
                                     const std::string& sourceProcess = "external");
    bool AddAndroidClipboardText(const std::string& text,
                                 const std::string& source = "android");
    std::vector<AndroidClipboardEntry> GetAndroidClipboardEntries() const;
    bool RemoveAndroidClipboardEntry(uint64_t id);
    bool SetAndroidClipboardEntryPinned(uint64_t id, bool pinned);
    const std::string& GetAndroidDeviceEndpoint() const;
    void SetAndroidDeviceEndpoint(const std::string& endpoint);
    bool SendTextItemsToAndroid(const std::vector<std::string>& texts, std::string* error = nullptr);
    bool RequestAndroidSyncToWindows(std::string* error = nullptr);
    bool CheckAndroidDeviceHealth(std::string* error = nullptr);
    bool IsAndroidSyncServerRunning() const;
    unsigned short AndroidSyncServerPort() const;

private:
    bool Init();
    void Shutdown();
    void RenderFrame();
    void ApplyAppearanceNow();
    void CommitAppearanceChange();
    bool HasRenderableUi() const;
    void ApplyLoadedConfig(const AppConfig& config);
    bool HandleClipboardTextCommand(const COPYDATASTRUCT& cds);
    bool LaunchExternalEditor();
    void RebuildClipboardHistories();
    void SaveClipboardHistory(const std::string& profileId);
    void SaveActiveClipboardHistory();
    void AddScreenshotPair(ClipboardHistory* history,
                           const std::filesystem::path& path,
                           ClipboardItem imageItem,
                           bool newAtTop);
    void ScheduleScreenshotPairAdd(ClipboardHistory* history,
                                   ClipboardItem imageItem,
                                   bool newAtTop);
    void SwitchClipboardForProcess(const std::string& processName);
    ClipboardHistory* HistoryForProfile(const std::string& profileId) const;

    bool CreateD3D();
    void DestroyD3D();
    void CreateRenderTarget();
    void DestroyRenderTarget();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance{};
    HWND      m_hwnd{};

    ID3D11Device*              m_d3dDevice{};
    ID3D11DeviceContext*       m_d3dContext{};
    IDXGISwapChain*            m_swapChain{};
    ID3D11RenderTargetView*    m_renderTarget{};
    ID3D11ShaderResourceView*  m_appIconSrv{};

    AppConfig m_config{};
    std::unique_ptr<TrayIcon>         m_tray;
    std::unique_ptr<ClipboardProfileManager> m_clipboardProfiles;
    mutable ClipboardHistory* m_history{};
    mutable std::string m_lastForegroundProcess;
    std::unique_ptr<ImageStore>       m_imageStore;
    std::unique_ptr<ClipboardMonitor> m_monitor;
    std::unique_ptr<PopupWindow>      m_popup;
    std::unique_ptr<TrayPopupWindow>  m_trayPopup;
    std::unique_ptr<TextEditorWindow> m_editor;
    std::unique_ptr<DebugWindow>      m_debugWindow;
    std::unique_ptr<HotkeyManager>    m_hotkeys;
    std::unique_ptr<AndroidIntegration> m_androidIntegration;

    bool m_running{false};
    bool m_mainVisible{false};
    AppearanceSettings m_appearance{};
    bool m_appearanceDirty{true};
    HotkeySettings m_hotkeySettings{};
    std::vector<std::string> m_developerEvents;

    static Application* s_instance;
};
