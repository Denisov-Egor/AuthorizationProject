
#include "WebServer.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>

    using Socket = SOCKET;
    using SocketLength = int;

    constexpr Socket INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>

    using Socket = int;
    using SocketLength = socklen_t;

    constexpr Socket INVALID_SOCKET_VALUE = -1;
#endif

namespace {

constexpr std::size_t MAX_HEADER_SIZE = 16384;
constexpr std::size_t MAX_BODY_SIZE = 8192;
constexpr int RECEIVE_TIMEOUT_SECONDS = 3;

#ifdef _WIN32

bool initializeSockets()
{
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void cleanupSockets()
{
    WSACleanup();
}

#else

bool initializeSockets()
{
    return true;
}

void cleanupSockets()
{
}

#endif

bool isValidSocket(Socket socket)
{
    return socket != INVALID_SOCKET_VALUE;
}

void closeSocket(Socket socket)
{
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

void shutdownSocket(Socket socket)
{
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
}

int receiveData(Socket socket, char* buffer, std::size_t size)
{
#ifdef _WIN32
    const int receiveSize = static_cast<int>(
        std::min(size, static_cast<std::size_t>(
            std::numeric_limits<int>::max()
        ))
    );

    return recv(socket, buffer, receiveSize, 0);
#else
    return static_cast<int>(recv(socket, buffer, size, 0));
#endif
}

int sendData(Socket socket, const char* data, std::size_t size)
{
#ifdef _WIN32
    const int sendSize = static_cast<int>(
        std::min(size, static_cast<std::size_t>(
            std::numeric_limits<int>::max()
        ))
    );

    return send(socket, data, sendSize, 0);
#else
    return static_cast<int>(send(socket, data, size, 0));
#endif
}

bool setReceiveTimeout(Socket socket)
{
#ifdef _WIN32
    const DWORD timeout = RECEIVE_TIMEOUT_SECONDS * 1000;

    return setsockopt(
        socket,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout),
        sizeof(timeout)
    ) == 0;
#else
    timeval timeout{};
    timeout.tv_sec = RECEIVE_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    return setsockopt(
        socket,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    ) == 0;
#endif
}

bool setReuseAddress(Socket socket)
{
    int enabled = 1;

#ifdef _WIN32
    return setsockopt(
        socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&enabled),
        sizeof(enabled)
    ) == 0;
#else
    return setsockopt(
        socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &enabled,
        sizeof(enabled)
    ) == 0;
#endif
}

bool bindAddress(Socket socket, sockaddr_in& address)
{
    return bind(
        socket,
        reinterpret_cast<sockaddr*>(&address),
        static_cast<SocketLength>(sizeof(address))
    ) == 0;
}

bool listenSocket(Socket socket)
{
    return listen(socket, 10) == 0;
}

Socket acceptConnection(Socket socket)
{
    sockaddr_in clientAddress{};
    SocketLength clientLength = sizeof(clientAddress);

    return accept(
        socket,
        reinterpret_cast<sockaddr*>(&clientAddress),
        &clientLength
    );
}

bool setLoopbackAddress(sockaddr_in& address)
{
#ifdef _WIN32
    return InetPtonA(
        AF_INET,
        "127.0.0.1",
        &address.sin_addr
    ) == 1;
#else
    return inet_pton(
        AF_INET,
        "127.0.0.1",
        &address.sin_addr
    ) == 1;
#endif
}

std::string toLower(std::string value)
{
    for (char& c : value) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))
        );
    }

    return value;
}

std::string trim(const std::string& value)
{
    const std::size_t first = value.find_first_not_of(" \t");

    if (first == std::string::npos) {
        return {};
    }

    const std::size_t last = value.find_last_not_of(" \t");

    return value.substr(first, last - first + 1);
}

std::string htmlEscape(const std::string& value)
{
    std::string result;

    for (char c : value) {
        switch (c) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&#39;";
                break;
            default:
                result += c;
                break;
        }
    }

    return result;
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

std::string urlDecode(const std::string& value)
{
    std::string result;

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            result += ' ';
        }
        else if (value[i] == '%' && i + 2 < value.size()) {
            const int high = hexValue(value[i + 1]);
            const int low = hexValue(value[i + 2]);

            if (high != -1 && low != -1) {
                result += static_cast<char>(high * 16 + low);
                i += 2;
            }
            else {
                result += value[i];
            }
        }
        else {
            result += value[i];
        }
    }

    return result;
}

std::string field(
    const std::string& body,
    const std::string& key
)
{
    std::size_t position = 0;

    while (position <= body.size()) {
        const std::size_t end = body.find('&', position);

        const std::string part = body.substr(
            position,
            end == std::string::npos
                ? std::string::npos
                : end - position
        );

        const std::size_t equal = part.find('=');

        if (equal != std::string::npos) {
            const std::string fieldName =
                urlDecode(part.substr(0, equal));

            if (fieldName == key) {
                return urlDecode(part.substr(equal + 1));
            }
        }

        if (end == std::string::npos) {
            break;
        }

        position = end + 1;
    }

    return {};
}

std::string page(
    const std::string& title,
    const std::string& content
)
{
    return
        "<!doctype html>"
        "<html lang=\"ru\">"
        "<head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>" + htmlEscape(title) + "</title>"
        "<link rel=\"stylesheet\" href=\"/style.css\">"
        "</head>"
        "<body>"
        "<main class=\"card\">"
        "<h1>" + htmlEscape(title) + "</h1>"
        + content +
        "</main>"
        "</body>"
        "</html>";
}

std::string formPage(const std::string& notice = "")
{
    const std::string notification = notice.empty()
        ? ""
        : "<p class=\"notice\">" + htmlEscape(notice) + "</p>";

    return page(
        "Система авторизации",
        notification +
        "<section>"
        "<h2>Вход</h2>"
        "<form method=\"post\" action=\"/login\">"
        "<label>Логин"
        "<input name=\"login\" minlength=\"3\" maxlength=\"32\" required>"
        "</label>"
        "<label>Пароль"
        "<input name=\"password\" type=\"password\" minlength=\"8\" maxlength=\"128\" required>"
        "</label>"
        "<button>Войти</button>"
        "</form>"
        "</section>"
        "<section>"
        "<h2>Регистрация</h2>"
        "<form method=\"post\" action=\"/register\">"
        "<label>Логин"
        "<input name=\"login\" minlength=\"3\" maxlength=\"32\" required>"
        "</label>"
        "<label>Пароль"
        "<input name=\"password\" type=\"password\" minlength=\"8\" maxlength=\"128\" required>"
        "</label>"
        "<button>Создать аккаунт</button>"
        "</form>"
        "</section>"
    );
}

std::string response(
    const std::string& status,
    const std::string& contentType,
    const std::string& body
)
{
    return
        "HTTP/1.1 " + status + "\r\n"
        "Content-Type: " + contentType + "; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'self'; style-src 'self' 'unsafe-inline'\r\n"
        "\r\n" +
        body;
}

bool sendAll(Socket socket, const std::string& data)
{
    std::size_t sent = 0;

    while (sent < data.size()) {
        const int result = sendData(
            socket,
            data.data() + sent,
            data.size() - sent
        );

        if (result <= 0) {
            return false;
        }

        sent += static_cast<std::size_t>(result);
    }

    return true;
}

bool parseContentLength(
    const std::string& value,
    std::size_t& result
)
{
    const std::string normalized = trim(value);

    if (normalized.empty()) {
        return false;
    }

    try {
        std::size_t processed = 0;

        const unsigned long long parsed =
            std::stoull(normalized, &processed);

        if (processed != normalized.size()) {
            return false;
        }

        if (parsed > std::numeric_limits<std::size_t>::max()) {
            return false;
        }

        result = static_cast<std::size_t>(parsed);
        return true;
    }
    catch (...) {
        return false;
    }
}

std::string css()
{
    return
        "*{box-sizing:border-box}"
        "body{margin:0;background:#10131a;color:#e8eaf0;"
        "font:16px system-ui,sans-serif;display:flex;"
        "justify-content:center;padding:32px 14px}"
        ".card{width:min(560px,100%);background:#1a1f2b;"
        "border:1px solid #303849;border-radius:18px;"
        "padding:28px;box-shadow:0 20px 60px #0005}"
        "h1{margin-top:0}"
        "section{margin-top:28px;padding-top:12px;"
        "border-top:1px solid #303849}"
        "label{display:block;margin:14px 0;color:#c8ceda}"
        "input{display:block;width:100%;margin-top:7px;"
        "padding:12px;border-radius:9px;border:1px solid #414b60;"
        "background:#111620;color:#fff}"
        "button{padding:11px 16px;border:0;border-radius:9px;"
        "background:#8ab4ff;color:#10131a;font-weight:700;cursor:pointer}"
        ".notice{background:#2b3547;padding:12px;border-radius:9px}"
        ".success{color:#a7f3c0}"
        "a{color:#8ab4ff}";
}

} // namespace

WebServer::WebServer(AuthModule& auth)
    : auth_(auth)
{
}

int WebServer::run(int port)
{
    if (!initializeSockets()) {
        std::cerr << "Не удалось инициализировать сетевую подсистему.\n";
        return 1;
    }

    const Socket server = socket(AF_INET, SOCK_STREAM, 0);

    if (!isValidSocket(server)) {
        std::cerr << "Не удалось создать сокет.\n";
        cleanupSockets();
        return 1;
    }

    setReuseAddress(server);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));

    if (!setLoopbackAddress(address)) {
        std::cerr << "Не удалось настроить адрес сервера.\n";
        closeSocket(server);
        cleanupSockets();
        return 1;
    }

    if (!bindAddress(server, address) || !listenSocket(server)) {
        std::cerr
            << "Не удалось запустить сервер на 127.0.0.1:"
            << port
            << ". Возможно, порт занят.\n";

        closeSocket(server);
        cleanupSockets();
        return 1;
    }

    std::cout
        << "Сервер запущен: http://127.0.0.1:"
        << port
        << " (остановка Ctrl+C)\n";

    while (true) {
        const Socket client = acceptConnection(server);

        if (!isValidSocket(client)) {
            continue;
        }

        setReceiveTimeout(client);

        std::string request;
        char buffer[4096];

        // Чтение HTTP-заголовков.
        while (
            request.find("\r\n\r\n") == std::string::npos &&
            request.size() <= MAX_HEADER_SIZE
        ) {
            const int received = receiveData(
                client,
                buffer,
                sizeof(buffer)
            );

            if (received <= 0) {
                break;
            }

            request.append(
                buffer,
                static_cast<std::size_t>(received)
            );
        }

        std::string output;

        const std::size_t headerEnd = request.find("\r\n\r\n");

        if (headerEnd == std::string::npos) {
            if (request.size() > MAX_HEADER_SIZE) {
                output = response(
                    "431 Request Header Fields Too Large",
                    "text/html",
                    page("Ошибка", "<p>Заголовки запроса слишком большие.</p>")
                );
            }
            else {
                output = response(
                    "400 Bad Request",
                    "text/html",
                    page("Ошибка", "<p>Некорректный HTTP-запрос.</p>")
                );
            }
        }
        else if (headerEnd > MAX_HEADER_SIZE) {
            output = response(
                "431 Request Header Fields Too Large",
                "text/html",
                page("Ошибка", "<p>Заголовки запроса слишком большие.</p>")
            );
        }
        else {
            const std::size_t lineEnd = request.find("\r\n");

            std::istringstream requestLine(
                request.substr(0, lineEnd)
            );

            std::string method;
            std::string path;
            std::string version;

            requestLine >> method >> path >> version;

            bool invalidHeaders = false;
            bool hasTransferEncoding = false;

            std::size_t contentLength = 0;
            bool hasContentLength = false;

            // Разбор HTTP-заголовков.
            std::size_t position = lineEnd + 2;

            while (position < headerEnd) {
                const std::size_t end = request.find(
                    "\r\n",
                    position
                );

                if (end == std::string::npos || end > headerEnd) {
                    break;
                }

                const std::string header = request.substr(
                    position,
                    end - position
                );

                const std::size_t colon = header.find(':');

                if (colon == std::string::npos) {
                    invalidHeaders = true;
                    break;
                }

                const std::string name = toLower(
                    trim(header.substr(0, colon))
                );

                const std::string value = trim(
                    header.substr(colon + 1)
                );

                if (name == "content-length") {
                    std::size_t parsedLength = 0;

                    if (!parseContentLength(value, parsedLength)) {
                        invalidHeaders = true;
                        break;
                    }

                    if (
                        hasContentLength &&
                        parsedLength != contentLength
                    ) {
                        invalidHeaders = true;
                        break;
                    }

                    contentLength = parsedLength;
                    hasContentLength = true;
                }
                else if (name == "transfer-encoding") {
                    hasTransferEncoding = true;
                }

                position = end + 2;
            }

            if (
                method.empty() ||
                path.empty() ||
                version != "HTTP/1.1" ||
                invalidHeaders ||
                hasTransferEncoding
            ) {
                output = response(
                    "400 Bad Request",
                    "text/html",
                    page("Ошибка", "<p>Некорректный HTTP-запрос.</p>")
                );
            }
            else if (contentLength > MAX_BODY_SIZE) {
                output = response(
                    "413 Payload Too Large",
                    "text/html",
                    page("Ошибка", "<p>Форма слишком большая.</p>")
                );
            }
            else {
                const std::size_t bodyStart = headerEnd + 4;
                const std::size_t expectedSize =
                    bodyStart + contentLength;

                // Чтение тела запроса.
                while (request.size() < expectedSize) {
                    const int received = receiveData(
                        client,
                        buffer,
                        sizeof(buffer)
                    );

                    if (received <= 0) {
                        break;
                    }

                    request.append(
                        buffer,
                        static_cast<std::size_t>(received)
                    );
                }

                if (request.size() < expectedSize) {
                    output = response(
                        "400 Bad Request",
                        "text/html",
                        page("Ошибка", "<p>Тело запроса получено не полностью.</p>")
                    );
                }
                else {
                    const std::string body = request.substr(
                        bodyStart,
                        contentLength
                    );

                    // Убираем query string из URL.
                    const std::size_t queryPosition = path.find('?');

                    if (queryPosition != std::string::npos) {
                        path.erase(queryPosition);
                    }

                    if (
                        method == "GET" &&
                        (path == "/" || path == "/login")
                    ) {
                        output = response(
                            "200 OK",
                            "text/html",
                            formPage()
                        );
                    }
                    else if (
                        method == "GET" &&
                        path == "/style.css"
                    ) {
                        output = response(
                            "200 OK",
                            "text/css",
                            css()
                        );
                    }
                    else if (
                        method == "POST" &&
                        (path == "/register" || path == "/login")
                    ) {
                        const std::string login = field(body, "login");
                        const std::string password = field(body, "password");

                        std::string message;

                        const bool success = path == "/register"
                            ? auth_.registerUser(login, password, message)
                            : auth_.loginUser(login, password, message);

                        if (success && path == "/login") {
                            output = response(
                                "200 OK",
                                "text/html",
                                page(
                                    "Успешный вход",
                                    "<p class=\"success\">"
                                    "Вы успешно вошли в систему, " +
                                    htmlEscape(login) +
                                    "!</p>"
                                    "<p><a href=\"/\">Вернуться</a></p>"
                                )
                            );
                        }
                        else {
                            output = response(
                                "200 OK",
                                "text/html",
                                formPage(message)
                            );
                        }
                    }
                    else {
                        output = response(
                            "404 Not Found",
                            "text/html",
                            page(
                                "Страница не найдена",
                                "<p>Такой страницы нет.</p>"
                                "<p><a href=\"/\">На главную</a></p>"
                            )
                        );
                    }
                }
            }
        }

        sendAll(client, output);

        shutdownSocket(client);
        closeSocket(client);
    }

    closeSocket(server);
    cleanupSockets();

    return 0;
}
