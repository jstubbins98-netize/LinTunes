# Packages contain the installed executable, desktop launcher, and manuals.
# Build on the target distribution: these are not self-contained binaries.
set(CPACK_PACKAGE_NAME "lintunes")
set(CPACK_PACKAGE_VENDOR "LinTunes")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Music library manager with iPod sync and CD support")
set(CPACK_PACKAGE_DESCRIPTION
    "LinTunes is a GTK3 desktop music manager with GStreamer playback, libVLC internet radio, playlists, Windows/FAT iPod synchronization, and CD ripping and burning.")
set(LINTUNES_PACKAGE_MAINTAINER
    "LinTunes local build <lintunes@localhost>" CACHE STRING
    "Maintainer identity for locally generated packages")
set(CPACK_PACKAGE_CONTACT "${LINTUNES_PACKAGE_MAINTAINER}")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PACKAGE_FILE_NAME "lintunes-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "TGZ")
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_RELOCATABLE FALSE)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(LINTUNES_HOMEPAGE_URL "" CACHE STRING "Public LinTunes project homepage")
set(CPACK_PACKAGE_HOMEPAGE_URL "${LINTUNES_HOMEPAGE_URL}")

# rpmbuild generates ELF requirements from the locally installed shared libs.
# Codec/encoder recommendations differ between distributions and repositories.
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_AUTOREQPROV "yes")
set(_lintunes_rpm_plugins "gstreamer1-plugins-base, gstreamer1-plugins-good")
if(EXISTS "/etc/os-release")
    file(STRINGS "/etc/os-release" _lintunes_distro REGEX "^(ID|ID_LIKE)=")
    if("${_lintunes_distro}" MATCHES "(opensuse|suse|sles)")
        set(_lintunes_rpm_plugins "gstreamer-plugins-base, gstreamer-plugins-good")
    endif()
endif()
set(CPACK_RPM_PACKAGE_REQUIRES "${_lintunes_rpm_plugins}, vlc, udisks2, dosfstools")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
# Do not claim ownership of system directories shared with other packages.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/usr/share/applications" "/usr/share/doc")

include(CPack)
