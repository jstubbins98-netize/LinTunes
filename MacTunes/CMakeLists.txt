cmake_minimum_required(VERSION 3.16)
project(MacTunes VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build type ────────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# ── pkg-config ────────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

# GTK3 and the playback/database stack are required for the GUI.
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# GStreamer (core + audio)
pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-audio-1.0
)

# TagLib (metadata)
pkg_check_modules(TAGLIB REQUIRED taglib)

# SQLite3
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# These integrations are optional.  libgpod is not currently published in
# Homebrew's core repository and libcdio is unavailable on some macOS setups.
if(APPLE)
    set(MACTUNES_IPOD_DEFAULT OFF)
else()
    set(MACTUNES_IPOD_DEFAULT ON)
endif()
option(MACTUNES_ENABLE_IPOD "Build libgpod iPod synchronization" ${MACTUNES_IPOD_DEFAULT})
option(MACTUNES_ENABLE_CD "Build libcdio CD ripping integration" ON)
option(MACTUNES_ENABLE_RADIO "Build libVLC internet radio integration" ON)
if(MACTUNES_ENABLE_IPOD)
    pkg_check_modules(GPOD QUIET libgpod-1.0)
    if(NOT GPOD_FOUND)
        message(WARNING "libgpod not found: iPod sync will be disabled")
        set(MACTUNES_ENABLE_IPOD OFF)
    endif()
endif()

# libcdio (CD ripping)
if(MACTUNES_ENABLE_CD)
    pkg_check_modules(CDIO       QUIET libcdio)
    pkg_check_modules(CDIO_PARA  QUIET libcdio_paranoia)
    pkg_check_modules(CDDB       QUIET libcddb)
    if(NOT CDIO_FOUND OR NOT CDIO_PARA_FOUND OR NOT CDDB_FOUND)
        message(WARNING "libcdio/libcddb not found: CD features will be disabled")
        set(MACTUNES_ENABLE_CD OFF)
    endif()
endif()

# libVLC (internet radio)
if(MACTUNES_ENABLE_RADIO)
pkg_check_modules(VLC QUIET libvlc)
if(NOT VLC_FOUND)
    message(WARNING "libVLC not found: internet radio will be disabled")
    set(MACTUNES_ENABLE_RADIO OFF)
endif()
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/Database.cpp
    src/Library.cpp
    src/AudioPlayer.cpp
    src/RadioPlayer.cpp
    src/iPodSync.cpp
    src/iPodRestore.cpp
    src/CDManager.cpp
    src/MainWindow.cpp
)

set(HEADERS
    src/Track.h
    src/Database.h
    src/Library.h
    src/AudioPlayer.h
    src/RadioPlayer.h
    src/iPodSync.h
    src/iPodRestore.h
    src/CDManager.h
    src/MainWindow.h
)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(mactunes ${SOURCES} ${HEADERS})
if(APPLE)
    # GTK3 remains a regular executable; packaging it as an app bundle is left
    # to the distributor because GTK resources vary between Homebrew installs.
    set_target_properties(mactunes PROPERTIES MACOSX_RPATH ON)
endif()
if(MACTUNES_ENABLE_IPOD)
    target_compile_definitions(mactunes PRIVATE MACTUNES_HAVE_GPOD=1)
endif()
if(MACTUNES_ENABLE_CD)
    target_compile_definitions(mactunes PRIVATE MACTUNES_HAVE_CDIO=1)
endif()
if(MACTUNES_ENABLE_RADIO)
    target_compile_definitions(mactunes PRIVATE MACTUNES_HAVE_VLC=1)
endif()

target_compile_options(mactunes PRIVATE
    ${GTK3_CFLAGS_OTHER}
    ${GST_CFLAGS_OTHER}
    ${TAGLIB_CFLAGS_OTHER}
    ${GPOD_CFLAGS_OTHER}
    ${CDIO_CFLAGS_OTHER}
    ${VLC_CFLAGS_OTHER}
)

target_include_directories(mactunes PRIVATE
    ${GTK3_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}
    ${TAGLIB_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
    ${GPOD_INCLUDE_DIRS}
    ${CDIO_INCLUDE_DIRS}
    ${CDIO_PARA_INCLUDE_DIRS}
    ${CDDB_INCLUDE_DIRS}
    ${VLC_INCLUDE_DIRS}
    src/
)

target_link_libraries(mactunes PRIVATE
    ${GTK3_LIBRARIES}
    ${GST_LIBRARIES}
    ${TAGLIB_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${VLC_LIBRARIES}
)
if(MACTUNES_ENABLE_IPOD)
    target_compile_options(mactunes PRIVATE ${GPOD_CFLAGS_OTHER})
    target_include_directories(mactunes PRIVATE ${GPOD_INCLUDE_DIRS})
    target_link_libraries(mactunes PRIVATE ${GPOD_LIBRARIES})
endif()
if(MACTUNES_ENABLE_CD)
    target_compile_options(mactunes PRIVATE ${CDIO_CFLAGS_OTHER})
    target_include_directories(mactunes PRIVATE ${CDIO_INCLUDE_DIRS} ${CDIO_PARA_INCLUDE_DIRS} ${CDDB_INCLUDE_DIRS})
    target_link_libraries(mactunes PRIVATE ${CDIO_LIBRARIES} ${CDIO_PARA_LIBRARIES} ${CDDB_LIBRARIES})
endif()

# stdc++fs was merged into libstdc++ in GCC 9; only link it on older compilers
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(mactunes PRIVATE stdc++fs)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(mactunes PRIVATE stdc++fs)
endif()

target_link_directories(mactunes PRIVATE
    ${GTK3_LIBRARY_DIRS}
    ${GST_LIBRARY_DIRS}
    ${TAGLIB_LIBRARY_DIRS}
    ${SQLITE3_LIBRARY_DIRS}
    ${VLC_LIBRARY_DIRS}
)
if(MACTUNES_ENABLE_IPOD)
    target_link_directories(mactunes PRIVATE ${GPOD_LIBRARY_DIRS})
endif()
if(MACTUNES_ENABLE_CD)
    target_link_directories(mactunes PRIVATE ${CDIO_LIBRARY_DIRS} ${CDIO_PARA_LIBRARY_DIRS} ${CDDB_LIBRARY_DIRS})
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS mactunes DESTINATION bin)

if(NOT APPLE)
install(FILES resources/mactunes.desktop
    DESTINATION share/applications
    OPTIONAL
)
endif()

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "GTK3        : ${GTK3_VERSION}")
message(STATUS "GStreamer   : ${GST_VERSION}")
message(STATUS "TagLib      : ${TAGLIB_VERSION}")
message(STATUS "SQLite3     : ${SQLITE3_VERSION}")
message(STATUS "iPod sync   : ${MACTUNES_ENABLE_IPOD}")
message(STATUS "CD support  : ${MACTUNES_ENABLE_CD}")
message(STATUS "Radio       : ${MACTUNES_ENABLE_RADIO}")
message(STATUS "libVLC      : ${VLC_VERSION}")
