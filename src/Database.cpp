#include "Database.h"
#include <filesystem>

Database::Database(const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) { error_ = "Cannot create database directory: " + ec.message(); return; }
    }
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        error_ = db_ ? sqlite3_errmsg(db_) : "SQLite could not allocate a database handle";
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return;
    }
    sqlite3_busy_timeout(db_, 3000);
    if (!initialize()) {
        sqlite3_close(db_); db_ = nullptr;
    }
}

Database::~Database() { if (db_) sqlite3_close(db_); }
bool Database::isOpen() const { return db_ != nullptr; }
std::string Database::lastError() const { return error_; }

bool Database::initialize() {
    const char* sql = "CREATE TABLE IF NOT EXISTS users ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "login TEXT NOT NULL UNIQUE,"
                      "password_hash TEXT NOT NULL,"
                      "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);";
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        error_ = err ? err : sqlite3_errmsg(db_);
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool Database::userExists(const std::string& login) const {
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT 1 FROM users WHERE login = ? LIMIT 1", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

bool Database::addUser(const std::string& login, const std::string& passwordHash) {
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "INSERT INTO users(login, password_hash) VALUES(?, ?)", -1, &stmt, nullptr) != SQLITE_OK) {
        error_ = sqlite3_errmsg(db_); return false;
    }
    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, passwordHash.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) error_ = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    return ok;
}

std::optional<std::string> Database::getPasswordHash(const std::string& login) const {
    if (!db_) return std::nullopt;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT password_hash FROM users WHERE login = ? LIMIT 1", -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) result = std::string(text);
    }
    sqlite3_finalize(stmt);
    return result;
}
