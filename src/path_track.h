// Recognizers that read what a file name encodes by convention: dates,
// season and episode numbers, a yt-dlp id. They only claim what is
// unambiguous; who-versus-title is left alone. The results form the
// Path track.
#pragma once

#include <string>
#include <vector>

#include "eval.h"

namespace mediagrep {

// Fields recognized in `name` (the file name) and `directory` (its parent
// directory's own name, used only as a fallback for the season):
//   When     "YYYY-MM-DD" or "YYYY": 2024-03-01, 2024_03_01, 2024.03.01,
//            20240301 (plausible month and day), (2019), or a delimited
//            year 1900..2099. The first full date wins over a bare year.
//   Season   from S01E02, 1x02, or a "Season 2" / "S02" parent directory
//   Episode  from S01E02 or 1x02
//   Id       the 11-character [id] yt-dlp appends: the bracketed word at the
//            end of the name, before any extensions. It is masked out before
//            the date and season/episode recognizers run.
// Season and Episode are integers without leading zeros.
std::vector<FieldValue> recognizePath(const std::string& name, const std::string& directory);

}  // namespace mediagrep
