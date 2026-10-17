# mediagrep design

Date: 2026-10-16
Status: implemented (v1); this document is the design of record

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
value)` triples that satisfied comparisons, for the default output.

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
order, dispatched to a queue served by `-j` workers (default: the
smaller of 2 and `hardware_concurrency`, because the bottleneck is disk
or network seeks and more workers mostly add contention), and results
are handed to the printer in
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
    comparison is false and a warning is printed once on stderr. A
    Text-typed field is also treated as numeric when the library
    attaches a unit of measure to it and its value is a number, because
    MediaInfoLib declares some numeric parameters (FileSize) as text.
  - Date: chronological, at the operand's granularity, by comparing a
    normalized `YYYY-MM-DD HH:MM:SS` form as a prefix. Operands accept
    `YYYY`, `YYYY-MM`, `YYYY-MM-DD`, `YYYY-MM-DD HH:MM[:SS]`. MediaInfo
    declares its dates as text, so this applies whenever the field's
    value is date-shaped (`UTC 2024-03-01 12:00:00`) and the operand is
    too; otherwise the comparison is plain text.
  - Text: `=` and `!=` are exact matches. `<` and `>` compare
    lexically. `~` and `!~` are unanchored regex searches.
  - Binary: never matches; skipped by bare patterns.
- **Units.** Applied only to fields whose raw unit is known:
  - Duration-like fields (raw milliseconds): `h`, `m`, `s`, `ms`
    suffixes, combinable (`1h30m`). Bare number means seconds.
  - Size and bitrate fields (raw bytes or bits per second): `K`, `M`,
    `G`, `T` decimal suffixes. Bare number is the raw unit.
  - All other numeric fields: bare number only.
- **Regex.** ECMAScript syntax via `std::regex`.
- **Case.** As in grep, matching is case-sensitive unless `-i` is given.
  `-i` applies to every string comparison anywhere: `=`, `!=`, `~`,
  `!~`, and bare patterns, in `-e` expressions and the positional
  pattern alike. Field and track names are case-insensitive regardless.
- **Bare pattern.** A STRING atom, or the positional `PATTERN`, is a
  regex searched against every non-binary field value in every track,
  numeric fields included. Matches record which field matched, for the
  default output.
- **Multiple `-e`.** ORed, as in grep. `-v` inverts the whole result.

### Field discovery

`mediagrep --fields FILE` prints one `Track[N]/Field: value` line per
field of one file and exits, in the same form as the default output. This is the answer to "what is the field
called".

## Command line

```
mediagrep [OPTIONS] -e EXPR [-e EXPR ...] PATH...
mediagrep [OPTIONS] PATTERN PATH...
mediagrep --fields FILE
```

With no `PATH`, the current directory is searched, as GNU grep does
with `-r`. The second form is grep's: when no `-e` is given, the first
positional argument is a bare pattern, that is a regex searched across all text
fields, exactly as if it were a quoted STRING atom. It is not parsed as
an expression, so regex metacharacters such as parentheses never collide
with the grammar. Field comparisons and boolean operators require `-e`.
`--` ends option parsing.

| Option | Meaning |
|---|---|
| `-e EXPR` | Expression. Repeatable; ORed together. |
| `-i` | Case-insensitive string matching, regex and equality alike. |
| `-v` | Print files that do not match. |
| `-l` | Print only matching paths, one per file, instead of the default field lines. |
| `-L` | Print only non-matching paths. |
| `-c` | Print a count of matching files per PATH argument. |
| `-0` | NUL-terminate output records instead of newline. |
| `-r` | Recurse into directories, do not follow symlinks (default). |
| `-R` | Recurse and follow symlinks. |
| `--include GLOB` | Only files whose name matches. Repeatable. |
| `--exclude GLOB` | Skip files whose name matches. Repeatable. |
| `-j N` | Worker threads. Default: min(2, hardware concurrency). |
| `--parse-speed F` | MediaInfoLib ParseSpeed, 0 to 1. Default 0. |
| `--fields FILE` | Dump all fields of one file and exit. |
| `-q` | Quiet: no output, exit status only. |
| `-s` | Suppress warnings about unreadable files. |
| `--version`, `--help` | The usual. |

Flags borrowed from grep keep grep's meaning.

## Output

Default output mirrors grep's `file: line` form, with the matching
field standing in for the line. One line per `(track, field, value)`
triple that satisfied a comparison, in discovery order of files and
track order within a file:

```
videos/talk.mkv: General/Publisher: Interesting Engineering
videos/talk.mkv: Video/Height: 720
```

Track names carry a 1-based index when the file has more than one
track of that kind, as in `Audio[2]/Language`. Field names are printed
as the library spells them; Matroska tags it does not map keep their
uppercase names (`General/PUBLISHER`). A file that matches
without producing any triples, for instance via `not Audio/Language =
ja`, prints its path alone. `-v` and `-L` print paths alone, since a
non-match has no matching field.

`-l` collapses this to one path per matching file. Paths are printed as
given on the command line joined with the relative part found by the
walker, like grep.

## Errors and exit status

- Exit 0 if at least one file matched (or, with `-v`/`-L`, was printed),
  1 if none did, 2 if any error occurred.
- A file the library cannot open or read: warning on stderr naming the
  file, processing continues, final exit status 2. `-s` hides the
  warning but not the status.
- A file the library opens but reports no format for (sidecar, text):
  skipped silently.
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
  units, case sensitivity, and match triple reporting.
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
  utf.{h,cpp}     UTF-8 <-> wchar_t shim (UTF-16 on Windows, UTF-32 elsewhere)
  pool.h          OrderedPool: parallel workers, in-order results
  cli.{h,cpp}     option parsing and output formatting
tests/
  doctest.h       vendored
  test_parser.cpp
  test_eval.cpp
  test_cli.cpp    runs the built binary against the fixtures
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
