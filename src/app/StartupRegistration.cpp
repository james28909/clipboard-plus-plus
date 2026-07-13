#include "StartupRegistration.h"

#include <string>
#include <vector>

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"Clipboard++";

std::wstring StartupCommand() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};
    return L"\"" + std::wstring(path.data(), length) +
           L"\" --clipboardpp-run-gui";
}

bool QueryCommand(std::wstring& command) {
    HKEY key{};
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0,
                                   KEY_QUERY_VALUE, &key);
    if (status != ERROR_SUCCESS)
        return false;

    DWORD type{};
    DWORD bytes{};
    status = RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return false;
    }

    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(key, kValueName, nullptr, &type,
                              reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
        return false;

    command.assign(value.data());
    return true;
}

} // namespace

namespace StartupRegistration {

bool IsEnabled() {
    const std::wstring expected = StartupCommand();
    if (expected.empty())
        return false;

    std::wstring registered;
    return QueryCommand(registered) && _wcsicmp(registered.c_str(), expected.c_str()) == 0;
}

bool SetEnabled(bool enabled, LSTATUS* error) {
    LSTATUS status = ERROR_SUCCESS;
    HKEY key{};

    if (enabled) {
        const std::wstring command = StartupCommand();
        if (command.empty()) {
            status = ERROR_FILE_NOT_FOUND;
        } else {
            status = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                     KEY_SET_VALUE, nullptr, &key, nullptr);
            if (status == ERROR_SUCCESS) {
                const DWORD bytes = static_cast<DWORD>(
                    (command.size() + 1) * sizeof(wchar_t));
                status = RegSetValueExW(key, kValueName, 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(command.c_str()), bytes);
            }
        }
    } else {
        status = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0,
                               KEY_SET_VALUE, &key);
        if (status == ERROR_SUCCESS) {
            status = RegDeleteValueW(key, kValueName);
            if (status == ERROR_FILE_NOT_FOUND)
                status = ERROR_SUCCESS;
        } else if (status == ERROR_FILE_NOT_FOUND) {
            status = ERROR_SUCCESS;
        }
    }

    if (key)
        RegCloseKey(key);
    if (error)
        *error = status;
    return status == ERROR_SUCCESS;
}

} // namespace StartupRegistration
