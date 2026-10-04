# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2026 MuseScore Limited and others
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

muse_create_module(muse_update ALIAS muse::update)

target_sources(muse_update PRIVATE
    updatemodule.cpp
    updatemodule.h
    updatetypes.h
    updateerrors.h
    iupdateconfiguration.h
    iappupdatescenario.h
    iappupdateservice.h

    internal/updateconfiguration.cpp
    internal/updateconfiguration.h
    internal/updateuiactions.cpp
    internal/updateuiactions.h
    internal/updateactioncontroller.cpp
    internal/updateactioncontroller.h
    internal/appupdatescenario.cpp
    internal/appupdatescenario.h
    internal/appupdateservice.cpp
    internal/appupdateservice.h

    # StarScore: updates come from the fork's GitHub releases; see starscoregithubrelease.h
    internal/starscoregithubrelease.cpp
    internal/starscoregithubrelease.h
)

# StarScore: the running version to compare against GitHub is STARSCORE_VERSION (version.cmake), not MuseScore's.
target_compile_definitions(muse_update PRIVATE STARSCORE_VERSION_STR="${STARSCORE_VERSION}")

# StarScore: the macOS install script (internal/starscore_macos_install.sh) is embedded as a string, so the app can
# write it to a temporary file and run it detached when it quits. configure_file rewrites the header only when the
# script's contents changed, and CMAKE_CONFIGURE_DEPENDS makes an edit to the script re-run the configure step.
set(STARSCORE_MACOS_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/internal/starscore_macos_install.sh")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${STARSCORE_MACOS_INSTALL_SCRIPT_FILE}")
file(READ "${STARSCORE_MACOS_INSTALL_SCRIPT_FILE}" STARSCORE_MACOS_INSTALL_SCRIPT)
configure_file(internal/starscore_macos_install_script.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/starscore_macos_install_script.h" @ONLY)
unset(STARSCORE_MACOS_INSTALL_SCRIPT)

if (MUSE_QT_SUPPORT)
    target_link_libraries(muse_update PRIVATE Qt::Concurrent)
endif()

if (MUSE_MODULE_UPDATE_TESTS)
    add_subdirectory(tests)
endif()

if (MUSE_MODULE_UPDATE_QML)
    add_subdirectory(qml/Muse/Update)
endif()