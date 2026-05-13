cmake_minimum_required(VERSION 3.16)

project(
    jspdlog
    VERSION 0.1.0
    DESCRIPTION "JSON-line logging on top of spdlog v2."
    HOMEPAGE_URL "https://github.com/hidglobal/jspdlog"
    LANGUAGES CXX
)

# Detect whether we are the top-level project; only then turn on tests/examples
# by default. Users embedding via add_subdirectory get a quiet build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(JSPDLOG_IS_TOP_LEVEL ON)
else()
    set(JSPDLOG_IS_TOP_LEVEL OFF)
endif()

option(JSPDLOG_BUILD_TESTS "Build jspdlog unit tests" ${JSPDLOG_IS_TOP_LEVEL})
option(JSPDLOG_BUILD_EXAMPLES "Build jspdlog examples" ${JSPDLOG_IS_TOP_LEVEL})
option(JSPDLOG_INSTALL "Generate install rules for jspdlog" ${JSPDLOG_IS_TOP_LEVEL})
# JSPDLOG_TEST_NLOHMANN_INTEROP is declared inside tests/CMakeLists.txt so its
# default tracks the live value of JSPDLOG_BUILD_TESTS across reconfigures
# (option()'s first-seen default would otherwise stick).

# --- Find spdlog --------------------------------------------------------------
# Required: the only runtime dependency. fmt comes transitively. spdlog v2
# is currently maintained on the `v2.x` branch and is not yet tagged, so
# the FetchContent fallback pins a specific commit on that branch.
find_package(spdlog 2 CONFIG QUIET)
if(NOT spdlog_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG
            b8944a4bcd478ee03375c9c50dc8d6c741f43f7b # pinned commit on spdlog's v2.x branch
    )
    # Forward our install opt-in to spdlog so that `cmake --install` ships a
    # spdlog package alongside jspdlog when no system spdlog was found.
    set(SPDLOG_INSTALL ${JSPDLOG_INSTALL} CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(spdlog)
endif()

# --- The library: a single-header INTERFACE target ----------------------------
add_library(jspdlog INTERFACE)
add_library(jspdlog::jspdlog ALIAS jspdlog)

target_compile_features(jspdlog INTERFACE cxx_std_17)
target_include_directories(
    jspdlog
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
)
target_link_libraries(jspdlog INTERFACE spdlog::spdlog)

# --- Install rules ------------------------------------------------------------
# jspdlog is a pure INTERFACE (header-only) target. To stay friendly to users
# who pull spdlog via FetchContent (which is incompatible with install(EXPORT)),
# we install only the header and a hand-rolled config file that recreates the
# INTERFACE IMPORTED target at find_package() time.
if(JSPDLOG_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/jspdlog
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
        PATTERN "*.h"
    )

    set(JSPDLOG_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/jspdlog)

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/jspdlogConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/jspdlogConfig.cmake
        INSTALL_DESTINATION ${JSPDLOG_INSTALL_CMAKEDIR}
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/jspdlogConfigVersion.cmake
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/jspdlogConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/jspdlogConfigVersion.cmake
        DESTINATION ${JSPDLOG_INSTALL_CMAKEDIR}
    )
endif()

# --- Tests and examples -------------------------------------------------------
if(JSPDLOG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(JSPDLOG_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
