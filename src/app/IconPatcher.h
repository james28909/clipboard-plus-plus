#pragma once
#include <windows.h>
#include <string>

namespace IconPatcher {
    // Replaces the RT_GROUP_ICON (resource ID 1) of exePath with the icon at icoPath.
    // On success, notifies Explorer to refresh its icon cache.
    // On failure, *outError receives GetLastError() if outError != nullptr.
    bool PatchExeIcon(const std::wstring& exePath, const std::wstring& icoPath,
                      DWORD* outError = nullptr);

    // Reads the RT_GROUP_ICON from exePath and writes it as a .ico file to outIcoPath.
    // Used to snapshot the factory icon before any user customization.
    bool ExtractExeIcon(const std::wstring& exePath, const std::wstring& outIcoPath);
}
