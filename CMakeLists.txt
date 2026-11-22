cmake_minimum_required(VERSION 3.16)

project(
    jspdlog
    VERSION 0.1.0
    DESCRIPTION "JSON-line logging on top of spdlog."
    HOMEPAGE_URL "https://github.com/jan-moravec/jspdlog"
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
option(JSPDLOG_WARNINGS_AS_ERRORS "Treat warnings in jspdlog's tests and examples as errors" OFF)
# JSPDLOG_TEST_NLOHMANN_INTEROP is declared inside test/CMakeLists.txt so its
# default tracks the live value of JSPDLOG_BUILD_TESTS across reconfigures
# (option()'s first-seen default would otherwise stick).

# Dependencies fetched with FetchContent are marked SYSTEM (CMake >= 3.25), so the strict warnings applied to the
# tests and examples report jspdlog's header only, not spdlog's, {fmt}'s or Catch2's.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
    set(JSPDLOG_FETCH_SYSTEM SYSTEM)
else()
    set(JSPDLOG_FETCH_SYSTEM "")
endif()

# --- Find spdlog --------------------------------------------------------------
# The only dependency ({fmt} comes with it). Both spdlog v1.x (>= 1.12) and
# v2.x are supported. An installed spdlog is preferred; otherwise
# JSPDLOG_SPDLOG_GIT_TAG is fetched. The default pins a commit on spdlog's
# untagged v2.x branch; pass e.g. -DJSPDLOG_SPDLOG_GIT_TAG=v1.17.0 for v1.
set(JSPDLOG_SPDLOG_GIT_TAG
    "d24088deaa441a79267df8ae3dbc567fbe2a5e03"
    CACHE STRING
    "spdlog tag/commit to fetch when no installed spdlog is found"
)
find_package(spdlog CONFIG QUIET)
if(spdlog_FOUND)
    if(spdlog_VERSION VERSION_LESS 1.12)
        message(FATAL_ERROR "jspdlog requires spdlog >= 1.12, found ${spdlog_VERSION}")
    endif()
else()
    include(FetchContent)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG ${JSPDLOG_SPDLOG_GIT_TAG}
        ${JSPDLOG_FETCH_SYSTEM}
    )
    # Ship spdlog alongside jspdlog on `cmake --install` when we fetched it.
    set(SPDLOG_INSTALL ${JSPDLOG_INSTALL} CACHE BOOL "")
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
        COMPATIBILITY SameMinorVersion # 0.x: minor versions may break the API
        ARCH_INDEPENDENT
    )

    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/jspdlogConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/jspdlogConfigVersion.cmake
        DESTINATION ${JSPDLOG_INSTALL_CMAKEDIR}
    )
endif()

# --- Tests and examples -------------------------------------------------------
if(JSPDLOG_BUILD_TESTS OR JSPDLOG_BUILD_EXAMPLES)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
endif()

if(JSPDLOG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(JSPDLOG_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()
