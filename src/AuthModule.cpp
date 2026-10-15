#include "AuthModule.h"
#include "PasswordHasher.h"
#include <cctype>

AuthModule::AuthModule(const std::string& databasePath) : database_(databasePath) {}
bool AuthModule::isReady() const { return database_.isOpen(); }

bool AuthModule::validLogin(const std::string& login) {
    if (login.size() < 3 || login.size() > 32) return false;
    for (unsigned char c : login) if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}
bool AuthModule::validPassword(const std::string& password) { return password.size() >= 8 && password.size() <= 128; }

bool AuthModule::registerUser(const std::string& login, const std::string& password, std::string& message) {
    if (!database_.isOpen()) { message = "Ошибка базы данных: " + database_.lastError(); return false; }
    if (!validLogin(login)) { message = "Логин должен содержать 3–32 символа: латинские буквы, цифры, _ или -."; return false; }
    if (!validPassword(password)) { message = "Пароль должен содержать от 8 до 128 символов."; return false; }
    if (database_.userExists(login)) { message = "Пользователь с таким логином уже существует."; return false; }
    const auto hash = PasswordHasher::hashPassword(password);
    if (hash.empty()) { message = "Не удалось безопасно обработать пароль."; return false; }
    if (!database_.addUser(login, hash)) { message = "Не удалось сохранить пользователя: " + database_.lastError(); return false; }
    message = "Регистрация прошла успешно.";
    return true;
}

bool AuthModule::loginUser(const std::string& login, const std::string& password, std::string& message) const {
    if (!database_.isOpen()) { message = "Ошибка базы данных."; return false; }
    const auto storedHash = database_.getPasswordHash(login);
    if (!storedHash || !PasswordHasher::verifyPassword(password, *storedHash)) {
        message = "Неверный логин или пароль.";
        return false;
    }
    message = "Авторизация прошла успешно.";
    return true;
}
