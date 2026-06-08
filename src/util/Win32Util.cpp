#include "Win32Util.h"

#include <shlobj.h>

#include <cstring>
#include <sstream>

namespace win32util {

std::string WideToUtf8(const wchar_t* value, int len) {
    if (!value || (len == -1 && !*value))
        return {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, value, len, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return {};

    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, len, out.data(), bytes, nullptr, nullptr);
    if (len == -1 && !out.empty() && out.back() == '\0')
        out.pop_back();
    return out;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty())
        return {};
    int chars = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (chars <= 0)
        return {};

    std::wstring out(static_cast<size_t>(chars - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), chars);
    return out;
}

std::wstring TrimPathToken(std::wstring value) {
    auto isSpace = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    };
    while (!value.empty() && isSpace(value.front()))
        value.erase(value.begin());
    while (!value.empty() && isSpace(value.back()))
        value.pop_back();
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
        value = value.substr(1, value.size() - 2);
    return value;
}

std::vector<std::wstring> ExistingPathList(const wchar_t* text) {
    std::vector<std::wstring> paths;
    if (!text || !*text)
        return paths;

    std::wistringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line)) {
        line = TrimPathToken(std::move(line));
        if (line.empty())
            continue;
        if (GetFileAttributesW(line.c_str()) == INVALID_FILE_ATTRIBUTES)
            return {};
        paths.push_back(std::move(line));
    }
    return paths;
}

std::vector<std::wstring> ExistingPathListUtf8(const std::string& text) {
    std::vector<std::wstring> paths;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        std::wstring path = TrimPathToken(Utf8ToWide(line));
        if (path.empty())
            continue;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return {};
        paths.push_back(std::move(path));
    }
    return paths;
}

bool SetClipboardUnicodeText(HWND owner, const wchar_t* text, size_t chars) {
    if (!OpenClipboard(owner))
        return false;
    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }

    const size_t bytes = (chars + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }

    void* data = GlobalLock(mem);
    if (!data) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }

    std::memcpy(data, text, chars * sizeof(wchar_t));
    static_cast<wchar_t*>(data)[chars] = L'\0';
    GlobalUnlock(mem);

    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

bool SetClipboardFileDrop(HWND owner, const std::vector<std::wstring>& paths) {
    if (paths.empty() || !OpenClipboard(owner))
        return false;
    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }

    size_t chars = 1;
    for (const std::wstring& path : paths)
        chars += path.size() + 1;

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                              sizeof(DROPFILES) + chars * sizeof(wchar_t));
    if (!mem) {
        CloseClipboard();
        return false;
    }

    auto* drop = static_cast<DROPFILES*>(GlobalLock(mem));
    if (!drop) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }

    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;

    wchar_t* out = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
    for (const std::wstring& path : paths) {
        std::memcpy(out, path.c_str(), path.size() * sizeof(wchar_t));
        out += path.size() + 1;
    }

    GlobalUnlock(mem);
    if (!SetClipboardData(CF_HDROP, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

std::string ClipboardUnicodeText() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(nullptr))
        return {};

    std::string text;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* value = static_cast<const wchar_t*>(GlobalLock(h));
        if (value) {
            text = WideToUtf8(value);
            GlobalUnlock(h);
        }
    }

    CloseClipboard();
    return text;
}

std::string ProcessNameFromWindow(HWND hwnd) {
    if (!hwnd)
        return {};

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return {};

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc)
        return {};

    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(hProc, 0, path, &size);
    CloseHandle(hProc);
    if (!ok)
        return {};

    std::wstring full(path, size);
    const size_t pos = full.rfind(L'\\');
    return WideToUtf8((pos == std::wstring::npos ? full : full.substr(pos + 1)).c_str());
}

std::string ModulePath() {
    wchar_t path[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return n == 0 ? std::string{} : WideToUtf8(path, static_cast<int>(n));
}

std::string CurrentDirectory() {
    DWORD needed = GetCurrentDirectoryW(0, nullptr);
    if (needed == 0)
        return {};

    std::wstring dir(needed, L'\0');
    DWORD written = GetCurrentDirectoryW(needed, dir.data());
    if (written == 0)
        return {};
    while (!dir.empty() && dir.back() == L'\0')
        dir.pop_back();
    return WideToUtf8(dir.c_str());
}

} // namespace win32util
