#pragma once
#include <windows.h>
#include <d3d11.h>
#include <memory>
#include "ConfigStore.h"
#include "../clipboard/ClipboardHistory.h"
#include "../ui/Appearance.h"
#include "../hotkeys/HotkeyManager.h"

class TrayIcon;
class ClipboardMonitor;
class PopupWindow;
enum class HotkeyAction : WPARAM;

// Message IDs used across the app
constexpr UINT WM_TRAYICON     = WM_APP + 1;
constexpr UINT WM_SHOWCPP_MAIN = WM_APP + 2;
constexpr UINT WM_SHOWPOPUP    = WM_APP + 3;
constexpr UINT WM_HOTKEYACTION = WM_APP + 4;

class Application {
public:
    explicit Application(HINSTANCE hInstance);
    ~Application();

    int  Run();
    void ShowMainWindow();
    void HideMainWindow();
    void ShowPopup();

    static Application* Get()  { return s_instance; }
    HWND GetHwnd()             const { return m_hwnd; }
    ID3D11Device*        GetDevice()  const { return m_d3dDevice; }
    ID3D11DeviceContext* GetContext() const { return m_d3dContext; }
    ClipboardHistory*    GetHistory() const { return m_history.get(); }
    ClipboardMonitor*    GetMonitor() const { return m_monitor.get(); }
    PopupWindow*         GetPopup()   const { return m_popup.get(); }
    HotkeyManager*       GetHotkeys() const { return m_hotkeys.get(); }
    const AppearanceSettings& GetAppearance() const { return m_appearance; }
    void RequestAppearance(const AppearanceSettings& settings);
    void SetPopupOpacity(float opacity);
    const HotkeySettings& GetHotkeySettings() const { return m_hotkeySettings; }
    void RequestHotkeySettings(const HotkeySettings& settings);
    bool GetNewItemsAtTop() const { return m_config.newItemsAtTop; }
    void SetNewItemsAtTop(bool value);
    bool GetAppendNewlineAfterPaste() const { return m_config.appendNewlineAfterPaste; }
    void SetAppendNewlineAfterPaste(bool value);
    ClipboardHistory::MoveTarget GetPasteMoveTarget() const;
    void SetPasteMoveTarget(ClipboardHistory::MoveTarget target);

private:
    bool Init();
    void Shutdown();
    void RenderFrame();
    void ApplyAppearanceNow();
    void SaveConfig();

    bool CreateD3D();
    void DestroyD3D();
    void CreateRenderTarget();
    void DestroyRenderTarget();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE m_hInstance{};
    HWND      m_hwnd{};

    ID3D11Device*           m_d3dDevice{};
    ID3D11DeviceContext*    m_d3dContext{};
    IDXGISwapChain*         m_swapChain{};
    ID3D11RenderTargetView* m_renderTarget{};

    std::unique_ptr<TrayIcon>         m_tray;
    std::unique_ptr<ClipboardHistory> m_history;
    std::unique_ptr<ClipboardMonitor> m_monitor;
    std::unique_ptr<PopupWindow>      m_popup;
    std::unique_ptr<HotkeyManager>    m_hotkeys;

    bool m_running{false};
    bool m_mainVisible{false};
    AppearanceSettings m_appearance{};
    bool m_appearanceDirty{true};
    HotkeySettings m_hotkeySettings{};
    AppConfig m_config{};

    static Application* s_instance;
};
