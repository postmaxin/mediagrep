# .deb and .rpm packages, built with `cpack` from the install rules in
# CMakeLists.txt. Included only on Linux. Run, after a Release build:
#
#     cpack --config build/CPackConfig.cmake -B dist
#
# These are our own release packages, not the distributions' (there is no
# debian/ directory or .spec file). They install under /usr.

set(CPACK_PACKAGE_NAME mediagrep)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Tyler MacDonald <postmaxin@users.noreply.github.com>")
set(CPACK_PACKAGE_VENDOR "Tyler MacDonald")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/postmaxin/mediagrep")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "grep for media file metadata")
set(CPACK_PACKAGE_DESCRIPTION
"mediagrep searches the metadata of audio and video files (codecs, bit rates,
durations, tags, and what the file name says) with a regular expression or
a field expression. It reads the files with MediaInfoLib.")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
set(CPACK_GENERATOR "DEB;RPM")
set(CPACK_STRIP_FILES ON)
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)

# mediagrep loads MediaInfoLib with dlopen, so neither dpkg-shlibdeps nor
# rpm's automatic requirements see it. Name the package that provides
# libmediainfo.so.0 for each family.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libmediainfo0v5")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)   # adds libc6 (the rest is static)
set(CPACK_DEBIAN_PACKAGE_RELEASE 1)       # 0.1.0-1: not a Debian-native package
set(CPACK_DEBIAN_PACKAGE_SECTION utils)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)  # mediagrep_0.1.0_amd64.deb

set(CPACK_RPM_PACKAGE_REQUIRES "libmediainfo")
set(CPACK_RPM_PACKAGE_LICENSE BSD-2-Clause)
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_RELEASE 1)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)     # mediagrep-0.1.0-1.x86_64.rpm
set(CPACK_RPM_COMPRESSION_TYPE xz)
# Fedora's filesystem package owns these.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/man /usr/share/man/man1)

# Debian policy wants the license at share/doc/mediagrep/copyright and man
# pages gzipped; the script below does both in the staging tree
# (CPACK_PRE_BUILD_SCRIPTS needs CMake 3.19; building the packages does, the
# program does not).
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageInstall.cmake.in
               ${CMAKE_CURRENT_BINARY_DIR}/PackageInstall.cmake @ONLY)
set(CPACK_PRE_BUILD_SCRIPTS ${CMAKE_CURRENT_BINARY_DIR}/PackageInstall.cmake)

include(CPack)
