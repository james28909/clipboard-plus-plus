#include <windows.h>
#include <shellapi.h>
#include "app/Application.h"

// Forward declaration — CLI dispatcher lives in src/cli/CLI.cpp (Milestone 6)
static int RunCLI(int argc, wchar_t** argv);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Parse the real command line (WinMain drops args we need)
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    // CLI mode: any argument triggers pipe-based dispatch
    if (argc > 1) {
        int result = RunCLI(argc, argv);
        LocalFree(argv);
        return result;
    }
    LocalFree(argv);

    // Single-instance guard — second launch without args brings window to front
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\ClipboardPlusPlus");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Signal the running instance to show its main window
        HWND existing = FindWindowW(L"ClipboardPlusPlus_Main", nullptr);
        if (existing)
            PostMessageW(existing, WM_APP + 2, 0, 0); // WM_SHOWCPP_MAIN
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    Application app(hInstance);
    int result = app.Run();

    CloseHandle(hMutex);
    return result;
}

// ── Stub — replaced in Milestone 6 ───────────────────────────────────────────
static int RunCLI(int argc, wchar_t** argv) {
    MessageBoxW(nullptr,
        L"CLI support coming in Milestone 6.\n"
        L"Run clipboardpp.exe without arguments to start the tray app.",
        L"Clipboard++", MB_OK | MB_ICONINFORMATION);
    return 0;
}
