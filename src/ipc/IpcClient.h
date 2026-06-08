#pragma once

#include <windows.h>

#include <string>

namespace ipc {

HWND FindRunningInstance();
void SignalRunning(unsigned int message);
bool SendClipboardHistoryText(const std::wstring& text, int position, bool setSystemClipboard);

} // namespace ipc
