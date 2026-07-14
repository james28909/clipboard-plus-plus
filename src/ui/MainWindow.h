#pragma once

struct ClipboardItem;

class MainWindow {
public:
    // Pixel dimensions shared with Application's WM_NCHITTEST handler
    static constexpr int kTitleBarHeight = 32;
    static constexpr int kTitleBtnWidth  = 46;

    // Draw all panels. Called once per frame when the window is visible.
    static void Draw(bool& open);
    static void RequestFocus();
    static void OpenDiffView(const ClipboardItem& left, const ClipboardItem& right);

private:
    static void DrawTitleBar();
    static void DrawSidebarNav(int& selectedSection);
    static void DrawGeneral();
    static void DrawClipboard();
    static void DrawPopupSettings();
    static void DrawHotkeys();
    static void DrawAppearance();
    static void DrawHistory();
    static void DrawFilters();
    static void DrawPasteTools();
    static void DrawEditor();
    static void DrawImages();
    static void DrawAndroid();
    static void DrawPrivacy();
    static void DrawProfiles();
    static void DrawIntegrations();
    static void DrawDeveloper();
    static void DrawAbout();
    static void DrawAboutInfo();
    static void DrawSupport();
    static void DrawDiffView();
};
