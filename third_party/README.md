# Third-party code

Vendored unmodified, each with its upstream license file beside it.

| Directory | What | Upstream | License |
|---|---|---|---|
| `MediaInfoDLL/` | `MediaInfoDLL.h` and `MediaInfoDLL_Static.h`, MediaInfoLib's C++ wrapper headers | [MediaArea/MediaInfoLib](https://github.com/MediaArea/MediaInfoLib), `Source/MediaInfoDLL/` | BSD-2-Clause, [`License.html`](MediaInfoDLL/License.html) (from tag v26.10) |
| `doctest/` | `doctest.h` 2.4.11, the test framework (tests only, not in the mediagrep binary) | [doctest/doctest](https://github.com/doctest/doctest) | MIT, [`LICENSE.txt`](doctest/LICENSE.txt) |

The MediaInfoDLL headers are compiled into the mediagrep binary, so binary
distributions must carry MediaInfoLib's notice. The release archives
include it as `MediaInfoLib-License.html`, and `cmake --install` puts it in
the documentation directory.
