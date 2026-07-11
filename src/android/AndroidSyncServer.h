#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

class Application;

class AndroidSyncServer {
public:
    explicit AndroidSyncServer(Application& app);
    ~AndroidSyncServer();

    bool Start(uint16_t port = 8766);
    void Stop();
    bool IsRunning() const { return m_running.load(); }
    uint16_t Port() const { return m_port; }

private:
    void Run();
    void HandleClient(uintptr_t socketValue);
    std::string HandleRequest(const std::string& method,
                              const std::string& path,
                              const std::string& body,
                              int& statusCode);

    Application& m_app;
    std::atomic<bool> m_running{false};
    uintptr_t m_listenSocket{};
    uint16_t m_port{8766};
    std::thread m_thread;
};
