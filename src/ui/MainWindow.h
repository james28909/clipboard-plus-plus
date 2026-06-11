#pragma once

class MainWindow {
public:
    // Pixel dimensions shared with Application's WM_NCHITTEST handler
    static constexpr int kTitleBarHeight = 32;
    static constexpr int kTitleBtnWidth  = 46;

    // Draw all panels. Called once per frame when the window is visible.
    static void Draw(bool& open);
    static void RequestFocus();

private:
    static void DrawTitleBar();
    static void DrawSidebarNav(int& selectedSection);
    static void DrawGeneral();
    static void DrawHotkeys();
    static void DrawAppearance();
    static void DrawHistory();
    static void DrawImages();
    static void DrawPrivacy();
    static void DrawDeveloper();
    static void DrawAbout();
};
