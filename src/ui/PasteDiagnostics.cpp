#include "PasteDiagnostics.h"
#include "../app/Application.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace {

bool Enabled() {
#ifdef NDEBUG
    return false;
#else
    Application* app = Application::Get();
    return app && app->GetDeveloperSettings().eventLogEnabled;
#endif
}

const std::wstring& LogPath() {
    static std::wstring path;
    if (path.empty()) {
        wchar_t appData[MAX_PATH]{};
        GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
        path = std::wstring(appData) + L"\\Clipboard++\\paste_debug.log";
    }
    return path;
}

} // namespace

namespace PasteDiagnostics {

void Log(const char* fmt, ...) {
    if (!Enabled())
        return;

    static DWORD startTick = GetTickCount();
    static bool freshFile = true;

    char msg[512]{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char line[600]{};
    snprintf(line, sizeof(line), "[%6ums] %s\n", GetTickCount() - startTick, msg);
    OutputDebugStringA(line);

    if (FILE* file = _wfopen(LogPath().c_str(), freshFile ? L"w" : L"a")) {
        if (freshFile) {
            fputs("=== Clipboard++ Paste Debug Log ===\n", file);
            freshFile = false;
        }
        fputs(line, file);
        fclose(file);
    }
}

} // namespace PasteDiagnostics
