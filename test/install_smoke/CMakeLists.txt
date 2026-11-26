cmake_minimum_required(VERSION 3.16)
project(jspdlog_install_smoke LANGUAGES CXX)

# Consume jspdlog from the install tree. Typical CI usage:
#   cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/tmp/jspdlog-prefix
#   cmake --build build --target install
#   cmake -S test/install_smoke -B build-smoke \
#         -DCMAKE_PREFIX_PATH=/tmp/jspdlog-prefix
#   cmake --build build-smoke
find_package(jspdlog CONFIG REQUIRED)

# JSPDLOG_ACTIVE_LEVEL is the consumer's choice, per target or translation unit, so the package must not define it.
get_target_property(jspdlog_definitions jspdlog::jspdlog INTERFACE_COMPILE_DEFINITIONS)
if(jspdlog_definitions MATCHES "JSPDLOG_ACTIVE_LEVEL")
    message(FATAL_ERROR "The jspdlog package must not define JSPDLOG_ACTIVE_LEVEL")
endif()

add_executable(jspdlog_install_smoke main.cpp)
target_link_libraries(jspdlog_install_smoke PRIVATE jspdlog::jspdlog)
target_compile_definitions(jspdlog_install_smoke PRIVATE JSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
