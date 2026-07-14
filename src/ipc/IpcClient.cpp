#include "IpcClient.h"

#include "app/Application.h"
#include "util/Win32Util.h"

#include <cstddef>
#include <cstring>
#include <vector>

namespace ipc {

HWND FindRunningInstance() {
    return FindWindowW(L"ClipboardPlusPlus_Main", nullptr);
}

void SignalRunning(unsigned int message) {
    if (HWND hwnd = FindRunningInstance()) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid)
            AllowSetForegroundWindow(pid);

        if (message == WM_SHOWCPP_MAIN) {
            DWORD_PTR result = 0;
            SendMessageTimeoutW(hwnd, message, 0, 0,
                                SMTO_ABORTIFHUNG, 2000, &result);
        } else {
            PostMessageW(hwnd, message, 0, 0);
        }
    }
}

bool SendClipboardHistoryText(const std::wstring& text, int position, bool setSystemClipboard) {
    HWND hwnd = FindRunningInstance();
    if (!hwnd)
        return false;

    const size_t headerBytes = sizeof(ClipboardTextCommand);
    const size_t textBytes = (text.size() + 1) * sizeof(wchar_t);
    std::vector<unsigned char> buffer(headerBytes + textBytes);

    const ClipboardTextCommand command{
        position, setSystemClipboard ? TRUE : FALSE};
    std::memcpy(buffer.data(), &command, sizeof(command));
    std::memcpy(buffer.data() + headerBytes, text.c_str(), textBytes);

    COPYDATASTRUCT cds{};
    cds.dwData = CD_CLIPBOARD_TEXT;
    cds.cbData = static_cast<DWORD>(buffer.size());
    cds.lpData = buffer.data();

    DWORD_PTR result = 0;
    LRESULT sent = SendMessageTimeoutW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
                                       SMTO_ABORTIFHUNG, 2000, &result);
    return sent != 0 && result == TRUE;
}

bool SendHistoryMutation(HistoryMutationOperation operation, uint64_t itemId,
                         const std::string& profileId) {
    HWND hwnd = FindRunningInstance();
    if (!hwnd || profileId.empty())
        return false;

    const std::wstring wideProfile = win32util::Utf8ToWide(profileId);
    if (wideProfile.empty())
        return false;
    const size_t headerBytes = sizeof(HistoryMutationCommand);
    const size_t profileBytes = (wideProfile.size() + 1) * sizeof(wchar_t);
    if (headerBytes + profileBytes > MAXDWORD)
        return false;
    std::vector<unsigned char> buffer(headerBytes + profileBytes);

    const HistoryMutationCommand command{
        kHistoryMutationVersion, static_cast<uint32_t>(operation), itemId};
    std::memcpy(buffer.data(), &command, sizeof(command));
    std::memcpy(buffer.data() + headerBytes, wideProfile.c_str(), profileBytes);

    COPYDATASTRUCT cds{};
    cds.dwData = CD_HISTORY_MUTATION;
    cds.cbData = static_cast<DWORD>(buffer.size());
    cds.lpData = buffer.data();

    DWORD_PTR result = 0;
    const LRESULT sent = SendMessageTimeoutW(
        hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
        SMTO_ABORTIFHUNG, 2000, &result);
    return sent != 0 && result == TRUE;
}

} // namespace ipc
