#ifndef AUTH_MODULE_H
#define AUTH_MODULE_H

#include "Database.h"
#include <string>

class AuthModule {
public:
    explicit AuthModule(const std::string& databasePath = "database/users.db");
    bool isReady() const;
    bool registerUser(const std::string& login, const std::string& password, std::string& message);
    bool loginUser(const std::string& login, const std::string& password, std::string& message) const;

private:
    Database database_;
    static bool validLogin(const std::string& login);
    static bool validPassword(const std::string& password);
};

#endif
