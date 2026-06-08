#include <windows.h>

#include "app/Application.h"
#include "cli/CLI.h"
#include "ipc/IpcClient.h"

#include <cwchar>
#include <string>
#include <vector>

namespace {

bool Eq(const wchar_t* a, const wchar_t* b) {
    return _wcsicmp(a, b) == 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    if (argc > 1 && !Eq(argv[1], L"--clipboardpp-run-gui"))
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
