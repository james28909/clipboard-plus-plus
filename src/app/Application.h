#pragma once
#include <windows.h>
#include <d3d11.h>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>
#include "ConfigStore.h"
#include "StartupProfiler.h"
#include "../android/AndroidIntegration.h"
#include "../clipboard/ClipboardHistory.h"
#include "../clipboard/ClipboardDatabase.h"
#include "../clipboard/ClipboardProfileManager.h"
#include "../ui/Appearance.h"
#include "../hotkeys/HotkeyManager.h"
#include "../ipc/IpcProtocol.h"

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
    const std::vector<StartupTiming>& GetStartupTimings() const {
        return m_startupProfiler.Timings();
    }
    const std::vector<StartupMetric>& GetStartupMetrics() const {
        return m_startupProfiler.Metrics();
    }
    double GetStartupElapsedMs() const { return m_startupProfiler.ElapsedMs(); }

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
    const PopupSettings& GetPopupSettings() const { return m_config.popup; }
    void SetPopupSettings(const PopupSettings& settings);
    const std::vector<CustomFilter>& GetCustomFilters() const { return m_config.customFilters; }
    void SetCustomFilters(const std::vector<CustomFilter>& filters);
    const std::vector<std::string>& GetPopupButtonOrder() const { return m_config.popupButtonOrder; }
    void SetPopupButtonOrder(const std::vector<std::string>& order);
    bool GetHidePopupOnOutsideClick() const { return m_config.hidePopupOnOutsideClick; }
    void SetHidePopupOnOutsideClick(bool value);
    void AddDeveloperEvent(const std::string& event);
    void RecordGeneratedPaste(const std::string& sourceProcess,
                              const std::string& destinationProcess);
    const std::vector<std::string>& GetDeveloperEvents() const { return m_developerEvents; }
    const std::string& GetLastGeneratedPasteSource() const { return m_lastGeneratedPasteSource; }
    const std::string& GetLastGeneratedPasteDestination() const { return m_lastGeneratedPasteDestination; }
    void ClearDeveloperEvents() { m_developerEvents.clear(); }
    bool GetNewItemsAtTop() const { return m_config.newItemsAtTop; }
    void SetNewItemsAtTop(bool value);
    int GetActiveHistoryLimit() const { return m_config.activeHistoryLimit; }
    void SetActiveHistoryLimit(int value);
    bool IsHistoryDeduplicationEnabled() const { return m_config.deduplicateHistory; }
    void SetHistoryDeduplicationEnabled(bool enabled);
    bool IsVaultUnlimited() const { return m_config.vaultUnlimited; }
    int GetVaultLimitMB() const { return m_config.vaultLimitMB; }
    void SetVaultLimit(bool unlimited, int limitMB);
    size_t GetVaultCount() const;
    std::vector<ClipboardVaultEntry> SearchVault(const std::string& query) const;
    bool PromoteVaultItem(int64_t archiveId);
    bool DeleteVaultItem(int64_t archiveId);
    std::vector<NamedClipboardSlot> GetNamedSlots() const;
    bool SaveNamedSlot(NamedClipboardSlot& slot);
    bool DeleteNamedSlot(int64_t slotId);
    std::vector<RegexTransformDefinition> GetRegexTransforms() const;
    bool SaveRegexTransform(RegexTransformDefinition& transform);
    bool DeleteRegexTransform(int64_t transformId);
    std::vector<PasteTemplateDefinition> GetPasteTemplates() const;
    bool SavePasteTemplate(PasteTemplateDefinition& value);
    bool DeletePasteTemplate(int64_t templateId);
    std::vector<CustomActionDefinition> GetCustomActions() const;
    bool SaveCustomAction(CustomActionDefinition& action);
    bool DeleteCustomAction(int64_t actionId);
    bool ImportCustomAction(const std::string& payload, std::string* error = nullptr);
    bool CopyTextToClipboard(const std::string& text);
    bool IsStartWithWindowsEnabled() const;
    bool SetStartWithWindowsEnabled(bool enabled);
    bool IsIncognito() const { return m_incognito; }
    void SetIncognito(bool enabled);
    void ToggleIncognito();
    bool GetAppendNewlineAfterPaste() const { return m_config.appendNewlineAfterPaste; }
    void SetAppendNewlineAfterPaste(bool value);
    ClipboardHistory::MoveTarget GetPasteMoveTarget() const;
    void SetPasteMoveTarget(ClipboardHistory::MoveTarget target);
    const std::vector<ClipboardProfileConfig>& GetClipboardProfiles() const;
    const ClipboardProfileConfig* GetActiveClipboardProfile() const;
    bool IsClipboardProfileLoaded(const std::string& id) const;
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
    void ApplyLoadedConfig(const AppConfig& config, bool rebuildHistories = true);
    bool HandleClipboardTextCommand(const COPYDATASTRUCT& cds);
    bool HandleHistoryMutationCommand(const COPYDATASTRUCT& cds);
    bool LaunchExternalEditor();
    void RebuildClipboardHistories();
    void AdvanceDeferredStartup();
    void InitializeImageStoreAndMonitor();
    void InvalidateDatabaseCaches();
    void SyncCustomActionHotkeys();
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
    ClipboardHistory* HistoryForProfile(const std::string& profileId);

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
    bool m_incognito{false};
    AppearanceSettings m_appearance{};
    bool m_appearanceDirty{true};
    HotkeySettings m_hotkeySettings{};
    std::vector<std::string> m_developerEvents;
    std::string m_lastGeneratedPasteSource;
    std::string m_lastGeneratedPasteDestination;
    StartupProfiler m_startupProfiler;
    bool m_startupProfileComplete{false};
    mutable bool m_namedSlotsCached{false};
    mutable bool m_regexTransformsCached{false};
    mutable bool m_pasteTemplatesCached{false};
    mutable bool m_customActionsCached{false};
    mutable std::vector<NamedClipboardSlot> m_namedSlotsCache;
    mutable std::vector<RegexTransformDefinition> m_regexTransformsCache;
    mutable std::vector<PasteTemplateDefinition> m_pasteTemplatesCache;
    mutable std::vector<CustomActionDefinition> m_customActionsCache;
    enum class DeferredStartupPhase {
        AwaitFirstFrame,
        ProfileMetadata,
        ActiveHistory,
        ImageStoreAndMonitor,
        AndroidIntegration,
        Maintenance,
        Complete
    };
    DeferredStartupPhase m_deferredStartupPhase{DeferredStartupPhase::AwaitFirstFrame};
    std::future<ClipboardHistoryLoadResult> m_activeHistoryLoad;
    StartupProfiler::TimePoint m_activeHistoryLoadStarted{};
    std::future<ClipboardProfileMetadataLoadResult> m_profileMetadataLoad;
    StartupProfiler::TimePoint m_profileMetadataLoadStarted{};

    static Application* s_instance;
};
