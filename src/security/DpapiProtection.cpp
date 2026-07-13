#include "DpapiProtection.h"

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif

#include <limits>

namespace DpapiProtection {

bool Protect(const std::vector<uint8_t>& plaintext,
             std::vector<uint8_t>& protectedData,
             uint32_t* error) {
    protectedData.clear();
#ifdef _WIN32
    if (plaintext.size() > std::numeric_limits<DWORD>::max()) {
        if (error) *error = ERROR_FILE_TOO_LARGE;
        return false;
    }

    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(plaintext.size());
    input.pbData = plaintext.empty()
        ? nullptr
        : const_cast<BYTE*>(reinterpret_cast<const BYTE*>(plaintext.data()));

    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Clipboard++ history", nullptr, nullptr,
                          nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (error) *error = GetLastError();
        return false;
    }

    protectedData.assign(output.pbData, output.pbData + output.cbData);
    if (output.pbData)
        LocalFree(output.pbData);
    if (error) *error = ERROR_SUCCESS;
    return true;
#else
    (void)plaintext;
    if (error) *error = 50; // ERROR_NOT_SUPPORTED
    return false;
#endif
}

bool Unprotect(const std::vector<uint8_t>& protectedData,
               std::vector<uint8_t>& plaintext,
               uint32_t* error) {
    plaintext.clear();
#ifdef _WIN32
    if (protectedData.empty() ||
        protectedData.size() > std::numeric_limits<DWORD>::max()) {
        if (error) *error = ERROR_INVALID_DATA;
        return false;
    }

    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(protectedData.size());
    input.pbData = const_cast<BYTE*>(
        reinterpret_cast<const BYTE*>(protectedData.data()));

    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (error) *error = GetLastError();
        return false;
    }

    plaintext.assign(output.pbData, output.pbData + output.cbData);
    if (output.pbData) {
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
    }
    if (error) *error = ERROR_SUCCESS;
    return true;
#else
    (void)protectedData;
    if (error) *error = 50; // ERROR_NOT_SUPPORTED
    return false;
#endif
}

} // namespace DpapiProtection
