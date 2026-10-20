vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Countly/countly-sdk-cpp
    REF "${VERSION}"
    SHA512 0
    HEAD_REF master
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        sqlite COUNTLY_USE_SQLITE
        sqlite COUNTLY_USE_SYSTEM_SQLITE
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DCOUNTLY_USE_SYSTEM_JSON=ON
        -DCOUNTLY_BUILD_TESTS=OFF
        -DCOUNTLY_BUILD_SAMPLE=OFF
        # keeps the SDK's macOS Homebrew lookup from replacing vcpkg's OpenSSL
        "-DOPENSSL_ROOT_DIR=${CURRENT_INSTALLED_DIR}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME countly CONFIG_PATH lib/cmake/countly)
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
