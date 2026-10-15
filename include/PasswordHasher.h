#ifndef PASSWORD_HASHER_H
#define PASSWORD_HASHER_H

#include <string>

class PasswordHasher {
public:
    static std::string hashPassword(const std::string& password);
    static bool verifyPassword(const std::string& password, const std::string& encodedHash);
};

#endif
