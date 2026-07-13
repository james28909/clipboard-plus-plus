#include "AndroidIntegration.h"

#include "AndroidDeviceClient.h"
#include "AndroidSyncServer.h"
#include "../app/Application.h"
#include "../clipboard/ClipboardMonitor.h"
#include "../hotkeys/HotkeyManager.h"
#include "../util/Win32Util.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <thread>

namespace {

bool HotkeyKeysReleased() {
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0 &&
           (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0 &&
           (GetAsyncKeyState(VK_MENU) & 0x8000) == 0 &&
           (GetAsyncKeyState('Z') & 0x8000) == 0;
}

void SendCtrlC() {
    INPUT input[4]{};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_CONTROL;
    input[0].ki.dwExtraInfo = kClipboardPasteMagic;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = 'C';
    input[1].ki.dwExtraInfo = kClipboardPasteMagic;
    input[2].type = INPUT_KEYBOARD;
    input[2].ki.wVk = 'C';
    input[2].ki.dwFlags = KEYEVENTF_KEYUP;
    input[2].ki.dwExtraInfo = kClipboardPasteMagic;
    input[3].type = INPUT_KEYBOARD;
    input[3].ki.wVk = VK_CONTROL;
    input[3].ki.dwFlags = KEYEVENTF_KEYUP;
    input[3].ki.dwExtraInfo = kClipboardPasteMagic;
    SendInput(4, input, sizeof(INPUT));
}

} // namespace

AndroidIntegration::AndroidIntegration(Application& app)
    : m_app(app), m_syncServer(std::make_unique<AndroidSyncServer>(app))
{}

AndroidIntegration::~AndroidIntegration() {
    StopServer();
}

bool AndroidIntegration::StartServer() {
    return m_syncServer && m_syncServer->Start();
}

void AndroidIntegration::StopServer() {
    if (m_syncServer)
        m_syncServer->Stop();
}

bool AndroidIntegration::IsServerRunning() const {
    return m_syncServer && m_syncServer->IsRunning();
}

unsigned short AndroidIntegration::ServerPort() const {
    return m_syncServer ? m_syncServer->Port() : 8766;
}

bool AndroidIntegration::AddClipboardText(const std::string& text,
                                          const std::string& source) {
    if (text.empty())
        return false;

    std::lock_guard<std::mutex> lock(m_clipboardMutex);
    auto existing = std::find_if(m_clipboardEntries.begin(), m_clipboardEntries.end(),
        [&](const AndroidClipboardEntry& entry) { return entry.text == text; });

    if (existing != m_clipboardEntries.end()) {
        AndroidClipboardEntry entry = std::move(*existing);
        entry.source = source.empty() ? entry.source : source;
        entry.capturedAt = std::chrono::system_clock::now();
        m_clipboardEntries.erase(existing);
        m_clipboardEntries.insert(m_clipboardEntries.begin(), std::move(entry));
        return false;
    }

    AndroidClipboardEntry entry;
    entry.id = m_nextClipboardEntryId++;
    entry.text = text;
    entry.source = source.empty() ? "android" : source;
    entry.capturedAt = std::chrono::system_clock::now();
    m_clipboardEntries.insert(m_clipboardEntries.begin(), std::move(entry));
    return true;
}

std::vector<AndroidClipboardEntry> AndroidIntegration::ClipboardEntries() const {
    std::lock_guard<std::mutex> lock(m_clipboardMutex);
    return m_clipboardEntries;
}

bool AndroidIntegration::RemoveClipboardEntry(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_clipboardMutex);
    auto it = std::find_if(m_clipboardEntries.begin(), m_clipboardEntries.end(),
        [&](const AndroidClipboardEntry& entry) { return entry.id == id; });
    if (it == m_clipboardEntries.end())
        return false;
    m_clipboardEntries.erase(it);
    return true;
}

bool AndroidIntegration::SetClipboardEntryPinned(uint64_t id, bool pinned) {
    std::lock_guard<std::mutex> lock(m_clipboardMutex);
    auto it = std::find_if(m_clipboardEntries.begin(), m_clipboardEntries.end(),
        [&](const AndroidClipboardEntry& entry) { return entry.id == id; });
    if (it == m_clipboardEntries.end())
        return false;
    it->pinned = pinned;
    std::stable_sort(m_clipboardEntries.begin(), m_clipboardEntries.end(),
        [](const AndroidClipboardEntry& a, const AndroidClipboardEntry& b) {
            if (a.pinned != b.pinned)
                return a.pinned && !b.pinned;
            return a.capturedAt > b.capturedAt;
        });
    return true;
}

void AndroidIntegration::SetDeviceEndpoint(std::string endpoint) {
    while (!endpoint.empty() && std::isspace(static_cast<unsigned char>(endpoint.front())))
        endpoint.erase(endpoint.begin());
    while (!endpoint.empty() && std::isspace(static_cast<unsigned char>(endpoint.back())))
        endpoint.pop_back();
    if (!endpoint.empty() && endpoint.rfind("http://", 0) != 0 &&
        endpoint.rfind("https://", 0) != 0) {
        endpoint = "http://" + endpoint;
    }
    m_deviceEndpoint = std::move(endpoint);
}

bool AndroidIntegration::SendTextItems(const std::vector<std::string>& texts,
                                       std::string* error) const {
    return androidsync::SendItemsToAndroid(m_deviceEndpoint, texts, error);
}

bool AndroidIntegration::RequestSyncToWindows(std::string* error) const {
    return androidsync::RequestAndroidSyncToWindows(m_deviceEndpoint, error);
}

bool AndroidIntegration::CheckDeviceHealth(std::string* error) const {
    return androidsync::CheckAndroidHealth(m_deviceEndpoint, error);
}

void AndroidIntegration::SendSelectionToDevice() {
    if (m_deviceEndpoint.empty()) {
        m_app.AddDeveloperEvent("send selection to Android failed: endpoint not set");
        return;
    }

    std::thread([this]() {
        for (int i = 0; i < 60 && !HotkeyKeysReleased(); ++i)
            Sleep(25);

        const bool hadText = IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
        const std::string previousText = hadText ? win32util::ClipboardUnicodeText() : std::string{};
        const DWORD beforeSeq = GetClipboardSequenceNumber();

        if (ClipboardMonitor* monitor = m_app.GetMonitor())
            monitor->SuppressNextUpdate();
        SendCtrlC();

        bool changed = false;
        for (int i = 0; i < 40; ++i) {
            Sleep(25);
            if (GetClipboardSequenceNumber() != beforeSeq) {
                changed = true;
                break;
            }
        }

        const std::string selectedText = win32util::ClipboardUnicodeText();
        if (hadText) {
            const std::wstring wide = win32util::Utf8ToWide(previousText);
            if (ClipboardMonitor* monitor = m_app.GetMonitor())
                monitor->SuppressNextUpdate();
            win32util::SetClipboardUnicodeText(nullptr, wide.c_str(), wide.size());
        }

        if (!changed || selectedText.empty()) {
            m_app.AddDeveloperEvent("send selection to Android skipped: no selected text copied");
            return;
        }

        std::string error;
        if (SendTextItems({selectedText}, &error))
            m_app.AddDeveloperEvent("sent selected text to Android clipboard");
        else
            m_app.AddDeveloperEvent("send selection to Android failed: " +
                (error.empty() ? std::string("unknown error") : error));
    }).detach();
}
