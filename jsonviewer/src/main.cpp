#include "JsonViewer.h"
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    const wchar_t* initialFile = (argc > 1 && argv && argv[1][0]) ? argv[1] : nullptr;

    JsonViewerApp app;
    const int result = app.Init(hInstance, initialFile) ? app.Run() : 1;

    if (argv) LocalFree(argv);
    return result;
}
