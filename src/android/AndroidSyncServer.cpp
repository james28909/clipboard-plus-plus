#include "AndroidSyncServer.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "../app/Application.h"

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace {

std::atomic<int> g_wsaUsers{0};

bool EnsureWinsock() {
    if (g_wsaUsers.fetch_add(1) == 0) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            g_wsaUsers.fetch_sub(1);
            return false;
        }
    }
    return true;
}

void ReleaseWinsock() {
    if (g_wsaUsers.fetch_sub(1) == 1)
        WSACleanup();
}

std::string Reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    default:  return "Error";
    }
}

std::string JsonResponse(int status, const nlohmann::json& body) {
    const std::string payload = body.dump();
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << Reason(status) << "\r\n"
        << "Content-Type: application/json; charset=utf-8\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Content-Length: " << payload.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << payload;
    return out.str();
}

std::string HeaderValue(const std::string& headers, const std::string& name) {
    std::string lowerHeaders = headers;
    std::string lowerName = name;
    std::transform(lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const std::string needle = "\r\n" + lowerName + ":";
    size_t pos = lowerHeaders.find(needle);
    if (pos == std::string::npos) {
        if (lowerHeaders.rfind(lowerName + ":", 0) != 0)
            return {};
        pos = 0;
    } else {
        pos += 2;
    }

    const size_t colon = lowerHeaders.find(':', pos);
    if (colon == std::string::npos)
        return {};
    const size_t valueStart = colon + 1;
    const size_t valueEnd = lowerHeaders.find("\r\n", valueStart);

    std::string value = headers.substr(valueStart, valueEnd == std::string::npos
                                                    ? std::string::npos
                                                    : valueEnd - valueStart);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

} // namespace

AndroidSyncServer::AndroidSyncServer(Application& app)
    : m_app(app)
{}

AndroidSyncServer::~AndroidSyncServer() {
    Stop();
}

bool AndroidSyncServer::Start(uint16_t port) {
    if (m_running.load())
        return true;
    if (!EnsureWinsock())
        return false;

    m_port = port;
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        ReleaseWinsock();
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(m_port);

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenSocket);
        ReleaseWinsock();
        return false;
    }

    m_listenSocket = static_cast<uintptr_t>(listenSocket);
    m_running = true;
    m_thread = std::thread(&AndroidSyncServer::Run, this);
    return true;
}

void AndroidSyncServer::Stop() {
    if (!m_running.exchange(false))
        return;

    if (m_listenSocket) {
        closesocket(static_cast<SOCKET>(m_listenSocket));
        m_listenSocket = 0;
    }

    if (m_thread.joinable())
        m_thread.join();

    ReleaseWinsock();
}

void AndroidSyncServer::Run() {
    while (m_running.load()) {
        SOCKET client = accept(static_cast<SOCKET>(m_listenSocket), nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!m_running.load())
                break;
            continue;
        }

        std::thread(&AndroidSyncServer::HandleClient, this,
                    static_cast<uintptr_t>(client)).detach();
    }
}

void AndroidSyncServer::HandleClient(uintptr_t socketValue) {
    SOCKET client = static_cast<SOCKET>(socketValue);
    std::string request;
    std::vector<char> buffer(4096);

    int received = 0;
    while ((received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0)) > 0) {
        request.append(buffer.data(), static_cast<size_t>(received));
        const size_t headerEnd = request.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            const std::string headers = request.substr(0, headerEnd + 2);
            size_t contentLength = 0;
            const std::string length = HeaderValue(headers, "Content-Length");
            if (!length.empty())
                contentLength = static_cast<size_t>(std::stoul(length));
            if (request.size() >= headerEnd + 4 + contentLength)
                break;
        }
        if (request.size() > 1024 * 1024)
            break;
    }

    int status = 400;
    nlohmann::json body = {{"ok", false}, {"error", "bad request"}};

    const size_t firstLineEnd = request.find("\r\n");
    const size_t headerEnd = request.find("\r\n\r\n");
    if (firstLineEnd != std::string::npos && headerEnd != std::string::npos) {
        std::istringstream line(request.substr(0, firstLineEnd));
        std::string method;
        std::string path;
        line >> method >> path;
        const std::string payload = request.substr(headerEnd + 4);
        try {
            body = nlohmann::json::parse(HandleRequest(method, path, payload, status));
        } catch (...) {
            status = 500;
            body = {{"ok", false}, {"error", "internal error"}};
        }
    }

    const std::string response = JsonResponse(status, body);
    send(client, response.data(), static_cast<int>(response.size()), 0);
    shutdown(client, SD_BOTH);
    closesocket(client);
}

std::string AndroidSyncServer::HandleRequest(const std::string& method,
                                             const std::string& path,
                                             const std::string& body,
                                             int& statusCode) {
    if (method == "GET" && path == "/health") {
        statusCode = 200;
        return nlohmann::json{
            {"ok", true},
            {"name", "clipboardpp-windows-android-sync"},
            {"port", m_port}
        }.dump();
    }

    if (method == "GET" && path == "/profiles") {
        nlohmann::json profiles = nlohmann::json::array();
        const auto* active = m_app.GetActiveClipboardProfile();
        for (const ClipboardProfileConfig& profile : m_app.GetClipboardProfiles()) {
            profiles.push_back({
                {"id", profile.id},
                {"name", profile.name},
                {"processName", profile.processName},
                {"active", active && active->id == profile.id}
            });
        }

        statusCode = 200;
        return nlohmann::json{
            {"ok", true},
            {"activeProfileId", active ? active->id : ""},
            {"profiles", profiles}
        }.dump();
    }

    if (method == "POST" && path == "/android/items/missing") {
        nlohmann::json req = nlohmann::json::parse(body);
        nlohmann::json missing = nlohmann::json::array();
        const auto existing = m_app.GetAndroidClipboardEntries();

        auto existsInWindows = [&](const std::string& text) {
            return std::any_of(existing.begin(), existing.end(),
                [&](const AndroidClipboardEntry& entry) {
                    return entry.text == text;
                });
        };

        if (req.contains("items") && req["items"].is_array()) {
            for (const auto& item : req["items"]) {
                const std::string text = item.is_object() ? item.value("text", "") : "";
                if (!text.empty() && !existsInWindows(text))
                    missing.push_back(item);
            }
        }

        statusCode = 200;
        return nlohmann::json{
            {"ok", true},
            {"missing", missing},
            {"missingCount", missing.size()}
        }.dump();
    }

    if (method != "POST" || path != "/android/items") {
        statusCode = 404;
        return nlohmann::json{{"ok", false}, {"error", "not found"}}.dump();
    }

    nlohmann::json req = nlohmann::json::parse(body);
    size_t accepted = 0;

    auto ingestText = [&](const std::string& text, const std::string& source) {
        if (!text.empty()) {
            m_app.AddAndroidClipboardText(text, source);
            ++accepted;
        }
    };

    const std::string source = req.value("source", "android");
    if (req.contains("items") && req["items"].is_array()) {
        for (const auto& item : req["items"]) {
            if (item.is_object())
                ingestText(item.value("text", ""), item.value("source", source));
        }
    } else {
        ingestText(req.value("text", ""), source);
    }

    statusCode = 200;
    return nlohmann::json{{"ok", true}, {"accepted", accepted}}.dump();
}
