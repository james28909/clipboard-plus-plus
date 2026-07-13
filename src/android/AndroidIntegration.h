#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Application;
class AndroidSyncServer;

struct AndroidClipboardEntry {
    uint64_t id{};
    std::string text;
    std::string source;
    std::chrono::system_clock::time_point capturedAt;
    bool pinned{false};
};

class AndroidIntegration {
public:
    explicit AndroidIntegration(Application& app);
    ~AndroidIntegration();

    bool StartServer();
    void StopServer();
    bool IsServerRunning() const;
    unsigned short ServerPort() const;

    bool AddClipboardText(const std::string& text,
                          const std::string& source = "android");
    std::vector<AndroidClipboardEntry> ClipboardEntries() const;
    bool RemoveClipboardEntry(uint64_t id);
    bool SetClipboardEntryPinned(uint64_t id, bool pinned);

    const std::string& DeviceEndpoint() const { return m_deviceEndpoint; }
    void SetDeviceEndpoint(std::string endpoint);
    bool SendTextItems(const std::vector<std::string>& texts,
                       std::string* error = nullptr) const;
    bool RequestSyncToWindows(std::string* error = nullptr) const;
    bool CheckDeviceHealth(std::string* error = nullptr) const;
    void SendSelectionToDevice();

private:
    Application& m_app;
    std::unique_ptr<AndroidSyncServer> m_syncServer;
    mutable std::mutex m_clipboardMutex;
    std::vector<AndroidClipboardEntry> m_clipboardEntries;
    uint64_t m_nextClipboardEntryId{1};
    std::string m_deviceEndpoint;
};
