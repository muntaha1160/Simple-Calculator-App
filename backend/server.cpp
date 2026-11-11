// =====================================================================
//  Calculator App - C++ Backend
//
//  A tiny dependency-free HTTP server that:
//    1. Serves the static frontend (frontend/index.html, style.css, script.js)
//    2. Evaluates calculator expressions sent from the browser:
//         POST /api/calculate   body: {"expression":"2+3*4"}
//         -> {"result":14}   or   {"error":"..."}
//
//  Build:  g++ -std=c++14 -O2 -Wall backend/server.cpp -o build/calculator-server.exe -lws2_32
//  Run:    build\calculator-server.exe      (auto-opens http://localhost:8080)
//
//  Supported math:  +  -  *  /  %  ^  parentheses, decimals, unary minus
// =====================================================================

#include <winsock2.h>

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <process.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif

namespace {

// ---------------------------------------------------------------------
//  Math engine - recursive descent expression parser
// ---------------------------------------------------------------------

class ExprParser {
public:
    explicit ExprParser(const std::string& text) : src_(text), pos_(0) {}

    double parse() {
        double value = parseSum();
        skipSpaces();
        if (pos_ != src_.size()) {
            throw std::runtime_error(std::string("Unexpected character '") + src_[pos_] + "'");
        }
        return value;
    }

private:
    const std::string& src_;
    size_t pos_;

    void skipSpaces() {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_]))) ++pos_;
    }

    bool eat(char c) {
        skipSpaces();
        if (pos_ < src_.size() && src_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    // sum := product (('+' | '-') product)*
    double parseSum() {
        double value = parseProduct();
        for (;;) {
            if (eat('+'))      value += parseProduct();
            else if (eat('-')) value -= parseProduct();
            else               return value;
        }
    }

    // product := unary (('*' | '/' | '%') unary)*
    double parseProduct() {
        double value = parseUnary();
        for (;;) {
            if (eat('*')) {
                value *= parseUnary();
            } else if (eat('/')) {
                double rhs = parseUnary();
                if (rhs == 0.0) throw std::runtime_error("Division by zero");
                value /= rhs;
            } else if (eat('%')) {
                double rhs = parseUnary();
                if (rhs == 0.0) throw std::runtime_error("Modulo by zero");
                value = std::fmod(value, rhs);
            } else {
                return value;
            }
        }
    }

    // unary := ('+' | '-') unary | power
    double parseUnary() {
        if (eat('-')) return -parseUnary();
        if (eat('+')) return  parseUnary();
        return parsePower();
    }

    // power := primary ('^' unary)?   (right-associative, so 2^-3 works)
    double parsePower() {
        double base = parsePrimary();
        if (eat('^')) return std::pow(base, parseUnary());
        return base;
    }

    // primary := number | '(' sum ')'
    double parsePrimary() {
        if (eat('(')) {
            double value = parseSum();
            if (!eat(')')) throw std::runtime_error("Missing closing parenthesis");
            return value;
        }
        skipSpaces();
        size_t start = pos_;
        bool seenDigit = false;
        bool seenDot = false;
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (std::isdigit(static_cast<unsigned char>(c))) { seenDigit = true; ++pos_; }
            else if (c == '.' && !seenDot)                    { seenDot = true;   ++pos_; }
            else break;
        }
        if (!seenDigit) throw std::runtime_error("Expected a number");
        std::string number = src_.substr(start, pos_ - start);

        errno = 0;
        char* end = 0;
        double value = std::strtod(number.c_str(), &end);
        if (end == number.c_str()) throw std::runtime_error("Invalid number '" + number + "'");
        return value;
    }
};

double evaluateExpression(const std::string& expression) {
    if (expression.empty())      throw std::runtime_error("Expression is empty");
    if (expression.size() > 500) throw std::runtime_error("Expression is too long");
    double value = ExprParser(expression).parse();
    if (!std::isfinite(value))   throw std::runtime_error("Result is infinite or NaN");
    return value;
}

// Print whole numbers without a decimal tail, keep 15 digits otherwise.
std::string formatNumber(double value) {
    std::ostringstream out;
    out.precision(15);
    if (std::fabs(value) < 1e15 && value == std::floor(value)) {
        out << static_cast<long long>(value);
    } else {
        out << value;
    }
    return out.str();
}

// ---------------------------------------------------------------------
//  Tiny JSON helpers (only what this app needs)
// ---------------------------------------------------------------------

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

// Extract a string field from a small JSON object, e.g. {"expression":"2+3"}
std::string extractJsonStringField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos) return "";
    size_t colon = json.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return "";
    size_t openQuote = json.find('"', colon + 1);
    if (openQuote == std::string::npos) return "";

    std::string out;
    for (size_t i = openQuote + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            char next = json[++i];
            if      (next == 'n') out += '\n';
            else if (next == 't') out += '\t';
            else if (next == 'r') out += '\r';
            else                  out += next;   // covers \" \\ \/ etc.
        } else if (c == '"') {
            break;
        } else {
            out += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------------
//  Files and HTTP plumbing
// ---------------------------------------------------------------------

bool fileExists(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    return f.good();
}

std::string readFile(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string mimeType(const std::string& path) {
    size_t dot = path.rfind('.');
    std::string ext = (dot == std::string::npos) ? "" : path.substr(dot);
    for (size_t i = 0; i < ext.size(); ++i) ext[i] = (char)std::tolower((unsigned char)ext[i]);
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css")                    return "text/css; charset=utf-8";
    if (ext == ".js")                     return "application/javascript; charset=utf-8";
    if (ext == ".json")                   return "application/json; charset=utf-8";
    if (ext == ".svg")                    return "image/svg+xml";
    if (ext == ".png")                    return "image/png";
    if (ext == ".ico")                    return "image/x-icon";
    return "application/octet-stream";
}

std::string httpResponse(int status, const std::string& type, const std::string& body) {
    const char* reason = "Error";
    if      (status == 200) reason = "OK";
    else if (status == 400) reason = "Bad Request";
    else if (status == 404) reason = "Not Found";
    else if (status == 405) reason = "Method Not Allowed";
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << reason << "\r\n"
        << "Content-Type: " << type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;
    return out.str();
}

void sendAll(SOCKET sock, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = send(sock, data.data() + sent, (int)(data.size() - sent), 0);
        if (n <= 0) return;
        sent += (size_t)n;
    }
}

size_t findContentLength(const std::string& headers) {
    std::string lower = headers;
    for (size_t i = 0; i < lower.size(); ++i) lower[i] = (char)std::tolower((unsigned char)lower[i]);
    size_t p = lower.find("content-length:");
    if (p == std::string::npos) return 0;
    size_t start = p + 15;
    size_t end = lower.find_first_of("\r\n", start);
    std::string value = (end == std::string::npos) ? lower.substr(start) : lower.substr(start, end - start);
    return (size_t)std::strtoul(value.c_str(), 0, 10);
}

// ---------------------------------------------------------------------
//  Request handling
// ---------------------------------------------------------------------

std::string g_frontendDir;

void handleClient(SOCKET client) {
    char buffer[8192];
    std::string raw;
    size_t headerEnd = std::string::npos;

    while (headerEnd == std::string::npos && raw.size() < 65536) {
        int n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) { closesocket(client); return; }
        raw.append(buffer, (size_t)n);
        headerEnd = raw.find("\r\n\r\n");
    }
    if (headerEnd == std::string::npos) {
        sendAll(client, httpResponse(400, "text/plain", "Bad request"));
        closesocket(client);
        return;
    }

    std::string headers = raw.substr(0, headerEnd);
    std::string body = raw.substr(headerEnd + 4);

    std::istringstream headerStream(headers);
    std::string method, path, version;
    headerStream >> method >> path >> version;

    size_t contentLength = findContentLength(headers);
    while (body.size() < contentLength) {
        int n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        body.append(buffer, (size_t)n);
    }

    size_t questionMark = path.find('?');
    if (questionMark != std::string::npos) path = path.substr(0, questionMark);

    std::cout << "[request] " << method << " " << path << std::endl;

    if (method == "POST" && path == "/api/calculate") {
        std::string expression = extractJsonStringField(body, "expression");
        try {
            double result = evaluateExpression(expression);
            sendAll(client, httpResponse(200, "application/json; charset=utf-8",
                                         "{\"result\":" + formatNumber(result) + "}"));
        } catch (const std::exception& e) {
            sendAll(client, httpResponse(400, "application/json; charset=utf-8",
                                         "{\"error\":\"" + jsonEscape(e.what()) + "\"}"));
        }
    } else if (method == "GET") {
        if (path.find("..") != std::string::npos) {
            sendAll(client, httpResponse(404, "text/plain", "Not found"));
        } else {
            std::string relative = (path == "/") ? "/index.html" : path;
            std::string fullPath = g_frontendDir + relative;
            if (fileExists(fullPath)) {
                sendAll(client, httpResponse(200, mimeType(fullPath), readFile(fullPath)));
            } else {
                sendAll(client, httpResponse(404, "text/plain", "Not found: " + path));
            }
        }
    } else {
        sendAll(client, httpResponse(405, "text/plain", "Method not allowed"));
    }

    shutdown(client, SD_BOTH);
    closesocket(client);
}

unsigned __stdcall clientThread(void* arg) {
    SOCKET client = (SOCKET)(uintptr_t)arg;
    handleClient(client);
    return 0;
}

// ---------------------------------------------------------------------
//  Startup
// ---------------------------------------------------------------------

std::string directoryOf(const std::string& filePath) {
    size_t slash = filePath.find_last_of("\\/");
    return (slash == std::string::npos) ? "." : filePath.substr(0, slash);
}

bool resolveFrontendDirectory(const std::string& exePath) {
    std::string exeDir = directoryOf(exePath);
    std::vector<std::string> candidates;
    candidates.push_back(exeDir + "/frontend");   // exe next to frontend/
    candidates.push_back(exeDir + "/../frontend");// exe inside build/
    candidates.push_back("frontend");              // current working directory
    candidates.push_back("../frontend");
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (fileExists(candidates[i] + "/index.html")) {
            g_frontendDir = candidates[i];
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    // Show server messages immediately, even when output is piped/redirected
    // (e.g. VS Code task terminal).
    std::cout << std::unitbuf;

    std::cout << "=============================================\n";
    std::cout << "   Calculator App  -  C++ Backend Server\n";
    std::cout << "=============================================\n\n";

    char exePathBuffer[MAX_PATH];
    GetModuleFileNameA(0, exePathBuffer, MAX_PATH);
    if (!resolveFrontendDirectory(exePathBuffer)) {
        std::cerr << "ERROR: could not locate the 'frontend' folder.\n"
                  << "Run this program from the project root folder.\n";
        return 1;
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "ERROR: WSAStartup failed.\n";
        return 1;
    }

    // Listen on localhost only - no Windows Firewall popup.
    // If 8080 is busy, try the next ports up to 8095.
    SOCKET listener = INVALID_SOCKET;
    int port = 0;
    for (int candidate = 8080; candidate <= 8095; ++candidate) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) break;
        sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons((u_short)candidate);
        if (bind(s, (sockaddr*)&address, sizeof(address)) == 0 && listen(s, SOMAXCONN) == 0) {
            listener = s;
            port = candidate;
            break;
        }
        closesocket(s);
    }
    if (listener == INVALID_SOCKET) {
        std::cerr << "ERROR: could not bind to any port between 8080 and 8095.\n";
        WSACleanup();
        return 1;
    }

    std::ostringstream urlStream;
    urlStream << "http://localhost:" << port;
    std::string url = urlStream.str();

    std::cout << "  Frontend : " << g_frontendDir << "\n";
    std::cout << "  Server   : " << url << "\n";
    std::cout << "  API      : POST " << url << "/api/calculate\n\n";
    std::cout << "  Serving calculator... close this window (or press Ctrl+C) to stop.\n\n";

    // Open the calculator in the default browser.
    // Set environment variable CALC_NO_BROWSER=1 to disable (used by tests).
    if (std::getenv("CALC_NO_BROWSER") == 0) {
        std::string cmd = "start \"\" " + url;
        system(cmd.c_str());
    }

    while (true) {
        sockaddr_in clientAddress;
        int clientAddressSize = sizeof(clientAddress);
        SOCKET client = accept(listener, (sockaddr*)&clientAddress, &clientAddressSize);
        if (client == INVALID_SOCKET) continue;
        uintptr_t threadHandle = _beginthreadex(0, 0, clientThread, (void*)(uintptr_t)client, 0, 0);
        if (threadHandle != 0) {
            CloseHandle((HANDLE)threadHandle);
        } else {
            handleClient(client);   // fall back to sequential handling
        }
    }
}
