#include <windows.h>
#include <shlobj.h>

#include "app/Application.h"
#include "app/IconPatcher.h"
#include "cli/CLI.h"
#include "ipc/IpcClient.h"
#include "util/Win32Util.h"

#include <cwchar>
#include <string>
#include <vector>

namespace {

void ConfigureDpiAwareness() {
#ifdef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    ConfigureDpiAwareness();
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    // Internal: spawned by Shutdown() to patch the exe after parent exits.
    // Usage: clipboardpp --patch-icon <exePath> <icoPath> <parentPid>
    if (argc >= 5 && win32util::EqW(argv[1], L"--patch-icon")) {
        DWORD parentPid = static_cast<DWORD>(_wtoi(argv[4]));
        if (parentPid) {
            HANDLE hParent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
            if (hParent) {
                WaitForSingleObject(hParent, 15000);
                CloseHandle(hParent);
            } else {
                Sleep(1000);
            }
        }
        DWORD err = 0;
        bool ok = IconPatcher::PatchExeIcon(argv[2], argv[3], &err);

        // Write result log so failures are always diagnosable
        wchar_t logDir[MAX_PATH]{};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, logDir))) {
            std::wstring logPath = std::wstring(logDir) + L"\\Clipboard++\\iconpatch.log";
            if (FILE* fp = _wfopen(logPath.c_str(), L"w")) {
                fwprintf(fp, L"%s\nexe: %s\nico: %s\nerr: %lu\n",
                         ok ? L"SUCCESS" : L"FAILED",
                         argv[2], argv[3], err);
                fclose(fp);
            }
        }
        return ok ? 0 : 1;
    }

    if (argc > 1 && !win32util::EqW(argv[1], L"--clipboardpp-run-gui"))
        return RunCLI(argc, argv);

    if (argc <= 1) {
        if (HWND existing = ipc::FindRunningInstance()) {
            ipc::SignalRunning(WM_SHOWCPP_MAIN);
            return 0;
        }

        wchar_t module[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, module, MAX_PATH))
            return 1;

        std::wstring command = L"\"";
        command += module;
        command += L"\" --clipboardpp-run-gui";

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');

        BOOL started = CreateProcessW(
            module,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
            nullptr,
            nullptr,
            &si,
            &pi);
        if (!started)
            return 1;

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return 0;
    }

    FreeConsole();

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\ClipboardPlusPlus");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ipc::SignalRunning(WM_SHOWCPP_MAIN);
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    Application app(hInstance);
    int result = app.Run();

    CloseHandle(hMutex);
    return result;
}
