cmake_minimum_required(VERSION 3.20)
project(AuthorizationProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(authorization
    src/main.cpp
    src/AuthModule.cpp
    src/Database.cpp
    src/PasswordHasher.cpp
    src/WebServer.cpp
)
target_include_directories(authorization PRIVATE include)
target_link_libraries(authorization PRIVATE SQLite::SQLite3 OpenSSL::Crypto)
target_compile_options(authorization PRIVATE -Wall -Wextra -Wpedantic)
