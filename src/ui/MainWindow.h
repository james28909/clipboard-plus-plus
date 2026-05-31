#pragma once

// Static-method wrapper around the ImGui settings window.
// Called once per frame from Application::RenderFrame() when the window is visible.
class MainWindow {
public:
    // Draw all settings panels. Sets open=false if the user closes the window.
    static void Draw(bool& open);

private:
    static void DrawSidebarNav(int& selectedSection);
    static void DrawGeneral();
    static void DrawHotkeys();
    static void DrawAppearance();
    static void DrawHistory();
    static void DrawPrivacy();
    static void DrawDeveloper();
    static void DrawAbout();
};
