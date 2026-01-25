cmake_minimum_required(VERSION 3.13)

project(git-starter)

set(CMAKE_CXX_STANDARD 23)

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

# Explicitly list source files (Best Practice for maintainability)
add_executable(git
    src/main.cpp
    src/Utils.cpp
    src/Repository.cpp
    src/Cloner.cpp
)

target_link_libraries(git PRIVATE OpenSSL::Crypto ZLIB::ZLIB)