#pragma once
#include <windows.h>
#include <d3d11.h>
#include <memory>

class TrayIcon;
class ClipboardHistory;
class ClipboardMonitor;
class PopupWindow;

// Message IDs used across the app
constexpr UINT WM_TRAYICON     = WM_APP + 1;
constexpr UINT WM_SHOWCPP_MAIN = WM_APP + 2;
constexpr UINT WM_SHOWPOPUP    = WM_APP + 3;

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

private:
    bool Init();
    void Shutdown();
    void RenderFrame();

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

    bool m_running{false};
    bool m_mainVisible{false};

    static Application* s_instance;
};
