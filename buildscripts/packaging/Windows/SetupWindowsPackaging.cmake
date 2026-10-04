include(GetPlatformInfo)

if(NOT OS_IS_WIN)
    return()
endif()

include(InstallRequiredSystemLibraries)

set(CPACK_PACKAGE_NAME ${MUSE_APP_NAME})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MuseScore is a full featured WYSIWYG score editor")
set(CPACK_PACKAGE_VENDOR "MuseScore Limited")
set(CPACK_PACKAGE_CONTACT "https://musescore.org")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://musescore.org")

set(CPACK_PACKAGE_VERSION_MAJOR "${MUSE_APP_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${MUSE_APP_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${MUSE_APP_VERSION_PATCH}")
set(CPACK_PACKAGE_VERSION_BUILD "${CMAKE_BUILD_NUMBER}")
set(CPACK_PACKAGE_VERSION "${MUSE_APP_VERSION_MAJOR}.${MUSE_APP_VERSION_MINOR}.${MUSE_APP_VERSION_PATCH}.${CPACK_PACKAGE_VERSION_BUILD}")

# StarScore: the installer carries StarScore Studio's own version (STARSCORE_VERSION in version.cmake), not the
# MuseScore version it is based on. With MuseScore's 4.7.5 every StarScore installer would have had the same
# product version, and Windows could not tell a newer one from an older one. The major version also goes into
# the ProgId suffix of the file-type registry keys (WIX.template.in), which keeps them apart from MuseScore Studio 4's.
if(STARSCORE_VERSION)
    string(REPLACE "." ";" STARSCORE_VERSION_PARTS "${STARSCORE_VERSION}")
    list(GET STARSCORE_VERSION_PARTS 0 CPACK_PACKAGE_VERSION_MAJOR)
    list(GET STARSCORE_VERSION_PARTS 1 CPACK_PACKAGE_VERSION_MINOR)
    list(GET STARSCORE_VERSION_PARTS 2 CPACK_PACKAGE_VERSION_PATCH)
    set(CPACK_PACKAGE_VERSION "${STARSCORE_VERSION}.${CPACK_PACKAGE_VERSION_BUILD}")
endif()
message("CPACK_PACKAGE_VERSION: ${CPACK_PACKAGE_VERSION}")

set(git_date_string "")

if(MUSE_APP_UNSTABLE)
    find_program(GIT_EXECUTABLE git PATHS ENV PATH)

    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" log -1 --date=short --format=%cd
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE git_date
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()

    if(git_date)
        string(REGEX REPLACE "-" "" git_date "${git_date}")
        set(git_date_string "~git${git_date}")
    endif()
endif(MUSE_APP_UNSTABLE)

set(CPACK_PACKAGE_FILE_NAME "${MUSE_APP_NAME}-${MUSE_APP_VERSION}${git_date_string}")
# StarScore: install into "Program Files\StarScore Studio". MUSE_APP_NAME_VERSION is "MuseScore 4" (kept for
# compatibility), and installing there would overwrite a MuseScore Studio 4 installation on the same machine.
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${MUSE_APP_TITLE}")

set(MUSESCORE_EXECUTABLE_NAME ${MUSE_APP_NAME}${MUSE_APP_VERSION_MAJOR})
set(CPACK_PACKAGE_EXECUTABLES "${MUSESCORE_EXECUTABLE_NAME}" "${MUSE_APP_TITLE_VERSION}") # exe name, label
set(CPACK_CREATE_DESKTOP_LINKS "${MUSESCORE_EXECUTABLE_NAME}" "${MUSE_APP_TITLE_VERSION}") # exe name, label

# Wix-specific options
set(CPACK_GENERATOR "WIX")

file(TO_CMAKE_PATH $ENV{PROGRAMFILES} PROGRAMFILES)
set(CPACK_WIX_ROOT "${PROGRAMFILES}/WiX Toolset v3.11")

# Use custom version of WIX.template.in
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/buildscripts/packaging/Windows/Installer" ${CMAKE_MODULE_PATH})

if(NOT CPACK_WIX_PRODUCT_GUID)
    set(CPACK_WIX_PRODUCT_GUID "00000000-0000-0000-0000-000000000000")
endif()

message(STATUS "[SetupWindowsPackaging.cmake] CPACK_WIX_PRODUCT_GUID: ${CPACK_WIX_PRODUCT_GUID}")

if(NOT CPACK_WIX_UPGRADE_GUID)
    set(CPACK_WIX_UPGRADE_GUID "11111111-1111-1111-1111-111111111111")
endif()

message(STATUS "[SetupWindowsPackaging.cmake] CPACK_WIX_UPGRADE_GUID: ${CPACK_WIX_UPGRADE_GUID}")

set(CPACK_WIX_LICENSE_RTF "${PROJECT_SOURCE_DIR}/buildscripts/packaging/Windows/Installer/LICENSE.rtf")
set(CPACK_WIX_PRODUCT_ICON "${PROJECT_SOURCE_DIR}/share/icons/AppIcon/MS4_AppIcon.ico")
set(CPACK_WIX_UI_BANNER "${PROJECT_SOURCE_DIR}/buildscripts/packaging/Windows/Installer/installer_banner_wix.png")
set(CPACK_WIX_UI_DIALOG "${PROJECT_SOURCE_DIR}/buildscripts/packaging/Windows/Installer/installer_background_wix.png")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "${MUSE_APP_TITLE_VERSION}")
set(CPACK_WIX_EXTENSIONS "WixUtilExtension")

# Extra CPack variables
list(APPEND CPACK_WIX_CANDLE_EXTRA_FLAGS
    "-dMUSE_APP_TITLE_VERSION=${MUSE_APP_TITLE_VERSION}"
    "-dMUSE_APP_TITLE=${MUSE_APP_TITLE}"
    "-dMUSESCORE_EXECUTABLE_NAME=${MUSESCORE_EXECUTABLE_NAME}"
    "-dMUSE_APP_RELEASE_CHANNEL=${MUSE_APP_RELEASE_CHANNEL}"
    "-dCPACK_PACKAGE_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}"
)

if (MUSE_APP_IS_PRERELEASE)
    list(APPEND CPACK_WIX_CANDLE_EXTRA_FLAGS "-dMUSE_APP_IS_PRERELEASE=ON")
endif()

include(CPack)
