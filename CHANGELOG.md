# Changelog

All notable changes to mediagrep are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions
follow [Semantic Versioning](https://semver.org/).

## [0.1.0] - Unreleased

The first release.

### Searching

- A positional `PATTERN` is a regular expression searched across every
  field of every track, skipping the fields MediaInfoLib derives from the
  path, so a channel name does not match the folder it was saved in.
- `-e EXPR` takes an expression: `and`, `or`, `not`, parentheses, and
  field comparisons with `= != ~ !~ < <= > >=`. Several `-e` are ORed.
- Fields are `[Track[N]/]Name`. A comparison is true if any track in
  scope satisfies it. Numbers, durations (`1h30m`), sizes (`4G`) and
  dates (`>= 2024-03`) compare by value.
- A `File` track from `stat`: name, directory, extension, size, times,
  and on POSIX mode, owner and group.
- A `Path` track recognized from file names (a bare name such as
  `Episode 3.mkv` inside `Season 2/` still sees its folder): dates,
  `S01E02` season and episode, and the yt-dlp video id (the bracketed
  word at the end of the name, not an earlier one; dates and episodes
  are not read inside it).
- Aliases `@when`, `@who` and `@name` that search every field carrying
  that kind of information, reporting the one that matched.
- `--fields FILE` lists every field of a file, to find what to search.

### Like grep

- Output is `path: Track/Field: value` per matching field; `-l` and `-L`
  print paths, `-c` counts, `-0` separates records with NUL, `-v`
  inverts, `-q` exits at the first match.
- Directories are entered only with `-r` or `-R`.
- `-R` skips a symlink cycle with a "recursive directory loop" warning, as
  grep does, instead of descending until the path is too long; a broken
  link is reported and exits 2.
- `-i` ignores case in regexes and equality alike.
- `-v` with `-L` selects the same files for the output, `-c`, `-q`, the
  progress count and the exit status. `--parse-speed=nan` is rejected.
- In a quoted string a backslash escapes only the quote and another
  backslash, so regex escapes such as `"^\d+$"` work as written. A track
  index too large for a number is a parse error instead of wrapping.
- A value that merely starts with a date, such as the file name
  `2024-03-01.mkv` or the title `2024-03-01 Interview`, is no longer
  equal to `2024-03-01`; only real date values compare as dates.
- Regexes run on values of any length with GCC's standard library, which
  used to crash on long values and deeply nested groups. Patterns with
  back-references still skip values over 256 KB, with a warning.
- Exit status 0 when anything was selected, 1 when nothing was, 2 on
  error, including a failure to write the output (checked at the final
  flush, so `> /dev/full` is reported instead of silently losing it).

### Running

- Several files are read at once (`-j`, default 2), with output in a
  stable order.
- `--progress` shows a status line on stderr, automatically on a
  terminal, or periodic log lines when stderr is not a terminal. The
  line stays inside the window after it is resized smaller, and counts
  every East Asian wide character (including emoji such as U+2705) as
  two columns.
- MediaInfoLib is loaded at runtime by default, so any installation of
  it works; `-DMEDIAGREP_LINK_STATIC=ON` links it at build time instead.
- Builds with any C++17 compiler on Linux, macOS, Windows and the BSDs,
  including GCC 8 toolchains without `std::filesystem`.
- A man page, installed by `cmake --install`.
