#pragma once
/**
 * @file utils.h
 * @brief Common utilities for test GUI
 */

#ifdef _WIN32
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket close
#define DWORD unsigned int
#endif

#include <string>
#include <map>
#include <sstream>
#include <vector>

namespace test_gui {

// Path separator
#ifdef _WIN32
constexpr char PATH_SEP = '\\';
#else
constexpr char PATH_SEP = '/';
#endif

// URL decode a string
inline std::string url_decode(const std::string& val) {
    std::string decoded;
    for (size_t i = 0; i < val.size(); i++) {
        if (val[i] == '%' && i + 2 < val.size()) {
            int hex;
            sscanf(val.substr(i + 1, 2).c_str(), "%x", &hex);
            decoded += (char)hex;
            i += 2;
        } else if (val[i] == '+') {
            decoded += ' ';
        } else {
            decoded += val[i];
        }
    }
    return decoded;
}

// Parse query string from URL path
inline std::map<std::string, std::string> parse_query_string(const std::string& path) {
    std::map<std::string, std::string> params;
    size_t qpos = path.find('?');
    if (qpos == std::string::npos) return params;
    
    std::string query = path.substr(qpos + 1);
    std::istringstream iss(query);
    std::string pair;
    while (std::getline(iss, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            params[pair.substr(0, eq)] = url_decode(pair.substr(eq + 1));
        }
    }
    return params;
}

// Split string by delimiter
inline std::vector<std::string> split_string(const std::string& str, char delim) {
    std::vector<std::string> result;
    std::istringstream iss(str);
    std::string token;
    while (std::getline(iss, token, delim)) {
        if (!token.empty()) result.push_back(token);
    }
    return result;
}

// JSON escape a string
inline std::string json_escape(const std::string& str) {
    std::string escaped;
    for (char c : str) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else if (c == '\r') escaped += "\\r";
        else if (c == '\t') escaped += "\\t";
        else if (c >= 32) escaped += c;
    }
    return escaped;
}

// Send HTTP response
inline void send_response(SOCKET client, const std::string& content_type, 
                          const std::string& body, int status = 200) {
    std::string status_text = (status == 200) ? "OK" : 
                              (status == 404) ? "Not Found" : "Error";
    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << status_text << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << body;
    std::string resp = response.str();
    send(client, resp.c_str(), (int)resp.size(), 0);
}

inline void send_html(SOCKET client, const std::string& html) {
    send_response(client, "text/html; charset=utf-8", html);
}

inline void send_json(SOCKET client, const std::string& json) {
    send_response(client, "application/json", json);
}

inline void send_404(SOCKET client) {
    send_response(client, "text/html", "<h1>404 Not Found</h1>", 404);
}

// Send SSE headers
inline void send_sse_headers(SOCKET client) {
    std::string headers = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    send(client, headers.c_str(), (int)headers.size(), 0);
}

// Send SSE event
inline void send_sse(SOCKET client, const std::string& json) {
    std::string msg = "data: " + json + "\n\n";
    send(client, msg.c_str(), (int)msg.size(), 0);
}

} // namespace test_gui
