#include "IdeApp.h"

#include <shellapi.h>
#include <windows.h>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    IdeLaunchOptions options;
    for (int i = 1; argv && i < argc; ++i) {
        const std::wstring arg = argv[i] ? argv[i] : L"";
        if ((arg == L"--file" || arg == L"-f") && i + 1 < argc) {
            options.filePath = argv[++i];
        } else if ((arg == L"--mode" || arg == L"-m") && i + 1 < argc) {
            options.mode = argv[++i];
        } else if (arg == L"--return-to-clipboard") {
            options.returnToClipboard = true;
        } else if (arg == L"--wait") {
            options.waitMode = true;
        } else if (!arg.empty() && arg[0] != L'-' && options.filePath.empty()) {
            options.filePath = arg;
        }
    }

    ClipboardIdeApp app;
    const int result = app.Init(hInstance, options) ? app.Run() : 1;

    if (argv)
        LocalFree(argv);
    return result;
}
