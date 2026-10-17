#!/bin/sh
# Regenerates the test fixtures. Requires ffmpeg (set FFMPEG to its path).
# The committed files are what the tests run against; this script exists
# so they can be reproduced and extended. Byte-exact output is not needed,
# only the tags, dimensions, track layout and durations.
set -e
cd "$(dirname "$0")"
FFMPEG="${FFMPEG:-ffmpeg}"
common="-hide_banner -loglevel error -y"

# talk.mkv: 2 s, 32x32 video, two audio tracks (en, ja), container tags.
$FFMPEG $common \
  -f lavfi -i "color=c=black:s=32x32:r=10:d=2" \
  -f lavfi -i "sine=frequency=440:duration=2" \
  -f lavfi -i "sine=frequency=880:duration=2" \
  -map 0:v -map 1:a -map 2:a \
  -c:v libx264 -preset ultrafast -crf 51 -pix_fmt yuv420p \
  -c:a aac -b:a 16k \
  -metadata title="Talk about Engines" \
  -metadata publisher="Interesting Engineering" \
  -metadata description="A short talk." \
  -metadata:s:a:0 language=eng \
  -metadata:s:a:1 language=jpn \
  talk.mkv

# clip.mp4: 1 s, 1280x720 video, one audio track, title only.
$FFMPEG $common \
  -f lavfi -i "color=c=black:s=1280x720:r=10:d=1" \
  -f lavfi -i "sine=frequency=440:duration=1" \
  -c:v libx264 -preset ultrafast -crf 51 -pix_fmt yuv420p \
  -c:a aac -b:a 16k -movflags +faststart \
  -metadata title="Sixty Minutes of Nothing" \
  -metadata:s:a:0 language=eng \
  clip.mp4

# song.mp3: 1 s of audio with ID3 tags.
$FFMPEG $common \
  -f lavfi -i "sine=frequency=440:duration=1" \
  -c:a libmp3lame -b:a 32k \
  -metadata title="Quiet Song" \
  -metadata artist="Nobody" \
  -metadata album="Silence" \
  -metadata date=2019 \
  song.mp3

# notes.txt: not a media file; must be skipped silently.
printf 'Talk about Engines\n' > notes.txt

ls -l talk.mkv clip.mp4 song.mp3 notes.txt
