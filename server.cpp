#define NOMINMAX

#include "server.h"
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <cstdio>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "pages_login.h"
#include "pages_dashboard.h"
#include "pages_holiday.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

namespace fs = std::filesystem;
static fs::path g_usersRoot;
static fs::path g_credentialsPath;
static std::unordered_map<std::string, std::string> g_users;
static std::unordered_map<std::string, std::string> g_sessions;
static std::atomic_bool g_running(false);
static SOCKET g_serverSocket = INVALID_SOCKET;

static BOOL WINAPI consoleHandler(DWORD ctrlType) {
    // handle console close/ctrl events to gracefully stop server and free port
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_running.store(false);
            if (g_serverSocket != INVALID_SOCKET) {
                // Try to shutdown and close the listening socket to unblock accept()
                shutdown(g_serverSocket, SD_BOTH);
                closesocket(g_serverSocket);
                g_serverSocket = INVALID_SOCKET;
            }
            return TRUE;
        default:
            return FALSE;
    }
}

// Trim whitespace from both ends of a string.
// Returns an empty string if the input contains only whitespace.
static std::string trim(const std::string& value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    size_t end = value.find_last_not_of(" \t\r\n");
    if (start == std::string::npos || end == std::string::npos) {
        return "";
    }
    return value.substr(start, end - start + 1);
}

// Parse a single HTTP header value from the raw request text.
// headerName is case-sensitive here and should match the expected capitalization (e.g. "Cookie").
static std::string parseHeader(const std::string& request, const std::string& headerName) {
    std::istringstream stream(request);
    std::string line;
    std::string prefix = headerName + ":";
    std::getline(stream, line);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        if (line.rfind(prefix, 0) == 0) {
            return trim(line.substr(prefix.size()));
        }
    }
    return "";
}

// Extract a specific cookie value from the HTTP "Cookie" header.
static std::string getCookieValue(const std::string& request, const std::string& cookieName) {
    std::string cookieHeader = parseHeader(request, "Cookie");
    if (cookieHeader.empty()) {
        return "";
    }

    size_t position = 0;
    while (position < cookieHeader.size()) {
        size_t equalsPos = cookieHeader.find('=', position);
        if (equalsPos == std::string::npos) {
            break;
        }
        std::string name = trim(cookieHeader.substr(position, equalsPos - position));
        size_t semicolonPos = cookieHeader.find(';', equalsPos + 1);
        std::string value = trim(cookieHeader.substr(equalsPos + 1,
            semicolonPos == std::string::npos ? std::string::npos : semicolonPos - equalsPos - 1));
        if (name == cookieName) {
            return value;
        }
        if (semicolonPos == std::string::npos) {
            break;
        }
        position = semicolonPos + 1;
    }
    return "";
}

// Read Content-Length header (if present) and return body length.
static int getContentLength(const std::string& request) {
    std::string headerValue = trim(parseHeader(request, "Content-Length"));
    if (headerValue.empty()) {
        return 0;
    }
    try {
        return std::max(0, std::stoi(headerValue));
    } catch (...) {
        return 0;
    }
}

// Generate a random 32-hex-character session token.
static std::string generateUserId() {
    std::random_device rd;
    std::mt19937_64 generator(rd());
    std::uniform_int_distribution<unsigned long long> distribution(0, std::numeric_limits<unsigned long long>::max());
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << distribution(generator)
        << std::setw(16) << distribution(generator);
    return out.str();
}

// Check session token format (32 hex chars).
static bool isValidUserId(const std::string& id) {
    if (id.size() != 32) {
        return false;
    }
    for (char c : id) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

// Escape header values for safe inclusion in HTTP headers (simple escaping).
static std::string escapeHeaderValue(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() * 2);
    for (char c : value) {
        if (c == '"' || c == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

// Escape a string for inclusion as a JSON string value.
static std::string escapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size() * 2);
    for (unsigned char c : input) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    sprintf_s(buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

// Compare two strings in constant time to mitigate timing attacks.
static bool constantTimeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}

// Return the HTTP request body (everything after the CRLFCRLF separator).
static std::string parseRequestBody(const std::string& request) {
    size_t boundary = request.find("\r\n\r\n");
    if (boundary == std::string::npos) {
        return "";
    }
    return request.substr(boundary + 4);
}

// Parse application/x-www-form-urlencoded body into a key/value map.
static std::unordered_map<std::string, std::string> parseFormData(const std::string& body) {
    std::unordered_map<std::string, std::string> data;
    size_t position = 0;

    auto decode = [](const std::string& value) {
        std::string decoded;
        decoded.reserve(value.size());
        auto fromHex = [](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };

        for (size_t i = 0; i < value.size(); ++i) {
            char c = value[i];
            if (c == '%' && i + 2 < value.size()) {
                char high = value[i + 1];
                char low = value[i + 2];
                decoded.push_back(static_cast<char>((fromHex(high) << 4) | fromHex(low)));
                i += 2;
            } else if (c == '+') {
                decoded.push_back(' ');
            } else {
                decoded.push_back(c);
            }
        }
        return decoded;
    };

    while (position < body.size()) {
        size_t amp = body.find('&', position);
        std::string pair = body.substr(position, amp == std::string::npos ? std::string::npos : amp - position);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key = decode(pair.substr(0, eq));
            std::string value = decode(pair.substr(eq + 1));
            data[key] = value;
        }
        if (amp == std::string::npos) {
            break;
        }
        position = amp + 1;
    }

    return data;
}

// Simple username validation: allow alnum plus '_', '-', '.' and length <= 32.
static bool isValidUsername(const std::string& username) {
    if (username.empty() || username.size() > 32) {
        return false;
    }
    for (char c : username) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

// Load username:password pairs from the credentials file into g_users.
static void loadUserCredentials() {
    g_users.clear();
    std::ifstream file(g_credentialsPath);
    if (!file) {
        std::ofstream example(g_credentialsPath);
        example << "# Add username:password entries below\n";
        example << "# Example:\n";
        example << "# admin:admin123\n";
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string username = trim(line.substr(0, colon));
        std::string password = trim(line.substr(colon + 1));
        if (isValidUsername(username) && !password.empty()) {
            g_users[username] = password;
        }
    }
}

// Return the authenticated username for this request (via session cookie), or empty string.
static std::string getAuthenticatedUser(const std::string& request) {
    std::string token = getCookieValue(request, "session_token");
    if (token.empty()) {
        return "";
    }
    auto it = g_sessions.find(token);
    if (it == g_sessions.end()) {
        return "";
    }
    return it->second;
}

// Decode percent-encoded URL components.
static std::string decodeUrl(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    auto fromHex = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };

    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == '%' && i + 2 < value.size()) {
            char high = value[i + 1];
            char low = value[i + 2];
            decoded.push_back(static_cast<char>((fromHex(high) << 4) | fromHex(low)));
            i += 2;
        } else if (c == '+') {
            decoded.push_back(' ');
        } else {
            decoded.push_back(c);
        }
    }
    return decoded;
}

static std::string getContentType(const fs::path& filePath) {
    std::string extension = filePath.extension().string();
    for (auto& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (extension == ".html" || extension == ".htm") {
        return "text/html";
    }
    if (extension == ".css") {
        return "text/css";
    }
    if (extension == ".js") {
        return "application/javascript";
    }
    if (extension == ".json") {
        return "application/json";
    }
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }
    if (extension == ".gif") {
        return "image/gif";
    }
    if (extension == ".svg") {
        return "image/svg+xml";
    }
    if (extension == ".txt") {
        return "text/plain";
    }
    return "application/octet-stream";
}

static fs::path safeCanonical(const fs::path& p) {
    try {
        return fs::weakly_canonical(p);
    } catch (...) {
        return fs::path();
    }
}

static bool pathInsideUserFolder(const fs::path& path, const fs::path& userFolder) {
    fs::path normalizedRoot = safeCanonical(userFolder);
    fs::path normalizedPath = safeCanonical(path);
    if (normalizedRoot.empty() || normalizedPath.empty()) {
        return false;
    }

    auto rootIt = normalizedRoot.begin();
    auto pathIt = normalizedPath.begin();
    for (; rootIt != normalizedRoot.end(); ++rootIt, ++pathIt) {
        if (pathIt == normalizedPath.end() || *rootIt != *pathIt) {
            return false;
        }
    }
    return true;
}

// Build and send a full HTTP response over the given SSL connection.
// `status` should include reason (e.g. "200 OK").
static void sendResponse(SSL* ssl, const std::string& status, const std::string& contentType,
    const std::string& body, const std::vector<std::string>& extraHeaders = {}, const std::string& setCookie = "") {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "X-Content-Type-Options: nosniff\r\n";
    if (!setCookie.empty()) {
        response << "Set-Cookie: " << setCookie << "\r\n";
    }
    for (const auto& header : extraHeaders) {
        response << header << "\r\n";
    }
    response << "\r\n" << body;

    std::string responseStr = response.str();
    SSL_write(ssl, responseStr.c_str(), static_cast<int>(responseStr.length()));
}

// Read a file from disk and send it as an HTTP response body.
// Returns true on success, false if the file could not be opened.
static bool sendFile(SSL* ssl, const fs::path& filePath, const std::string& contentType,
    const std::vector<std::string>& extraHeaders, const std::string& setCookie = "") {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream body;
    body << file.rdbuf();
    sendResponse(ssl, "200 OK", contentType, body.str(), extraHeaders, setCookie);
    return true;
}

// Per-connection request handler. Reads the request, routes it, and responds.
// This runs on a detached thread for each accepted connection.
static void handleClient(SSL* ssl) {
    char buffer[8192];
    std::string request;
    size_t headerEnd = std::string::npos;
    int expectedBodyLength = 0;

    while (true) {
        int bytesReceived = SSL_read(ssl, buffer, sizeof(buffer));
        if (bytesReceived <= 0) {
            break;
        }
        request.append(buffer, bytesReceived);
        headerEnd = request.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            expectedBodyLength = getContentLength(request);
            size_t totalNeeded = headerEnd + 4 + static_cast<size_t>(expectedBodyLength);
            if (request.size() >= totalNeeded) {
                break;
            }
        }
        if (request.size() > 65536) {
            break;
        }
    }

    if (request.empty()) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return;
    }

    headerEnd = request.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        sendResponse(ssl, "400 Bad Request", "text/html",
            "<html><body><h1>400 Bad Request</h1></body></html>");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    std::istringstream requestStream(request);
    std::string method;
    std::string path;
    requestStream >> method >> path;

    if (path.find('?') != std::string::npos) {
        path = path.substr(0, path.find('?'));
    }

    if (method != "GET" && method != "HEAD" && method != "POST") {
        sendResponse(ssl, "405 Method Not Allowed", "text/html",
            "<html><body><h1>405 Method Not Allowed</h1></body></html>", {"Allow: GET, HEAD, POST"});
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    std::string authUser = getAuthenticatedUser(request);
    std::string setCookie;
    std::string userFolderName;
    if (!authUser.empty()) {
        userFolderName = authUser;
    }

    auto redirectTo = [&](const std::string& location, const std::string& cookie = "") {
        sendResponse(ssl, "302 Found", "text/html", "<html><body>Redirecting...</body></html>",
            {"Location: " + location}, cookie);
    };

    auto sendStaticFile = [&](const std::string& fileName, const std::string& cookie = "") {
        fs::path filePath = fs::current_path() / fileName;
        if (!fs::exists(filePath)) {
            sendResponse(ssl, "500 Internal Server Error", "text/html",
                "<html><body><h1>500 Internal Server Error</h1></body></html>", {}, cookie);
            return;
        }
        sendFile(ssl, filePath, "text/html", {}, cookie);
    };

    if ((path == "/" || path == "/login") && method == "GET") {
        if (!authUser.empty()) {
            redirectTo("/dashboard");
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }
        // Serve login HTML from pages_login module
        sendResponse(ssl, "200 OK", "text/html", getLoginPage(), {}, setCookie);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    if (path == "/logout" && method == "GET") {
        if (!authUser.empty()) {
            std::string token = getCookieValue(request, "session_token");
            g_sessions.erase(token);
        }
        setCookie = "session_token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT; SameSite=Lax; HttpOnly";
        redirectTo("/");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    if (path == "/login" && method == "POST") {
        std::string body = parseRequestBody(request);
        auto formData = parseFormData(body);
        std::string username = trim(formData["username"]);
        std::string password = formData["password"];

        if (isValidUsername(username) && !password.empty()) {
            auto it = g_users.find(username);
            if (it != g_users.end() && constantTimeEqual(it->second, password)) {
                std::string sessionToken = generateUserId();
                g_sessions[sessionToken] = username;
                setCookie = "session_token=" + sessionToken + "; Path=/; SameSite=Lax; HttpOnly";
                redirectTo("/dashboard", setCookie);
                closesocket(SSL_get_fd(ssl));
                return;
            }
        }
        redirectTo("/?error=invalid");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    if (path == "/dashboard" && method == "GET") {
        if (authUser.empty()) {
            redirectTo("/");
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }
        // Serve dashboard HTML from pages_dashboard module
        sendResponse(ssl, "200 OK", "text/html", getDashboardPage(), {}, setCookie);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    if (path == "/holiday" && method == "GET") {
        if (authUser.empty()) {
            redirectTo("/");
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }
        // Serve holiday HTML from pages_holiday module
        sendResponse(ssl, "200 OK", "text/html", getHolidayPage(), {}, setCookie);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    if (path == "/holiday/key" && method == "GET") {
        if (authUser.empty()) {
            sendResponse(ssl, "401 Unauthorized", "application/json", "{\"error\":\"Unauthorized\"}", {}, setCookie);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }
        fs::path keyFile = g_usersRoot / authUser / ".festivo_key";
        std::string key;
        if (fs::exists(keyFile)) {
            std::ifstream kf(keyFile);
            std::getline(kf, key);
            kf.close();
        }
        std::ostringstream json;
        json << "{\"api_key\":\"" << key << "\"}";
        sendResponse(ssl, "200 OK", "application/json", json.str(), {}, setCookie);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    if (path == "/holiday/key" && method == "POST") {
        if (authUser.empty()) {
            sendResponse(ssl, "401 Unauthorized", "application/json", "{\"error\":\"Unauthorized\"}", {}, setCookie);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }
        std::string body = parseRequestBody(request);
        auto formData = parseFormData(body);
        std::string api_key = trim(formData["api_key"]);
        fs::path userFolder = g_usersRoot / authUser;
        fs::create_directories(userFolder);
        fs::path keyFile = userFolder / ".festivo_key";
        std::ofstream out(keyFile);
        out << api_key;
        out.close();
        sendResponse(ssl, "200 OK", "application/json", "{\"ok\":true}", {}, setCookie);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    if (path == "/holiday/next" && method == "GET") {
        if (authUser.empty()) {
            sendResponse(ssl, "401 Unauthorized", "application/json", "{\"error\":\"Unauthorized\"}", {}, setCookie);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }

        // parse query string manually from the request start line
        std::string requestLine;
        std::istringstream rs(request);
        std::getline(rs, requestLine);
        std::istringstream r2(requestLine);
        std::string method2, fullpath;
        r2 >> method2 >> fullpath;
        std::string query;
        size_t qpos = fullpath.find('?');
        if (qpos != std::string::npos) query = fullpath.substr(qpos+1);

        auto parseQS = [&](const std::string& qs){
            std::unordered_map<std::string,std::string> m;
            size_t pos = 0;
            while (pos < qs.size()) {
                size_t amp = qs.find('&', pos);
                std::string pair = qs.substr(pos, amp==std::string::npos? std::string::npos : amp-pos);
                size_t eq = pair.find('=');
                if (eq!=std::string::npos) {
                    std::string k = decodeUrl(pair.substr(0,eq));
                    std::string v = decodeUrl(pair.substr(eq+1));
                    m[k]=v;
                }
                if (amp==std::string::npos) break;
                pos = amp+1;
            }
            return m;
        };

        auto qs = parseQS(query);
        std::string api_key = qs.count("api_key") ? qs["api_key"] : "";
        std::string country = qs.count("country") ? qs["country"] : "US";
        int year = qs.count("year") ? std::stoi(qs["year"]) : 0;
        std::string as_of = qs.count("as_of") ? qs["as_of"] : "";

        // If user didn't provide an api_key param, try per-account saved key
        if (api_key.empty()) {
            fs::path kf = g_usersRoot / authUser / ".festivo_key";
            if (fs::exists(kf)) {
                std::ifstream kif(kf);
                std::getline(kif, api_key);
            }
        }

        if (api_key.empty()) {
            sendResponse(ssl, "400 Bad Request", "application/json", "{\"error\":\"Missing api_key\"}", {}, setCookie);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }

        // Enforce archival-only years
        std::vector<int> archivalYears = {2025, 2024, 2023, 2022, 2021};
        auto isArchival = [&](int y){ for (int v : archivalYears) if (v==y) return true; return false; };

        if (year != 0 && !isArchival(year)) {
            std::ostringstream err;
            err << "{\"error\":\"This server only provides archival holiday data for years: ";
            for (size_t i=0;i<archivalYears.size();++i){ if(i) err<<","; err<<archivalYears[i]; }
            err << "\"}";
            sendResponse(ssl, "400 Bad Request", "application/json", err.str(), {}, setCookie);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }

        // Helper to run a Festivo query for a specific year and return response body
        auto fetch_for_year = [&](int y)->std::string{
            std::ostringstream festUrl;
            festUrl << "https://api.getfestivo.com/public-holidays/v3/list?api_key=" << api_key << "&country=" << country << "&year=" << y;
            std::string cmd = "curl -sS --fail -L \"" + festUrl.str() + "\"";
            FILE* pipe = _popen(cmd.c_str(), "r");
            if (!pipe) return std::string();
            std::string resp;
            char buf[4096];
            while (fgets(buf, sizeof(buf), pipe) != nullptr) resp.append(buf);
            int rc = _pclose(pipe);
            if (rc != 0 && resp.empty()) return std::string();
            return resp;
        };

        // Determine which years to try: either the provided archival year, or the full archival list
        std::vector<int> tryYears;
        if (year != 0) tryYears.push_back(year);
        else tryYears = archivalYears;

        std::vector<std::pair<std::string,std::string>> foundItems;
        int foundYear = 0;
        std::string foundBody;
        for (int y : tryYears) {
            std::string body = fetch_for_year(y);
            if (body.empty()) continue;
            size_t hpos = body.find("\"holidays\"");
            if (hpos==std::string::npos) continue;

            // extract date/name pairs by parsing each holiday object and
            // only considering top-level keys (avoid nested "weekday"->"date"->"name")
            std::vector<std::pair<std::string,std::string>> items;
            size_t arrStart = body.find('[', hpos);
            if (arrStart != std::string::npos) {
                size_t p = arrStart + 1;
                while (p < body.size()) {
                    // find next object start
                    size_t objStart = body.find('{', p);
                    if (objStart == std::string::npos) break;
                    // make sure we didn't pass the end of the array
                    size_t arrEnd = body.find(']', p);
                    if (arrEnd != std::string::npos && objStart > arrEnd) break;

                    // find matching closing brace for this object
                    int depth = 0;
                    size_t i = objStart;
                    bool started = false;
                    for (; i < body.size(); ++i) {
                        char ch = body[i];
                        if (ch == '{') { ++depth; started = true; }
                        else if (ch == '}') { --depth; if (started && depth == 0) { ++i; break; } }
                    }
                    if (i > body.size()) break;
                    size_t objEnd = i; // one past the closing '}'
                    std::string obj = body.substr(objStart, objEnd - objStart);

                    // scan top-level keys within obj (top-level = nesting depth == 1)
                    std::string dateVal, nameVal;
                    int nest = 0;
                    for (size_t j = 0; j < obj.size(); ++j) {
                        char ch = obj[j];
                        if (ch == '{') { ++nest; continue; }
                        if (ch == '}') { --nest; continue; }
                        if (ch == '"' && nest == 1) {
                            size_t k = obj.find('"', j+1);
                            if (k == std::string::npos) break;
                            std::string key = obj.substr(j+1, k-j-1);
                            size_t colon = obj.find(':', k+1);
                            if (colon == std::string::npos) break;
                            size_t valStart = colon + 1;
                            while (valStart < obj.size() && std::isspace(static_cast<unsigned char>(obj[valStart]))) ++valStart;
                            if (valStart >= obj.size()) break;
                            if (obj[valStart] == '"') {
                                // extract string value, handling escaped quotes
                                size_t vq1 = valStart;
                                size_t scan = vq1 + 1;
                                size_t vq2 = std::string::npos;
                                while (scan < obj.size()) {
                                    size_t posq = obj.find('"', scan);
                                    if (posq == std::string::npos) break;
                                    // count backslashes before posq
                                    size_t back = posq;
                                    int backslashes = 0;
                                    while (back > vq1 && obj[back-1] == '\\') { ++backslashes; --back; }
                                    if ((backslashes % 2) == 0) { vq2 = posq; break; }
                                    scan = posq + 1;
                                }
                                if (vq2 == std::string::npos) break;
                                std::string val = obj.substr(vq1+1, vq2 - vq1 - 1);
                                if (key == "date") dateVal = val;
                                else if (key == "name") nameVal = val;
                                j = vq2;
                            } else {
                                // non-string value - skip
                                j = colon;
                            }
                        }
                    }

                    if (!dateVal.empty() && !nameVal.empty()) {
                        items.emplace_back(dateVal, nameVal);
                    }

                    p = objEnd;
                }
            }

            if (items.empty()) continue;

            // choose based on as_of if provided, else return earliest in that year
            std::string pickDate = items[0].first;
            std::string pickName = items[0].second;
            if (!as_of.empty()) {
                // normalize as_of to YYYY-MM-DD if possible (assume user passed correctly)
                for (auto &it : items) {
                    if (it.first >= as_of) { pickDate = it.first; pickName = it.second; break; }
                }
            }

            foundItems.emplace_back(pickDate, pickName);
            foundYear = y;
            foundBody = body;
            break;
        }

        if (foundItems.empty()) {
            sendResponse(ssl, "200 OK", "application/json", "{\"error\":\"No holidays found (archival only)\"}", {}, setCookie);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }

        auto best = foundItems[0];
        std::ostringstream outjson;
        outjson << "{\"name\":\"" << escapeJsonString(best.second) << "\",\"date\":\"" << escapeJsonString(best.first) << "\",\"year\":" << foundYear;
        if (!foundBody.empty()) {
            outjson << ",\"festivo_raw\":\"" << escapeJsonString(foundBody) << "\"";
        }
        outjson << "}";
        sendResponse(ssl, "200 OK", "application/json", outjson.str(), {}, setCookie);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    if (path == "/session" && method == "GET") {
        if (authUser.empty()) {
            sendResponse(ssl, "401 Unauthorized", "application/json",
                "{\"error\":\"Unauthorized\"}", {}, setCookie);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }

        fs::path userFolder = g_usersRoot / authUser;
        fs::create_directories(userFolder);

        std::vector<std::string> entries;
        for (auto& entry : fs::directory_iterator(userFolder)) {
            std::string entryName = entry.path().filename().string();
            if (entry.is_directory()) {
                entryName += "/";
            }
            entries.push_back(entryName);
        }
        std::ostringstream json;
        json << "{\"folderName\":\"" << authUser << "\",\"entries\":[";
        for (size_t i = 0; i < entries.size(); ++i) {
            json << "\"" << entries[i] << "\"";
            if (i + 1 < entries.size()) {
                json << ",";
            }
        }
        json << "]}";
        sendResponse(ssl, "200 OK", "application/json", json.str(), {}, setCookie);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    bool isDownload = false;
    std::string relativePath;
    if (path.rfind("/files/", 0) == 0) {
        relativePath = path.substr(strlen("/files/"));
    } else if (path.rfind("/download/", 0) == 0) {
        relativePath = path.substr(strlen("/download/"));
        isDownload = true;
    }

    if (!relativePath.empty()) {
        if (authUser.empty()) {
            sendResponse(ssl, "401 Unauthorized", "text/html",
                "<html><body><h1>401 Unauthorized</h1></body></html>", {}, setCookie);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }

        relativePath = decodeUrl(relativePath);
        if (relativePath.empty() || relativePath.find("..") != std::string::npos || relativePath.front() == '/') {
            sendResponse(ssl, "400 Bad Request", "text/html",
                "<html><body><h1>400 Bad Request</h1></body></html>", {}, setCookie);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }

        fs::path userFolder = g_usersRoot / authUser;
        fs::create_directories(userFolder);
        fs::path requestedPath = userFolder / fs::path(relativePath);
        if (!pathInsideUserFolder(requestedPath, userFolder) || !fs::exists(requestedPath) || fs::is_directory(requestedPath)) {
            sendResponse(ssl, "404 Not Found", "text/html",
                "<html><body><h1>404 Not Found</h1></body></html>", {}, setCookie);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }

        std::string contentType = getContentType(requestedPath);
        std::vector<std::string> extraHeaders;
        if (isDownload) {
            std::string filename = requestedPath.filename().string();
            extraHeaders.push_back("Content-Disposition: attachment; filename=\"" + escapeHeaderValue(filename) + "\"");
        }
        if (!sendFile(ssl, requestedPath, contentType, extraHeaders, setCookie)) {
            sendResponse(ssl, "500 Internal Server Error", "text/html",
                "<html><body><h1>500 Internal Server Error</h1></body></html>", {}, setCookie);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closesocket(SSL_get_fd(ssl));
            return;
        }
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closesocket(SSL_get_fd(ssl));
        return;
    }

    sendResponse(ssl, "404 Not Found", "text/html",
        "<html><body><h1>404 Not Found</h1></body></html>", {}, setCookie);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    closesocket(SSL_get_fd(ssl));
}

static std::string getClientIP(SOCKET clientSocket) {
    sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);
    if (getpeername(clientSocket, (sockaddr*)&clientAddr, &addrLen) == 0) {
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        return std::string(ipStr);
    }
    return "unknown";
}

void runServer(unsigned short port) {
    g_usersRoot = fs::current_path() / "user_data";
    fs::create_directories(g_usersRoot);

    g_credentialsPath = fs::current_path() / "users.txt";
    loadUserCredentials();

    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    const SSL_METHOD* method = TLS_server_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) {
        std::cout << "Unable to create SSL context" << std::endl;
        ERR_print_errors_fp(stderr);
        return;
    }

    SSL_CTX_set_ecdh_auto(ctx, 1);

    if (SSL_CTX_use_certificate_file(ctx, "cert.pem", SSL_FILETYPE_PEM) <= 0) {
        std::cout << "Unable to load certificate" << std::endl;
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "key.pem", SSL_FILETYPE_PEM) <= 0) {
        std::cout << "Unable to load private key" << std::endl;
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        std::cout << "WSAStartup failed" << std::endl;
        SSL_CTX_free(ctx);
        return;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cout << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        SSL_CTX_free(ctx);
        return;
    }

    // allow quick reuse of the port after close
    {
        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (::bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cout << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        SSL_CTX_free(ctx);
        return;
    }

    if (::listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cout << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        SSL_CTX_free(ctx);
        return;
    }

    std::cout << "Server listening on port " << port << " (HTTPS)" << std::endl;

    // set running flag and register console handler so closing the console frees the port
    g_running.store(true);
    g_serverSocket = serverSocket;
    SetConsoleCtrlHandler(consoleHandler, TRUE);

    while (g_running.load()) {
        SOCKET clientSocket = ::accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (!g_running.load()) break; // exiting due to shutdown
            std::cout << "Accept failed: " << err << std::endl;
            continue;
        }

    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, clientSocket);

    if (SSL_accept(ssl) <= 0) {
    int err = SSL_get_error(ssl, -1);

    unsigned long opensslErr = ERR_get_error();
    if (opensslErr != 0) {
        char buf[256];
        ERR_error_string_n(opensslErr, buf, sizeof(buf));

        std::string msg(buf);

        // Ignore harmless client-side TLS shutdown/alert noise
        if (msg.find("alert certificate unknown") == std::string::npos &&
            msg.find("tls alert") == std::string::npos) {
            std::cout << "[SSL ERROR] " << msg << std::endl;
        }
    }

    SSL_free(ssl);
    closesocket(clientSocket);
    continue;
    }

    std::thread t(handleClient, ssl);
    t.detach();
}

    // cleanup listening socket (might have been closed by console handler already)
    if (serverSocket != INVALID_SOCKET) {
        shutdown(serverSocket, SD_BOTH);
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }
    g_serverSocket = INVALID_SOCKET;
    SetConsoleCtrlHandler(consoleHandler, FALSE);
    WSACleanup();
    SSL_CTX_free(ctx);
    EVP_cleanup();
}
