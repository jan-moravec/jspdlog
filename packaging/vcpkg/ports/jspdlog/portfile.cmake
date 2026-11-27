set(VCPKG_BUILD_TYPE release) # header-only

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO jan-moravec/jspdlog
    REF "v${VERSION}"
    SHA512 515c6142347249484e3fe4d74d6c86a29dabde3bd1bc5a6fb42eb6bb52662cf7a0d8450533c9463bc30f71c9f6717a90236e676bed38eed382255e583f43212f
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DJSPDLOG_BUILD_TESTS=OFF
        -DJSPDLOG_BUILD_EXAMPLES=OFF
        -DJSPDLOG_INSTALL=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/jspdlog)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
