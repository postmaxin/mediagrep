# Draft pull request for Entware/rtndev (not yet submitted)

Target repository: https://github.com/Entware/rtndev (branch `master`),
the feed for packages that never existed in OpenWrt. Package directory:
`mediagrep/` containing the `Makefile` next to this file.

Commit (one commit, author and Signed-off-by must be the same real
name and email; use `git commit -s`):

```
mediagrep: add new package

grep for media file metadata, built on MediaInfoLib. Depends on
libmediainfo, which is already in this feed.

Signed-off-by: Tyler MacDonald <github@crackerjack.net>
```

## PR title

```
mediagrep: add new package (0.1.0)
```

## PR body

```
Adds `mediagrep` 0.1.0, a `grep` for media metadata: it searches tags,
codecs, resolution, duration and so on of audio, video and image files,
recursively, with field comparisons and boolean operators. Upstream:
https://github.com/postmaxin/mediagrep (BSD-2-Clause, C++17, CMake, I am
the author and maintainer).

- New package, so rtndev rather than the OpenWrt-derived feeds.
- Source is the tagged release tarball, with `PKG_HASH`.
- Uses `cmake.mk`; tests off. `DEPENDS:=+libmediainfo +libstdcpp`.
- MediaInfoLib is loaded with `dlopen()` at run time, by the absolute
  path `/opt/lib/libmediainfo.so.0` (`-DMEDIAGREP_MEDIAINFO_LIBRARY`), so
  there is no link-time dependency on it. Entware's `libmediainfo`
  provides that file.
- Ships `/opt/bin/mediagrep`, its man page, and the license notices
  (upstream's LICENSE and MediaInfoLib's BSD notice, because the
  MediaInfoDLL headers are compiled in).
- Packaging for Entware is already built by upstream CI as a hand-made
  `.ipk` for x64, aarch64 and armv7; this PR is so that
  `opkg install mediagrep` works from the feed instead.

Compile tested: x64-k3.2 (Entware generic installer, native build with
Entware's gcc, cmake, glibc and libmediainfo) with the
same CMake options; and the `Makefile` options and install layout
reproduced with plain cmake with `DESTDIR` staging. <FILL IN: result of
the buildroot build if Tyler or a reviewer runs one.>

Run tested: x64-k3.2, in a clean Entware: `opkg install` of the upstream
`.ipk`, `mediagrep --version` prints the MediaInfoLib version, and
`mediagrep -ri engineering tests/fixtures` finds the expected files.
Upstream's test suite passes in that build.
```
