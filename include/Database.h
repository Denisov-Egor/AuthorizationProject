#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include <optional>
#include <string>

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool isOpen() const;
    bool userExists(const std::string& login) const;
    bool addUser(const std::string& login, const std::string& passwordHash);
    std::optional<std::string> getPasswordHash(const std::string& login) const;
    std::string lastError() const;

private:
    sqlite3* db_ = nullptr;
    std::string error_;
    bool initialize();
};

#endif
