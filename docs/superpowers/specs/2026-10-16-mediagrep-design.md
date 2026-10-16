# mediagrep design

Date: 2026-10-16
Status: approved for planning

## Purpose

`mediagrep` searches the metadata of media files the way `grep` searches
their contents. Given a pattern or a boolean expression over metadata
fields, it walks directories, parses each file with MediaInfoLib, and
prints the paths that match.

Motivating case: find every video in a collection published by a given
channel, regardless of filename:

    mediagrep -i "interesting engineering" ~/Videos

Richer queries scope to fields and tracks and combine with boolean
operators:

    mediagrep -e 'Video/Height < 1000 and not Audio/Language = ja' ~/Videos
    mediagrep -e 'Duration > 1h or Format = Matroska' ~/Videos

Today this is done with a shell loop around `mediainfo --Inform`, which
pays process startup and library load once per file, parses more of each
file than needed, and runs serially. `mediagrep` calls the library
in-process, parses headers only by default, and runs files in parallel.

## Goals and non-goals

Goals:

- Work on Windows, Linux, and macOS from one CMake build, and on other
  UNIX-like systems with a system `libmediainfo`.
- Be contributable to MediaArea: BSD-2-Clause, C++17, no dependencies
  beyond MediaInfoLib and the standard library, no parser generator.
- Be an order of magnitude faster than a `mediainfo` shell loop on a
  large collection.
- Keep grep's conventions where they transfer, and never reuse a grep
  flag with a different meaning.

Non-goals for v1:

- A metadata cache or index across runs.
- Editing metadata.
- Output formats other than paths and field lines (JSON can come later).
- PCRE or full Unicode case folding. `std::regex` is isolated so it can
  be replaced.

## Prior art

- MediaConch (MediaArea) policies: rules of track type, field, operator,
  value, and occurrence, nested in and/or policies. Same data model as
  this tool; operators `= != < <= > >=` and "exists". XML-based, so the
  parser is not reusable.
- ExifTool `-if`: Perl expression over tags; repeated `-if` ANDs; missing
  tag is false; no all-tag search.
- beets queries: `field:value`, `::` regex, `..` ranges, leading `:` for
  all fields. Ergonomic but no NOT or grouping.

## Architecture

Five units. Each has one job, a narrow interface, and its own tests.

```
   argv --> CLI --> Walker --> [worker threads] --> Printer
                       |              |
                       |     MediaInfo backend (one instance per thread)
                       |              |
                       |     Evaluator(Expr, MetadataSource) -> Match
                       |
                  Expression parser (text -> Expr), run once in CLI
```

### Expression parser

Input: UTF-8 string. Output: an `Expr` tree or a `ParseError` with a
byte offset and message. Hand-written recursive descent, no library
dependency. Pure: no I/O, no MediaInfo.

### Evaluator

Input: `Expr`, a `MetadataSource`, and options (case-insensitive flag).
Output: a `Match` holding a boolean plus the list of `(track, field,
value)` triples that satisfied comparisons, for `-o` output.

`MetadataSource` is the seam between logic and library:

```cpp
struct MetadataSource {
    virtual size_t trackCount(TrackKind) const = 0;
    virtual std::optional<std::string> get(TrackKind, size_t index,
                                           std::string_view field) const = 0;
    virtual ValueType type(TrackKind, size_t index,
                           std::string_view field) const = 0;
    // enumerate (field, value) pairs of a track, for bare patterns and --fields
    virtual void forEachField(TrackKind, size_t index,
                              const std::function<void(std::string_view, std::string_view)>&) const = 0;
};
```

`ValueType` is Text, Integer, Float, Date, or Binary, taken from
MediaInfoLib's `InfoOption_TypeOfValue`.

Unit tests use an in-memory fake `MetadataSource`.

### MediaInfo backend

Implements `MetadataSource` over the `MediaInfoDLL` C++ API. The same
source compiles against either header:

- `MediaInfoDLL.h` (default): loads `libmediainfo.so.0`,
  `libmediainfo.0.dylib`, or `MediaInfo.dll` at runtime. Users need only
  a MediaInfo installation.
- `MediaInfoDLL_Static.h` (CMake option `MEDIAGREP_LINK_STATIC`): links
  MediaInfoLib at build time. For packagers and upstream.

Internal strings are UTF-8 `std::string`. On Windows the library is used
in `UNICODE` mode and a single shim converts UTF-8 to and from UTF-16 at
the boundary. The 8-bit library API is deprecated upstream and not used.

Each worker thread owns one `MediaInfo` instance, reused across files.
Options set on it: `ParseSpeed` from `--parse-speed` (default 0),
`ReadByHuman=0` so values come back raw (milliseconds, bytes, bps).

Field lookup is case-insensitive. The backend enumerates a track's
fields through `Count_Get` plus `Get` by index with `Info_Name`.

### Walker

Expands the path arguments with `std::filesystem`. Directories recurse
(`-r`, the default); `-R` also follows symlinks. `--include` and
`--exclude` globs filter by filename. Files are numbered in discovery
order, dispatched to a queue served by `-j` workers (default
`hardware_concurrency`), and results are handed to the printer in
sequence number order, so output is deterministic regardless of thread
timing.

### CLI

Argument parsing by hand (no dependency), output, exit codes.

## Expression language

### Grammar

```
expr       := or_expr
or_expr    := and_expr ( "or" and_expr )*
and_expr   := not_expr ( "and" not_expr )*
not_expr   := "not" not_expr | atom
atom       := "(" expr ")"
            | STRING                       -- bare pattern, any text field
            | field                        -- exists test
            | field OP value
field      := [ TRACK [ "[" INDEX "]" ] "/" ] NAME
TRACK      := General | Video | Audio | Text | Image | Menu | Other
OP         := "=" | "!=" | "~" | "!~" | "<" | "<=" | ">" | ">="
value      := STRING | WORD
STRING     := "..." | '...'      (backslash escapes the quote)
WORD       := run of characters not whitespace, quotes, or parentheses
```

Keywords `and`, `or`, `not` are case-insensitive. `and` binds tighter
than `or`. Track and field names are case-insensitive. `INDEX` is
1-based, matching mediainfo's own numbering.

### Semantics

- **Track scope.** A field without a track prefix is looked up in every
  track of every kind. With a prefix and no index, in every track of
  that kind. With an index, in that one track.
- **Any-track rule.** A comparison is true if any in-scope track
  satisfies it. `Audio/Language = ja` means "some audio track is
  Japanese". `not Audio/Language = ja` means "no audio track is".
- **Missing field.** A comparison against a missing field is false. The
  exists test is true when the field is present and non-empty.
- **Typed comparison.** The field's declared type picks the comparison:
  - Integer, Float: numeric. The value is parsed as a number with an
    optional unit (below). If the value does not parse as a number, the
    comparison is false and a warning is printed once on stderr.
  - Date: chronological. Values accept `YYYY`, `YYYY-MM-DD`, or
    `YYYY-MM-DD HH:MM:SS`. MediaInfo dates are parsed from their raw
    form (`UTC 2024-03-01 12:00:00`).
  - Text: `=` and `!=` are case-insensitive exact matches. `<` and `>`
    compare lexically. `~` and `!~` are unanchored regex searches.
  - Binary: never matches; skipped by bare patterns.
- **Units.** Applied only to fields whose raw unit is known:
  - Duration-like fields (raw milliseconds): `h`, `m`, `s`, `ms`
    suffixes, combinable (`1h30m`). Bare number means seconds.
  - Size and bitrate fields (raw bytes or bits per second): `K`, `M`,
    `G`, `T` decimal suffixes. Bare number is the raw unit.
  - All other numeric fields: bare number only.
- **Regex.** ECMAScript syntax via `std::regex`. `-i` makes `~`, `!~`,
  and bare patterns case-insensitive. Equality is always
  case-insensitive on text.
- **Bare pattern.** A STRING atom, or the positional `PATTERN`, is a
  regex searched against every Text-typed field value in every track.
  Matches record which field matched, for `-o`.
- **Multiple `-e`.** ORed, as in grep. `-v` inverts the whole result.

### Field discovery

`mediagrep --fields FILE` prints one `Track[/N]/Field = value` line per
field of one file and exits. This is the answer to "what is the field
called".

## Command line

```
mediagrep [OPTIONS] -e EXPR [-e EXPR ...] PATH...
mediagrep [OPTIONS] PATTERN PATH...
mediagrep --fields FILE
```

The second form is grep's: when no `-e` is given, the first positional
argument is a bare pattern, that is a regex searched across all text
fields, exactly as if it were a quoted STRING atom. It is not parsed as
an expression, so regex metacharacters such as parentheses never collide
with the grammar. Field comparisons and boolean operators require `-e`.
`--` ends option parsing.

| Option | Meaning |
|---|---|
| `-e EXPR` | Expression. Repeatable; ORed together. |
| `-i` | Case-insensitive regex matching. |
| `-v` | Print files that do not match. |
| `-l` | Print matching paths (default). |
| `-L` | Print non-matching paths. |
| `-c` | Print a count of matching files per PATH argument. |
| `-o` | After each matching path, print each `Track/Field = value` that satisfied a comparison, indented. |
| `-0` | NUL-terminate paths instead of newline. |
| `-r` | Recurse into directories, do not follow symlinks (default). |
| `-R` | Recurse and follow symlinks. |
| `--include GLOB` | Only files whose name matches. Repeatable. |
| `--exclude GLOB` | Skip files whose name matches. Repeatable. |
| `-j N` | Worker threads. Default: hardware concurrency. |
| `--parse-speed F` | MediaInfoLib ParseSpeed, 0 to 1. Default 0. |
| `--fields FILE` | Dump all fields of one file and exit. |
| `-q` | Quiet: no output, exit status only. |
| `-s` | Suppress warnings about unreadable files. |
| `--version`, `--help` | The usual. |

Flags borrowed from grep keep grep's meaning. `-o` is the one stretch:
grep prints the matched text, we print the matched field.

## Output

Default: one path per matching file, in discovery order. Paths are
printed as given on the command line joined with the relative part
found by the walker, like grep.

`-o` example:

```
videos/talk.mkv
    General/Publisher = Interesting Engineering
    Video/Height = 720
```

## Errors and exit status

- Exit 0 if at least one file matched (or, with `-v`/`-L`, was printed),
  1 if none did, 2 if any error occurred.
- A file the library cannot open or read: warning on stderr naming the
  file, processing continues, final exit status 2. `-s` hides the
  warning but not the status.
- A file the library opens but reports no format for (sidecar, image
  thumbnail, text): skipped silently.
- Expression parse error: message with byte offset and a caret line,
  exit 2 before any file is read.
- Unknown field name: not an error (fields vary by format). The
  comparison is false.
- Library not found at runtime (dynamic build): one clear message naming
  the library file looked for, exit 2.

## Testing

- **Parser:** doctest cases from expression text to expected tree, and
  from bad text to expected error offset.
- **Evaluator:** doctest cases against a fake `MetadataSource` covering
  every operator, each value type, the any-track rule, missing fields,
  units, and `-o` match reporting.
- **Backend and CLI:** end-to-end tests over committed fixture files in
  `tests/fixtures/`: a handful of media files under 50 KB each with
  known tags, generated by `tests/fixtures/make.sh` using ffmpeg. The
  script is committed so fixtures are reproducible; ffmpeg is not needed
  to run the tests.
- **CI:** GitHub Actions matrix on ubuntu-latest, macos-latest, and
  windows-latest. libmediainfo from apt, Homebrew (`media-info`), and
  vcpkg respectively. Each job builds, runs CTest, and runs the CLI
  against the fixtures. One Linux job also builds with
  `MEDIAGREP_LINK_STATIC=ON`.

## Repository layout

```
CMakeLists.txt
cmake/            FindMediaInfoLib.cmake
src/
  main.cpp        CLI
  parser.{h,cpp}  expression parser
  expr.h          Expr tree types
  eval.{h,cpp}    evaluator, MetadataSource interface
  mediainfo_source.{h,cpp}
  walker.{h,cpp}
  units.{h,cpp}   unit and date parsing
  utf.{h,cpp}     UTF-8/UTF-16 shim (Windows)
tests/
  doctest.h       vendored
  test_parser.cpp
  test_eval.cpp
  test_cli.cmake
  fixtures/
third_party/MediaInfoDLL/   vendored MediaInfoDLL.h, MediaInfoDLL_Static.h (BSD)
docs/
.github/workflows/ci.yml
```

The two MediaInfoDLL headers are vendored because the dynamic-load build
should not require the dev package at all.

## Open points deferred past v1

- `..` range sugar from beets.
- JSON output.
- A metadata cache keyed on path, size, and mtime.
- PCRE2 for Unicode-aware case folding.
