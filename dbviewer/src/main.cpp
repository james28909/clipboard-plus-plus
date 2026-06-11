#include "DbViewer.h"
#include <windows.h>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    std::wstring filePath;
    if (lpCmdLine && lpCmdLine[0]) {
        filePath = lpCmdLine;
        // Strip surrounding quotes added by shell launchers (e.g. "C:\path\file.db" -> C:\path\file.db)
        if (filePath.size() >= 2 && filePath.front() == L'"' && filePath.back() == L'"')
            filePath = filePath.substr(1, filePath.size() - 2);
    }

    DbViewerApp app;
    if (!app.Init(hInstance, filePath.empty() ? nullptr : filePath.c_str()))
        return 1;
    return app.Run();
}
