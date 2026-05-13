cmake_minimum_required(VERSION 3.16)
project(jspdlog_install_smoke LANGUAGES CXX)

# Consume jspdlog from the install tree. Typical CI usage:
#   cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/tmp/jspdlog-prefix
#   cmake --build build --target install
#   cmake -S tests/install_smoke -B build-smoke \
#         -DCMAKE_PREFIX_PATH=/tmp/jspdlog-prefix
#   cmake --build build-smoke
find_package(jspdlog CONFIG REQUIRED)

add_executable(jspdlog_install_smoke main.cpp)
target_link_libraries(jspdlog_install_smoke PRIVATE jspdlog::jspdlog)
