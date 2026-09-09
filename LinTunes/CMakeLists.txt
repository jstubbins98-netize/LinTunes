cmake_minimum_required(VERSION 3.16)
project(lintunes VERSION 1.0.0 LANGUAGES CXX)

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

# GTK3
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

# libgpod (iPod sync)
pkg_check_modules(GPOD REQUIRED libgpod-1.0)

# libcdio (CD ripping)
pkg_check_modules(CDIO       REQUIRED libcdio)
pkg_check_modules(CDIO_PARA  REQUIRED libcdio_paranoia)
pkg_check_modules(CDDB       REQUIRED libcddb)

# libVLC (internet radio)
pkg_check_modules(VLC REQUIRED libvlc)

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
add_executable(lintunes ${SOURCES} ${HEADERS})

target_compile_options(lintunes PRIVATE
    ${GTK3_CFLAGS_OTHER}
    ${GST_CFLAGS_OTHER}
    ${TAGLIB_CFLAGS_OTHER}
    ${GPOD_CFLAGS_OTHER}
    ${CDIO_CFLAGS_OTHER}
    ${VLC_CFLAGS_OTHER}
)

target_include_directories(lintunes PRIVATE
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

target_link_libraries(lintunes PRIVATE
    ${GTK3_LIBRARIES}
    ${GST_LIBRARIES}
    ${TAGLIB_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    ${GPOD_LIBRARIES}
    ${CDIO_LIBRARIES}
    ${CDIO_PARA_LIBRARIES}
    ${CDDB_LIBRARIES}
    ${VLC_LIBRARIES}
)

# stdc++fs was merged into libstdc++ in GCC 9; only link it on older compilers
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(lintunes PRIVATE stdc++fs)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(lintunes PRIVATE stdc++fs)
endif()

target_link_directories(lintunes PRIVATE
    ${GTK3_LIBRARY_DIRS}
    ${GST_LIBRARY_DIRS}
    ${TAGLIB_LIBRARY_DIRS}
    ${SQLITE3_LIBRARY_DIRS}
    ${GPOD_LIBRARY_DIRS}
    ${CDIO_LIBRARY_DIRS}
    ${CDIO_PARA_LIBRARY_DIRS}
    ${CDDB_LIBRARY_DIRS}
    ${VLC_LIBRARY_DIRS}
)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS lintunes DESTINATION bin)

install(FILES resources/lintunes.desktop
    DESTINATION share/applications
    OPTIONAL
)

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "GTK3        : ${GTK3_VERSION}")
message(STATUS "GStreamer   : ${GST_VERSION}")
message(STATUS "TagLib      : ${TAGLIB_VERSION}")
message(STATUS "SQLite3     : ${SQLITE3_VERSION}")
message(STATUS "libgpod     : ${GPOD_VERSION}")
message(STATUS "libcdio     : ${CDIO_VERSION}")
message(STATUS "libcddb     : ${CDDB_VERSION}")
message(STATUS "libVLC      : ${VLC_VERSION}")
