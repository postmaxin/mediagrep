#!/bin/sh
# Build and check the Entware .ipk inside a clean Debian container. The
# release workflow runs this; so can you, with Docker:
#
#   docker run --rm -v "$PWD:/src:ro" -v "$PWD/out:/out" debian:stable-slim \
#       sh /src/tools/entware-ci.sh build x64-k3.2 x64-3.2
#   docker run --rm -v "$PWD:/src:ro" -v "$PWD/out:/out:ro" debian:stable-slim \
#       sh /src/tools/entware-ci.sh check x64-k3.2
#
#   build FEED ARCH   Install Entware's toolchain, build and test mediagrep
#                     natively (so it links Entware's glibc, with the
#                     interpreter /opt/lib/ld-linux-*), and write the .ipk
#                     to /out with tools/make-ipk.sh.
#   check FEED        In a fresh Entware, `opkg install` the .ipk from /out
#                     and run it, which proves Depends is enough.
#
# FEED is the directory name on bin.entware.net (x64-k3.2, aarch64-k3.10,
# armv7sf-k3.2). ARCH is opkg's name for it (x64-3.2, aarch64-3.10, armv7-3.2).
# /src is the source tree and /out the directory for the package.
set -eu

mode=${1:?usage: entware-ci.sh build FEED ARCH | check FEED}
feed=${2:?missing FEED}

install_entware() {
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends wget ca-certificates >/dev/null
    mkdir -p /opt
    wget -q -O - "http://bin.entware.net/$feed/installer/generic.sh" | sh
    PATH=/opt/bin:/opt/sbin:$PATH
    export PATH
    opkg update
    echo "== opkg architectures"
    opkg print-architecture
}

case "$mode" in
build)
    arch=${3:?missing ARCH}
    install_entware
    opkg install gcc make cmake libmediainfo
    # Debian's own tar and gzip pack the .ipk; Entware's binaries are not needed for that.
    version=$(sed -n 's/^project(mediagrep VERSION \([0-9.]*\).*/\1/p' /src/CMakeLists.txt)-1

    # A writable copy: the tests and the build must not touch /src. Not cp -a:
    # the files must belong to root, who has a passwd entry here; the tests
    # look up the owner and group of a fixture by name.
    mkdir -p /build/src
    cp -R /src/. /build/src
    cd /build/src
    cmake -S . -B ../b -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt \
        -DMEDIAGREP_MEDIAINFO_LIBRARY=/opt/lib/libmediainfo.so.0 \
        -DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,/opt/lib
    cmake --build ../b -j"$(nproc)"
    ctest --test-dir ../b --output-on-failure
    echo "== interpreter and libraries"
    /opt/bin/readelf -l ../b/mediagrep 2>/dev/null | grep -i interpreter || true
    ../b/mediagrep --version

    DESTDIR=/build/stage cmake --install ../b --strip
    PATH=/usr/bin:/bin:$PATH sh tools/make-ipk.sh /build/stage "$arch" "$version" /out
    ;;
check)
    install_entware
    # Entware's own packages are installed only as dependencies of the .ipk.
    opkg install /out/mediagrep_*.ipk
    echo "== installed"
    opkg list-installed | grep -E '^(mediagrep|libmediainfo|libzen|libc|libstdcpp) '
    out=$(/opt/bin/mediagrep --version)
    echo "$out"
    case "$out" in *"not found"*) echo "MediaInfoLib was not installed"; exit 1;; esac
    /opt/bin/mediagrep -ri engineering /src/tests/fixtures
    ;;
*)
    echo "unknown mode $mode" >&2
    exit 2
    ;;
esac
