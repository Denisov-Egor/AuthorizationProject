#include "AuthModule.h"
#include "WebServer.h"
#include <iostream>
#include <limits>
#include <string>

namespace {
void clearLine() { std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); }
std::string readLine(const std::string& prompt) {
    std::cout << prompt;
    std::string value;
    std::getline(std::cin, value);
    return value;
}
void consoleMode(AuthModule& auth) {
    while (true) {
        std::cout << "\n=== Система авторизации ===\n1. Регистрация\n2. Вход\n0. Выход\nВыберите действие: ";
        int choice = -1;
        if (!(std::cin >> choice)) { std::cin.clear(); clearLine(); std::cout << "Введите число.\n"; continue; }
        clearLine();
        if (choice == 0) break;
        if (choice != 1 && choice != 2) { std::cout << "Неизвестный пункт меню.\n"; continue; }
        const auto login = readLine("Логин: ");
        const auto password = readLine("Пароль: ");
        std::string message;
        const bool ok = choice == 1 ? auth.registerUser(login, password, message) : auth.loginUser(login, password, message);
        std::cout << (ok ? "[OK] " : "[ОШИБКА] ") << message << '\n';
    }
}
}

int main(int argc, char* argv[]) {
    AuthModule auth;
    if (!auth.isReady()) {
        std::cerr << "Не удалось открыть базу данных. Проверьте каталог database/.\n";
        return 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--web") {
        WebServer server(auth);
        return server.run(8080);
    }
    consoleMode(auth);
    return 0;
}
