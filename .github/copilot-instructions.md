# Review instructions for mediagrep

mediagrep is a grep-like C++17 command-line tool that searches media-file
metadata through MediaInfoLib. It targets Linux, macOS, Windows, the BSDs,
and NAS toolchains as old as GCC 8 without std::filesystem.

Pull requests into `review/base` are review-only. Each one adds a slice of
the existing code from `main` so that it can be reviewed. They are never
merged. Code that a slice calls but doesn't include is on `main`. The design
is in docs/superpowers/specs/2026-10-16-mediagrep-design.md on `main`.

When reviewing:

- Report correctness problems only: crashes, hangs, data races, wrong
  results, wrong exit status, security or privacy issues, resource leaks,
  portability bugs on the supported platforms, and tests that don't check
  what they claim.
- Give each comment a concrete trigger: an input, file name, platform or
  interleaving, and the wrong result it causes.
- Skip style, naming, formatting and comment wording, and suggestions to
  use std::filesystem or third-party libraries.
- Don't repeat a comment you already made in an earlier round.
