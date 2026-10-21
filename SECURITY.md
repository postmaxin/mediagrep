# Security

## Reporting a vulnerability

Please report security problems privately, through GitHub's
[private vulnerability reporting](https://github.com/postmaxin/mediagrep/security/advisories/new),
not in a public issue. Include what you ran, what happened, and the
platform. You should hear back within a week.

## What counts

mediagrep reads files that may come from anyone, so problems in how it
handles them are in scope: crashes or hangs on crafted file names, paths,
expressions or metadata, and anything that lets a file or directory make
mediagrep run code. That includes how it finds and loads MediaInfoLib
(see the Install section of the README).

Bugs in MediaInfoLib itself belong upstream, at
[MediaArea/MediaInfoLib](https://github.com/MediaArea/MediaInfoLib).

## Supported versions

Fixes go into the latest release.
