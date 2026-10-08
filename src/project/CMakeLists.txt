# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2021 MuseScore Limited and others
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
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

muse_create_module(project)

include(GetPlatformInfo)

target_sources(project PRIVATE
    projectmodule.cpp
    projectmodule.h
    projecterrors.h
    projectextensionpoints.h
    iexportprojectscenario.h
    inotationproject.h
    iprojectcreator.h
    inotationreader.h
    inotationreadersregister.h
    inotationwriter.h
    inotationwritersregister.h
    iprojectaudiosettings.h
    iprojectfilescontroller.h
    iprojectconfiguration.h
    irecentfilescontroller.h
    imscmetareader.h
    iprojectautosaver.h
    iprojectrwregister.h
    iprojectwriter.h
    iconvertfiletoscorescenario.h
    iconvertfiletoscoreservice.h
    istarscoreservice.h

    types/projecttypes.h
    types/projectmeta.h
    types/converttypes.h

    internal/iprojectmigrator.h
    internal/notationproject.cpp
    internal/notationproject.h
    internal/projectcreator.cpp
    internal/projectcreator.h
    internal/projectaudiosettings.cpp
    internal/projectaudiosettings.h
    internal/notationreadersregister.cpp
    internal/notationreadersregister.h
    internal/notationwritersregister.cpp
    internal/notationwritersregister.h
    internal/projectautosaver.cpp
    internal/projectautosaver.h
    internal/convertfiletoscorescenario.cpp
    internal/convertfiletoscorescenario.h
    internal/convertfiletoscoreservice.cpp
    internal/convertfiletoscoreservice.h
    internal/projectactionscontroller.cpp
    internal/projectactionscontroller.h
    internal/engravingpluginapihelper.cpp
    internal/engravingpluginapihelper.h
    internal/projectuiactions.cpp
    internal/projectuiactions.h
    internal/projectconfiguration.cpp
    internal/projectconfiguration.h
    internal/exporttype.cpp
    internal/exporttype.h
    internal/exportprojectscenario.cpp
    internal/exportprojectscenario.h
    internal/iopensaveprojectscenario.h
    internal/opensaveprojectscenario.cpp
    internal/opensaveprojectscenario.h
    internal/recentfilescontroller.cpp
    internal/recentfilescontroller.h
    internal/mscmetareader.cpp
    internal/mscmetareader.h
    internal/itemplatesrepository.h
    internal/templatesrepository.cpp
    internal/templatesrepository.h
    internal/mdlmigrator.cpp
    internal/mdlmigrator.h
    internal/projectmigrator.cpp
    internal/projectmigrator.h
    internal/projectfileinfoprovider.cpp
    internal/projectfileinfoprovider.h
    internal/projectrwregister.cpp
    internal/projectrwregister.h

    internal/starscore/starscoreservice.cpp
    internal/starscore/starscorebandexport.cpp
    internal/starscore/starscoresheetrecord.cpp
    internal/starscore/starscoretodo.cpp
    internal/starscore/starscoreautotest.cpp
    internal/starscore/starscoreservice.h
    internal/starscore/starscoreengraving.cpp
    internal/starscore/starscorehouse.cpp
    internal/starscore/starscorehouse.h
    internal/starscore/starscorecompare.cpp
    internal/starscore/starscoreaudit.cpp
    internal/starscore/starscoreimport.cpp
    internal/starscore/starscoremixer.cpp
    internal/starscore/starscoredemos.cpp
    internal/starscore/starscoreupdate.cpp
    internal/starscore/starscoredoubletime.cpp
    internal/starscore/starscorestandardize.cpp
    internal/starscore/starscoreprogress.cpp
    internal/starscore/starscorehorncopy.cpp
    internal/starscore/starscoreautoarchive.cpp
    internal/starscore/starscoreautoarchive.h
    internal/starscore/starscorepdf.cpp
    internal/starscore/starscorepdf.h
    internal/starscore/starscoreengraving.h
    internal/starscore/starscorechordchart.cpp
    internal/starscore/starscorechordchart.h

    internal/starscore/organizer/orgcore.cpp
    internal/starscore/organizer/orgcore.h
    internal/starscore/organizer/orgonline.cpp
    internal/starscore/organizer/orgonline.h
    internal/starscore/organizer/orgscan.cpp
    internal/starscore/organizer/orgscan.h
    internal/starscore/organizer/orglibrary.cpp
    internal/starscore/organizer/orglibrary.h
    internal/starscore/organizer/orgstores.cpp
    internal/starscore/organizer/orgstores.h
    internal/starscore/organizer/orgfiling.cpp
    internal/starscore/organizer/orgfiling.h
    internal/starscore/organizer/orgchangelog.cpp
    internal/starscore/organizer/orgchangelog.h
    internal/starscore/organizer/orgcolours.cpp
    internal/starscore/organizer/orgcolours.h
    internal/starscore/organizer/orgrecordings.cpp
    internal/starscore/organizer/orgrecordings.h
    internal/starscore/organizer/orghtml.cpp
    internal/starscore/organizer/orghtml.h
    internal/starscore/organizer/orghtml_recordings.cpp
    internal/starscore/organizer/orghtml_players.cpp
    internal/starscore/organizer/orghtml_home.cpp
    internal/starscore/organizer/organizer.cpp
    internal/starscore/organizer/organizer.h
    internal/starscore/organizer/orgplatform.h
)

# StarScore organizer: each file is its own translation unit (file-local helpers share names)
set_source_files_properties(
    internal/starscore/organizer/orgcore.cpp
    internal/starscore/organizer/orgonline.cpp
    internal/starscore/organizer/orgscan.cpp
    internal/starscore/organizer/orglibrary.cpp
    internal/starscore/organizer/orgstores.cpp
    internal/starscore/organizer/orgfiling.cpp
    internal/starscore/organizer/orgchangelog.cpp
    internal/starscore/organizer/orgcolours.cpp
    internal/starscore/organizer/orgrecordings.cpp
    internal/starscore/organizer/orghtml.cpp
    internal/starscore/organizer/orghtml_recordings.cpp
    internal/starscore/organizer/orghtml_players.cpp
    internal/starscore/organizer/orghtml_home.cpp
    internal/starscore/organizer/organizer.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

# StarScore audit: its own translation unit (file-local helper types)
set_source_files_properties(internal/starscore/starscoreaudit.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(internal/starscore/starscoresheetrecord.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(internal/starscore/starscoretodo.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(internal/starscore/starscoreautotest.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# StarScore chord charts: its own translation unit (file-local helper types named like the audit's)
set_source_files_properties(internal/starscore/starscorechordchart.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
# zstd (BSD, facebook/zstd 1.5.7, single-file build) for the autosave archive's packed song histories
target_sources(project PRIVATE thirdparty/zstd/zstd.c thirdparty/zstd/zstd.h)
set_source_files_properties(thirdparty/zstd/zstd.c PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON SKIP_PRECOMPILE_HEADERS ON COMPILE_OPTIONS "-w")
# the StarScore version, for the automatic test run's log
target_compile_definitions(project PRIVATE STARSCORE_VERSION_STR="${STARSCORE_VERSION}")

if (OS_IS_MAC)
    target_sources(project PRIVATE
        internal/platform/macos/macosrecentfilescontroller.mm
        internal/platform/macos/macosrecentfilescontroller.h
    )
    # StarScore: reference PDFs are drawn with CoreGraphics; the organizer prints its PDFs with WebKit and reads
    # sheets with PDFKit
    target_sources(project PRIVATE internal/starscore/organizer/orgplatform_mac.mm)
    set_source_files_properties(internal/starscore/organizer/orgplatform_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(project PRIVATE "-framework CoreGraphics" "-framework CoreFoundation" "-framework CoreText"
        "-framework AppKit" "-framework WebKit" "-framework PDFKit")
    # Don't mix C++ and Objective-C++ in Unity Build
    set_source_files_properties(
        internal/platform/macos/macosrecentfilescontroller.mm
        internal/starscore/starscorepdf.cpp
        internal/starscore/organizer/orgplatform_mac.mm
        PROPERTIES
        SKIP_UNITY_BUILD_INCLUSION ON
        SKIP_PRECOMPILE_HEADERS ON
    )
elseif (OS_IS_WIN)
    target_sources(project PRIVATE
        internal/platform/windows/windowsrecentfilescontroller.cpp
        internal/platform/windows/windowsrecentfilescontroller.h
    )
endif()
if (NOT OS_IS_MAC)
    target_sources(project PRIVATE internal/starscore/organizer/orgplatform_other.cpp)
endif()

target_link_libraries(project PRIVATE notation)

if (QT_ADD_CONCURRENT)
    target_link_libraries(project PRIVATE Qt::Concurrent)
endif()

if (MUE_BUILD_PROJECT_TESTS)
    add_subdirectory(tests)
endif()

if (MUE_BUILD_PROJECT_QML)
    add_subdirectory(qml/MuseScore/Project)
endif()

# StarScore: built-in default part-score style (Starsign 2.6)
qt_add_resources(project project_starscore_resources
    PREFIX
        /
    FILES
        resources/starscore/Starsign_2.6.mss
        resources/starscore/mixer_defaults.json
)
