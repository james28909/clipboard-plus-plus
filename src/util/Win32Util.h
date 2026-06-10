#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace win32util {

std::string WideToUtf8(const wchar_t* value, int len = -1);
std::wstring Utf8ToWide(const std::string& value);
std::wstring TrimPathToken(std::wstring value);
std::vector<std::wstring> ExistingPathList(const wchar_t* text);
std::vector<std::wstring> ExistingPathListUtf8(const std::string& text);

bool SetClipboardUnicodeText(HWND owner, const wchar_t* text, size_t chars);
bool SetClipboardFileDrop(HWND owner, const std::vector<std::wstring>& paths);
std::string ClipboardUnicodeText();

std::string ProcessNameFromWindow(HWND hwnd);
std::string ModulePath();
std::string CurrentDirectory();

float DpiScaleForWindow(HWND hwnd);
bool EqW(const wchar_t* a, const wchar_t* b);
std::string ToLower(std::string s);

} // namespace win32util
