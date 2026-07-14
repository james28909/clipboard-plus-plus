#pragma once

#include <windows.h>

#include "IpcProtocol.h"

#include <string>

namespace ipc {

HWND FindRunningInstance();
void SignalRunning(unsigned int message);
bool SendClipboardHistoryText(const std::wstring& text, int position, bool setSystemClipboard);
bool SendHistoryMutation(HistoryMutationOperation operation, uint64_t itemId,
                         const std::string& profileId);

} // namespace ipc
