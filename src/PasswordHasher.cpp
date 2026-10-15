#include "PasswordHasher.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {
constexpr int ITERATIONS = 200000;
constexpr int SALT_BYTES = 16;
constexpr int HASH_BYTES = 32;
std::string toHex(const unsigned char* data, std::size_t size) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) out << std::setw(2) << static_cast<unsigned int>(data[i]);
    return out.str();
}
bool fromHex(const std::string& hex, std::vector<unsigned char>& out) {
    if (hex.size() % 2 != 0) return false;
    out.clear(); out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        unsigned int value = 0;
        std::istringstream in(hex.substr(i, 2));
        in >> std::hex >> value;
        if (in.fail()) return false;
        out.push_back(static_cast<unsigned char>(value));
    }
    return true;
}
}

std::string PasswordHasher::hashPassword(const std::string& password) {
    unsigned char salt[SALT_BYTES];
    unsigned char hash[HASH_BYTES];
    if (RAND_bytes(salt, sizeof(salt)) != 1) return {};
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt, sizeof(salt),
                          ITERATIONS, EVP_sha256(), sizeof(hash), hash) != 1) return {};
    return "pbkdf2_sha256$" + std::to_string(ITERATIONS) + "$" + toHex(salt, sizeof(salt)) + "$" + toHex(hash, sizeof(hash));
}

bool PasswordHasher::verifyPassword(const std::string& password, const std::string& encodedHash) {
    const auto first = encodedHash.find('$');
    const auto second = first == std::string::npos ? first : encodedHash.find('$', first + 1);
    const auto third = second == std::string::npos ? second : encodedHash.find('$', second + 1);
    if (first == std::string::npos || second == std::string::npos || third == std::string::npos || encodedHash.substr(0, first) != "pbkdf2_sha256") return false;
    int iterations = 0;
    try { iterations = std::stoi(encodedHash.substr(first + 1, second - first - 1)); } catch (...) { return false; }
    if (iterations < 10000 || iterations > 2000000) return false;
    std::vector<unsigned char> salt, expected;
    if (!fromHex(encodedHash.substr(second + 1, third - second - 1), salt) || !fromHex(encodedHash.substr(third + 1), expected)) return false;
    if (salt.size() != SALT_BYTES || expected.size() != HASH_BYTES) return false;
    unsigned char actual[HASH_BYTES];
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(), static_cast<int>(salt.size()),
                          iterations, EVP_sha256(), sizeof(actual), actual) != 1) return false;
    return CRYPTO_memcmp(actual, expected.data(), sizeof(actual)) == 0;
}
