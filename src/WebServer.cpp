#include "WebServer.h"
#include <arpa/inet.h>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {
std::string htmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) { case '&': out += "&amp;"; break; case '<': out += "&lt;"; break; case '>': out += "&gt;"; break; case '"': out += "&quot;"; break; case '\'': out += "&#39;"; break; default: out += c; }
    }
    return out;
}
std::string urlDecode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+' ) out += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) {
            try { out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)); i += 2; } catch (...) { out += s[i]; }
        } else out += s[i];
    }
    return out;
}
std::string field(const std::string& body, const std::string& key) {
    std::size_t pos = 0;
    while (pos <= body.size()) {
        const auto end = body.find('&', pos);
        const auto part = body.substr(pos, end == std::string::npos ? end : end - pos);
        const auto eq = part.find('=');
        if (eq != std::string::npos && urlDecode(part.substr(0, eq)) == key) return urlDecode(part.substr(eq + 1));
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return {};
}
std::string page(const std::string& title, const std::string& content) {
    return "<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + htmlEscape(title) + "</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><main class=\"card\"><h1>" + htmlEscape(title) + "</h1>" + content + "</main></body></html>";
}
std::string formPage(const std::string& notice = "") {
    const std::string note = notice.empty() ? "" : "<p class=\"notice\">" + htmlEscape(notice) + "</p>";
    return page("Система авторизации", note +
      "<section><h2>Вход</h2><form method=\"post\" action=\"/login\"><label>Логин<input name=\"login\" minlength=\"3\" maxlength=\"32\" required></label><label>Пароль<input name=\"password\" type=\"password\" minlength=\"8\" maxlength=\"128\" required></label><button>Войти</button></form></section>"
      "<section><h2>Регистрация</h2><form method=\"post\" action=\"/register\"><label>Логин<input name=\"login\" minlength=\"3\" maxlength=\"32\" required></label><label>Пароль<input name=\"password\" type=\"password\" minlength=\"8\" maxlength=\"128\" required></label><button>Создать аккаунт</button></form></section>");
}
std::string response(const std::string& status, const std::string& type, const std::string& body) {
    return "HTTP/1.1 " + status + "\r\nContent-Type: " + type + "; charset=utf-8\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; style-src 'self' 'unsafe-inline'\r\n\r\n" + body;
}
void sendAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) { const auto n = send(fd, data.data() + sent, data.size() - sent, 0); if (n <= 0) return; sent += static_cast<std::size_t>(n); }
}
}

WebServer::WebServer(AuthModule& auth) : auth_(auth) {}
int WebServer::run(int port) {
    const int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { std::cerr << "Не удалось создать сокет.\n"; return 1; }
    int yes = 1; setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(static_cast<unsigned short>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(server, 10) < 0) {
        std::cerr << "Не удалось запустить сервер на 127.0.0.1:" << port << ". Возможно, порт занят.\n"; close(server); return 1;
    }
    std::cout << "Сервер запущен: http://127.0.0.1:" << port << " (остановка Ctrl+C)\n";
    while (true) {
        sockaddr_in clientAddress{}; socklen_t clientLength = sizeof(clientAddress);
        const int client = accept(server, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
        if (client < 0) continue;
        timeval timeout{3, 0}; setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        std::string request; char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
            const auto n = recv(client, buffer, sizeof(buffer), 0); if (n <= 0) break; request.append(buffer, static_cast<std::size_t>(n));
        }
        std::string output;
        const auto lineEnd = request.find("\r\n");
        if (lineEnd == std::string::npos) output = response("400 Bad Request", "text/html", page("Ошибка", "<p>Некорректный запрос.</p>"));
        else {
            std::istringstream line(request.substr(0, lineEnd)); std::string method, path, version; line >> method >> path >> version;
            const auto headerEnd = request.find("\r\n\r\n");
            std::size_t contentLength = 0, pos = lineEnd + 2;
            while (pos < headerEnd) {
                const auto end = request.find("\r\n", pos); if (end == std::string::npos || end > headerEnd) break;
                const auto header = request.substr(pos, end - pos); const auto colon = header.find(':');
                if (colon != std::string::npos) {
                    std::string name = header.substr(0, colon); for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (name == "content-length") { try { contentLength = std::stoul(header.substr(colon + 1)); } catch (...) {} }
                }
                pos = end + 2;
            }
            if (contentLength > 8192) output = response("413 Payload Too Large", "text/html", page("Ошибка", "<p>Форма слишком большая.</p>"));
            else {
                const auto bodyStart = headerEnd == std::string::npos ? request.size() : headerEnd + 4;
                while (request.size() - std::min(request.size(), bodyStart) < contentLength && request.size() < 24576) {
                    const auto n = recv(client, buffer, sizeof(buffer), 0); if (n <= 0) break; request.append(buffer, static_cast<std::size_t>(n));
                }
                const std::string body = bodyStart <= request.size() ? request.substr(bodyStart, contentLength) : "";
                if (method == "GET" && (path == "/" || path == "/login")) output = response("200 OK", "text/html", formPage());
                else if (method == "GET" && path == "/style.css") {
                    const std::string css = "*{box-sizing:border-box}body{margin:0;background:#10131a;color:#e8eaf0;font:16px system-ui,sans-serif;display:flex;justify-content:center;padding:32px 14px}.card{width:min(560px,100%);background:#1a1f2b;border:1px solid #303849;border-radius:18px;padding:28px;box-shadow:0 20px 60px #0005}h1{margin-top:0}section{margin-top:28px;padding-top:12px;border-top:1px solid #303849}label{display:block;margin:14px 0;color:#c8ceda}input{display:block;width:100%;margin-top:7px;padding:12px;border-radius:9px;border:1px solid #414b60;background:#111620;color:#fff}button{padding:11px 16px;border:0;border-radius:9px;background:#8ab4ff;color:#10131a;font-weight:700;cursor:pointer}.notice{background:#2b3547;padding:12px;border-radius:9px}.success{color:#a7f3c0}a{color:#8ab4ff}";
                    output = response("200 OK", "text/css", css);
                } else if (method == "POST" && (path == "/register" || path == "/login")) {
                    const auto login = field(body, "login"), password = field(body, "password"); std::string message;
                    const bool ok = path == "/register" ? auth_.registerUser(login, password, message) : auth_.loginUser(login, password, message);
                    if (ok && path == "/login") output = response("200 OK", "text/html", page("Успешный вход", "<p class=\"success\">Вы успешно вошли в систему, " + htmlEscape(login) + "!</p><p><a href=\"/\">Вернуться</a></p>"));
                    else output = response("200 OK", "text/html", formPage(message));
                } else output = response("404 Not Found", "text/html", page("Страница не найдена", "<p>Такой страницы нет.</p><p><a href=\"/\">На главную</a></p>"));
            }
        }
        sendAll(client, output); shutdown(client, SHUT_RDWR); close(client);
    }
    close(server); return 0;
}
