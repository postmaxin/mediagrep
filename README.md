# mediagrep

`grep` for media metadata. Searches the metadata of media files (title,
publisher, duration, resolution, codec, language, ...) the way `grep`
searches their contents, using [MediaInfoLib](https://mediaarea.net/MediaInfo).

```sh
# any field, any track, case-insensitive
mediagrep -i "interesting engineering" ~/Videos

# field-scoped boolean expressions
mediagrep -e 'Video/Height < 1000 and not Audio/Language = ja' ~/Videos
mediagrep -e 'Duration > 1h or Format = Matroska' ~/Videos
```

Status: design stage. Nothing to build yet.

## License

BSD-2-Clause, matching MediaInfo, so that this can be contributed
upstream if MediaArea wants it. See [LICENSE](LICENSE).
