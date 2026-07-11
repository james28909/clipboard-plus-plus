#include "AndroidDeviceClient.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <json.hpp>

#include <atomic>
#include <sstream>

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

struct ParsedUrl {
    std::string host;
    std::string port{"80"};
    std::string path{"/items"};
};

bool ParseHttpUrl(std::string url, ParsedUrl& out, const std::string& defaultPath) {
    constexpr const char* prefix = "http://";
    if (url.rfind(prefix, 0) == 0)
        url.erase(0, 7);

    const size_t slash = url.find('/');
    std::string authority = slash == std::string::npos ? url : url.substr(0, slash);
    out.path = slash == std::string::npos ? defaultPath : url.substr(slash);
    if (out.path.empty() || out.path == "/")
        out.path = defaultPath;

    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        out.host = authority.substr(0, colon);
        out.port = authority.substr(colon + 1);
    } else {
        out.host = authority;
    }

    return !out.host.empty() && !out.port.empty();
}

bool PostJson(const std::string& endpoint,
              const std::string& defaultPath,
              const nlohmann::json& payload,
              std::string* error) {
    ParsedUrl parsed;
    if (!ParseHttpUrl(endpoint, parsed, defaultPath)) {
        if (error) *error = "Invalid Android endpoint.";
        return false;
    }

    if (!EnsureWinsock()) {
        if (error) *error = "Winsock startup failed.";
        return false;
    }

    bool ok = false;
    SOCKET sock = INVALID_SOCKET;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    if (getaddrinfo(parsed.host.c_str(), parsed.port.c_str(), &hints, &result) != 0) {
        if (error) *error = "Could not resolve Android endpoint.";
        ReleaseWinsock();
        return false;
    }

    for (addrinfo* ptr = result; ptr; ptr = ptr->ai_next) {
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == INVALID_SOCKET)
            continue;
        if (connect(sock, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0)
            break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(result);

    if (sock == INVALID_SOCKET) {
        if (error) *error = "Could not connect to Android endpoint.";
        ReleaseWinsock();
        return false;
    }

    const std::string body = payload.dump();
    std::ostringstream request;
    request << "POST " << parsed.path << " HTTP/1.1\r\n"
            << "Host: " << parsed.host << ":" << parsed.port << "\r\n"
            << "Content-Type: application/json; charset=utf-8\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << body;

    const std::string bytes = request.str();
    if (send(sock, bytes.data(), static_cast<int>(bytes.size()), 0) == static_cast<int>(bytes.size())) {
        char response[256]{};
        const int received = recv(sock, response, sizeof(response) - 1, 0);
        ok = received > 0 && std::string(response, static_cast<size_t>(received)).find(" 200 ") != std::string::npos;
        if (!ok && error)
            *error = "Android endpoint returned a non-200 response.";
    } else if (error) {
        *error = "Failed to send request to Android.";
    }

    shutdown(sock, SD_BOTH);
    closesocket(sock);
    ReleaseWinsock();
    return ok;
}

bool SendSimpleRequest(const std::string& endpoint,
                       const std::string& defaultPath,
                       const std::string& method,
                       const std::string& body,
                       std::string* error) {
    ParsedUrl parsed;
    if (!ParseHttpUrl(endpoint, parsed, defaultPath)) {
        if (error) *error = "Invalid Android endpoint.";
        return false;
    }

    if (!EnsureWinsock()) {
        if (error) *error = "Winsock startup failed.";
        return false;
    }

    SOCKET sock = INVALID_SOCKET;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    if (getaddrinfo(parsed.host.c_str(), parsed.port.c_str(), &hints, &result) != 0) {
        if (error) *error = "Could not resolve Android endpoint.";
        ReleaseWinsock();
        return false;
    }

    for (addrinfo* ptr = result; ptr; ptr = ptr->ai_next) {
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == INVALID_SOCKET)
            continue;
        if (connect(sock, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == 0)
            break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(result);

    if (sock == INVALID_SOCKET) {
        if (error) *error = "Could not connect to Android endpoint.";
        ReleaseWinsock();
        return false;
    }

    std::ostringstream request;
    request << method << " " << parsed.path << " HTTP/1.1\r\n"
            << "Host: " << parsed.host << ":" << parsed.port << "\r\n"
            << "Connection: close\r\n";
    if (!body.empty()) {
        request << "Content-Type: application/json; charset=utf-8\r\n"
                << "Content-Length: " << body.size() << "\r\n";
    }
    request << "\r\n" << body;

    bool ok = false;
    const std::string bytes = request.str();
    if (send(sock, bytes.data(), static_cast<int>(bytes.size()), 0) == static_cast<int>(bytes.size())) {
        char response[256]{};
        const int received = recv(sock, response, sizeof(response) - 1, 0);
        ok = received > 0 && std::string(response, static_cast<size_t>(received)).find(" 200 ") != std::string::npos;
        if (!ok && error)
            *error = "Android endpoint returned a non-200 response.";
    } else if (error) {
        *error = "Failed to send request to Android.";
    }

    shutdown(sock, SD_BOTH);
    closesocket(sock);
    ReleaseWinsock();
    return ok;
}

} // namespace

namespace androidsync {

bool SendItemsToAndroid(const std::string& endpoint,
                        const std::vector<std::string>& texts,
                        std::string* error) {
    if (endpoint.empty()) {
        if (error) *error = "Android endpoint is empty.";
        return false;
    }

    nlohmann::json items = nlohmann::json::array();
    for (const std::string& text : texts) {
        if (!text.empty())
            items.push_back({{"text", text}});
    }
    if (items.empty()) {
        if (error) *error = "No text items to send.";
        return false;
    }

    const nlohmann::json payload = {
        {"makeActive", true},
        {"items", items}
    };
    return PostJson(endpoint, "/items", payload, error);
}

bool RequestAndroidSyncToWindows(const std::string& endpoint, std::string* error) {
    if (endpoint.empty()) {
        if (error) *error = "Android endpoint is empty.";
        return false;
    }
    return PostJson(endpoint, "/sync/windows", nlohmann::json::object(), error);
}

bool CheckAndroidHealth(const std::string& endpoint, std::string* error) {
    if (endpoint.empty()) {
        if (error) *error = "Android endpoint is empty.";
        return false;
    }
    return SendSimpleRequest(endpoint, "/health", "GET", "", error);
}

} // namespace androidsync
