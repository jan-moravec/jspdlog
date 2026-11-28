set(VCPKG_BUILD_TYPE release) # header-only

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO jan-moravec/jspdlog
    REF "v${VERSION}"
    SHA512 e6964ee8c25fc8bd01b7ca08e8811ca83922bc31f85c77d418d209f30bff171303b27f9b45a67da019064014b395fde8e1c8fc3fe160a5230dcdb6a7ec6a2edc
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
