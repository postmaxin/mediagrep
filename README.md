# mediagrep

[![CI](https://github.com/postmaxin/mediagrep/actions/workflows/ci.yml/badge.svg)](https://github.com/postmaxin/mediagrep/actions/workflows/ci.yml)

`grep` for media metadata. Searches the metadata of media files (title,
publisher, duration, resolution, codec, language, ...) the way `grep`
searches their contents, using [MediaInfoLib](https://mediaarea.net/MediaInfo).

```sh
# Every field of every track, case-insensitive, recursively from here
$ mediagrep -ri "interesting engineering" ~/Videos
~/Videos/talk.mkv: General/PUBLISHER: Interesting Engineering

# Field comparisons with boolean operators
$ mediagrep -r -e 'Video/Height < 1000 and not Audio/Language = ja' ~/Videos
~/Videos/old/clip.mp4: Video/Height: 720

# Several expressions are ORed, like grep -e
$ mediagrep -rl -e 'Duration > 1h' -e 'Format = Matroska' ~/Videos
```

Output mirrors grep's `file: line` form, with the matching field standing
in for the line. `-l` prints only paths.

## Why

A shell loop around `mediainfo --Inform` pays process startup and library
load once per file, parses more of each file than needed, and runs
serially. mediagrep calls the library in-process, parses headers only by
default, and keeps a couple of files in flight at once, which makes it an
order of magnitude faster on a large collection.

## Install

Prebuilt archives for Windows x64 and Linux x86_64 are attached to each
[release](https://github.com/postmaxin/mediagrep/releases). The Windows
one includes `MediaInfo.dll`; on Linux, install the library as below.
The Linux binary needs glibc 2.34 or later (Debian 12, Ubuntu 22.04,
RHEL 9 and newer); on older systems, build from source.

mediagrep needs MediaInfoLib at runtime. By default it loads the shared
library when it starts, so any MediaInfo installation works:

| Platform | Get the library | Build |
|---|---|---|
| Debian, Ubuntu | `apt install libmediainfo0v5` (`libmediainfo-dev` to link at build time) | `cmake -S . -B build && cmake --build build` |
| Fedora | `dnf install libmediainfo` | same |
| macOS | `brew install media-info` | same |
| Windows | [MediaInfo DLL package](https://mediaarea.net/en/MediaInfo/Download/Windows); put `MediaInfo.dll` on `PATH` or next to `mediagrep.exe` | Visual Studio 2019+ with CMake |
| FreeBSD, OpenBSD | `multimedia/libmediainfo` port | same |

Requirements: a C++17 compiler and CMake 3.16+. No other dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build        # optional
cmake --install build         # installs bin/mediagrep and its man page
```

To link MediaInfoLib at build time instead of loading it at runtime
(for packagers), install the development package and configure with
`-DMEDIAGREP_LINK_STATIC=ON`. This uses `pkg-config` to find `libmediainfo`.

If the library lives somewhere the dynamic loader does not search,
point the build at it with `-DMEDIAGREP_MEDIAINFO_LIBRARY=/path/to/libmediainfo.so.0`.
On macOS the build looks in Homebrew's `/opt/homebrew/lib` and
`/usr/local/lib` automatically.

## Usage

```
mediagrep [OPTIONS] -e EXPR [-e EXPR ...] PATH...
mediagrep [OPTIONS] PATTERN PATH...
mediagrep --fields FILE
```

At least one `PATH` is required. As with grep, directories are only
entered with `-r` or `-R`; otherwise a directory argument is reported
as `Is a directory` and skipped, so `mediagrep PATTERN *` searches
the files in front of you and nothing deeper. Files the library does
not recognize as media are skipped
silently, so sidecars and text files in a collection are fine. Images
are media to MediaInfoLib, so use `--exclude` to skip them when needed.

On Unix, MediaInfoLib opens files through the C library's locale
conversion, so a UTF-8 locale must exist on the system for file names
with non-ASCII characters. mediagrep selects one itself (the
environment's, then `C.UTF-8`, then `en_US.UTF-8`) and warns if none
is available.

| Option | Meaning |
|---|---|
| `-e EXPR` | Expression. Repeatable; ORed together. |
| `-i` | Case-insensitive matching, for regexes and equality alike. |
| `-v` | Select files that do not match. |
| `-l` | Print only matching paths. |
| `-L` | Print only non-matching paths. |
| `-c` | Print a count of matching files per `PATH`. |
| `-0` | End records with NUL instead of newline, for `xargs -0`. |
| `-r` | Recurse into directories, not following symlinks. |
| `-R` | Recurse and follow symlinks. |
| `--include GLOB` | Only files whose name matches. Repeatable. |
| `--exclude GLOB` | Skip files whose name matches. Repeatable. |
| `-j N` | Worker threads. Default: the smaller of 2 and the CPU count. |
| `--parse-speed F` | MediaInfoLib ParseSpeed, 0 to 1. Default 0 reads headers only; raise it for frame-accurate fields. |
| `--fields FILE` | List every field of one file and exit. The answer to "what is the field called". |
| `-q` | No output, exit status only. |
| `-s` | Suppress warnings about unreadable files. |
| `--progress` | Show progress on stderr. The default when stderr is a terminal and `-q` is not given. Without a terminal, a plain line every 10 seconds and a summary at the end, for logs. |
| `--no-progress` | Never show progress. |

The progress line counts files as they are discovered (`120/340+`) and
switches to a percentage, bar and time remaining once the walk has found
everything. It appears only after half a second, so quick searches stay
clean, and it is cleared before each match is printed, so it never mixes
with output even when stdout is the same terminal.

Exit status is 0 if any file was selected, 1 if none, 2 on error, including a
failure to write the output (a full disk, say), which is reported as `write error`.

### Patterns

`PATTERN`, and a quoted string inside an expression, is a regex
(ECMAScript syntax) searched against every field of every track,
except the fields MediaInfoLib derives from the path (`CompleteName`,
`FolderName`, `FileName` and friends), so a search for a channel name
is not satisfied by the folder it was downloaded into. It is not parsed
as an expression, so regex metacharacters never collide with the
expression grammar. Comparisons and boolean operators require `-e`.

Regexes use the C++ standard library. GCC's implementation needs stack
in proportion to the length of the text for patterns like `a.*b`, so
there mediagrep does not apply a regex to values longer than 256 KB, and
says so on stderr when it skips one. Such values are rare outside
embedded documents. `-i` folds ASCII letters only.

### Expressions

```
expr   := or
or     := and ( "or" and )*
and    := not ( "and" not )*
not    := "not" not | atom
atom   := "(" expr ")" | "pattern" | field | field OP value
field  := [ Track [ "[" N "]" ] "/" ] Name
OP     := = | != | ~ | !~ | < | <= | > | >=
```

- **Tracks** are `General`, `Video`, `Audio`, `Text`, `Image`, `Menu`,
  `Other`. `Audio/Language` looks in every audio track; `Audio[2]/Language`
  in the second (1-based, as mediainfo numbers them); plain `Language` in
  every track of every kind. MediaInfo's own parameter names can contain
  slashes (`Duration/String`); only a leading track name is split off.
- **A comparison is true if any track in scope satisfies it.** So
  `not Audio/Language = ja` means "no audio track is Japanese".
- **A bare field** (`Audio/Language`) tests that the field exists.
- **Numbers.** Fields the library reports as numeric compare numerically.
  Durations accept `1h30m`, `90m`, `45s`, `500ms`; a bare number is
  seconds. Sizes and bitrates accept `K`, `M`, `G`, `T` (decimal,
  `1.5G` or `1.5GB`); a bare number is bytes or bits per second.
- **Dates.** Date-shaped values (`Encoded_Date`, `Recorded_Date`)
  compare chronologically at the operand's granularity: `= 2024`,
  `>= 2024-03`, `< "2024-03-01 12:00"`.
- **Text.** `=` and `!=` are exact matches, `~` and `!~` unanchored
  regexes, `<` and `>` lexical. Case-sensitive unless `-i`.
- **Values** with spaces are quoted with `"` or `'`; a backslash escapes
  the quote. Quote the whole expression for your shell.

```sh
mediagrep -r -e 'Duration > 1h30m and Video/Height >= 1080' .
mediagrep -r -e 'Audio/Language = ja and not Text/Language = en' .
mediagrep -r -e '(Title ~ "engine" or Publisher ~ "engine") and Encoded_Date >= 2024' .
mediagrep -r -e 'FileSize > 4G' --include '*.mkv' /mnt/media
mediagrep -rc -e 'not Audio/Language' ~/Music   # files with no language tag
```

### The File track

Alongside MediaInfoLib's `General`, `Video`, `Audio`, `Text`, `Image`,
`Menu` and `Other` tracks, mediagrep adds a `File` track from `stat`:
`Name`, `Directory`, `Extension`, `Size`, `Modified`, `Created` (where
the OS records a birth time), `Accessed`, and on POSIX `Mode`, `Owner`,
`Group`. Times are UTC, matching MediaInfoLib's own dates.

```sh
mediagrep -rl -e 'File/Size > 4G' .
mediagrep -rl -e 'File/Modified >= 2026-01 and not Audio/Language' .
```

### The Path track

File names encode things by convention, and a collection usually mixes
several conventions, so mediagrep recognizes only what is unambiguous
wherever it appears and puts it in a `Path` track:

| Field | Recognized from |
|---|---|
| `Path/When` | `2024-03-01` (also with `_` or `.`), `20240301` with a plausible month and day, `(2019)`, or a delimited year 1900 to 2099. A full date wins over a bare year. |
| `Path/Season`, `Path/Episode` | `S01E02` or `1x02`; the season also from a `Season 2` or `S02` parent directory |
| `Path/Id` | the eleven-character `[id]` yt-dlp appends |

Delimited means `1080p`, `2160p` and the inside of an id never count.
Who-versus-title is deliberately not guessed: `Artist - Title` and
`Title - Subtitle` look the same.

### Aliases

A few `@names` stand for "any field that carries this kind of
information". They expand to an OR over concrete fields, and the output
shows which one matched. The sigil keeps them out of MediaInfoLib's
namespace.

| Alias | Looks in |
|---|---|
| `@when` | File/Modified, File/Created, Encoded_Date, Tagged_Date, Recorded_Date, Released_Date, Original/Released_Date, Mastered_Date, DATE, Path/When |
| `@who` | Performer, Artist, Publisher, Director, Composer, Author, Channel |
| `@name` | File/Name, Title, Movie, Track, Album |

```sh
mediagrep -r -e '@when = 2024' .           # anything dated 2024, by any measure
mediagrep -ril -e '@who ~ engineering' .   # channel, artist, publisher, ...
mediagrep -rc -e 'not @when' .             # files with no date at all
```

Field names are case-insensitive. To see what a file actually carries:

```sh
$ mediagrep --fields talk.mkv
General/Format: Matroska
General/Duration: 2023
...
Audio[2]/Language: ja
```

## Development

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build && ./build/mediagrep_tests
```

Tests use [doctest](https://github.com/doctest/doctest) (vendored). The
parser and evaluator are tested against an in-memory metadata source;
the MediaInfoLib backend and the CLI run against small fixture files in
`tests/fixtures`, regenerable with `tests/fixtures/make.sh` and ffmpeg.
CI builds and tests on Linux, macOS, and Windows, with both the
runtime-loaded and build-time-linked library.

See [CONTRIBUTING.md](CONTRIBUTING.md) for how to report problems and
send changes, and [CHANGELOG.md](CHANGELOG.md) for what changed when.
The design is written up in
[docs/superpowers/specs/2026-10-16-mediagrep-design.md](docs/superpowers/specs/2026-10-16-mediagrep-design.md).

## License

BSD-2-Clause, matching MediaInfo, so that this can be contributed
upstream if MediaArea wants it. See [LICENSE](LICENSE). The vendored
`MediaInfoDLL.h` and `MediaInfoDLL_Static.h` are copyright MediaArea.net
SARL under the same license; `doctest.h` is MIT.
