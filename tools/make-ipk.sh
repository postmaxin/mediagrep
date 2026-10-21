#!/bin/sh
# Pack an opkg .ipk (Entware, OpenWrt) from a staging tree.
#
#   tools/make-ipk.sh STAGING_DIR ARCH VERSION OUT_DIR
#
# STAGING_DIR is the result of `DESTDIR=STAGING_DIR cmake --install build
# --strip`, configured with -DCMAKE_INSTALL_PREFIX=/opt, so it holds
# opt/bin/mediagrep and so on. ARCH is opkg's architecture name for the
# target, as `opkg print-architecture` shows it (x64-3.2, aarch64-3.10,
# armv7-3.2). VERSION is the package version, such as 0.1.0-1.
#
# Writes OUT_DIR/mediagrep_VERSION_ARCH.ipk: a gzipped tar of ./debian-binary,
# ./control.tar.gz and ./data.tar.gz, which is the layout opkg reads.
# Needs POSIX sh and GNU tar.
set -eu

if [ $# -ne 4 ]; then
    echo "usage: $0 STAGING_DIR ARCH VERSION OUT_DIR" >&2
    exit 2
fi
stage=$1 arch=$2 version=$3 out=$4

[ -x "$stage/opt/bin/mediagrep" ] || {
    echo "$0: $stage/opt/bin/mediagrep is missing; install with DESTDIR first" >&2
    exit 1
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$out"
out=$(cd "$out" && pwd)

# Fixed, so the same input gives the same package.
tarflags="--owner=0 --group=0 --numeric-owner"
if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
    tarflags="$tarflags --mtime=@$SOURCE_DATE_EPOCH"
fi

size=$(du -sk "$stage" | cut -f1)

mkdir "$work/control"
cat > "$work/control/control" <<EOF
Package: mediagrep
Version: $version
Architecture: $arch
Maintainer: Tyler MacDonald <postmaxin@users.noreply.github.com>
Section: utils
Priority: optional
License: BSD-2-Clause
Source: https://github.com/postmaxin/mediagrep
Depends: libc, libstdcpp, libmediainfo
Installed-Size: $size
Description: grep for media file metadata
 mediagrep searches the metadata of audio and video files (codecs, bit
 rates, durations, tags, and what the file name says) with a regular
 expression or a field expression. It reads the files with MediaInfoLib.
EOF

printf '2.0\n' > "$work/debian-binary"
# shellcheck disable=SC2086  # $tarflags is a list of flags
(cd "$work/control" && tar $tarflags --sort=name -czf "$work/control.tar.gz" .)
# shellcheck disable=SC2086
(cd "$stage" && tar $tarflags --sort=name -czf "$work/data.tar.gz" .)

ipk="$out/mediagrep_${version}_${arch}.ipk"
# The members go in this order: opkg expects debian-binary first.
# shellcheck disable=SC2086
(cd "$work" && tar $tarflags -czf "$ipk" ./debian-binary ./control.tar.gz ./data.tar.gz)
echo "$ipk"
