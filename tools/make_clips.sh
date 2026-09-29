#!/usr/bin/env bash
# Renders the demo clips in docs/demo/ with the headless renderer (Mesa
# lavapipe is fine). Needs ffmpeg on PATH (or python3 -m pip install
# imageio-ffmpeg). Everything shown is the labelled SIMULATION.
set -euo pipefail
cd "$(dirname "$0")/.."
bazel build //src/app:usa_election
BIN=$PWD/bazel-bin/src/app/usa_election
OUT=docs/demo
TMP=$(mktemp -d)
FFMPEG=$(command -v ffmpeg || python3 -c "import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())")
mkdir -p "$OUT"
# Mock OpenAI-compatible server for the news/LLM clip.
python3 tools/mock_openai_server.py --port 8089 >/dev/null 2>&1 &
MOCK=$!
trap 'kill $MOCK 2>/dev/null || true; rm -rf "$TMP"' EXIT
sleep 1
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1

clip() {  # name seconds fps flags...
  local name=$1 secs=$2 fps=$3; shift 3
  rm -rf "$TMP/$name" && mkdir -p "$TMP/$name"
  "$BIN" --headless --width=1280 --height=720 --db="$TMP/$name.db" --record="$TMP/$name" \
         --record_seconds="$secs" --fps="$fps" "$@"
  "$FFMPEG" -loglevel error -y -framerate "$fps" -i "$TMP/$name/frame_%05d.png" \
            -c:v libx264 -pix_fmt yuv420p -crf 23 -movflags +faststart "$OUT/$name.mp4"
}

clip 01_whole_night_x1024 44 15 --simulate --sim_speed=1024
clip 02_georgia_senate_news_x128 24 15 --simulate --sim_speed=128 --sim_clock=20:30 --focus=GA \
     --news --show_news --mock_news_rate=40 --llm_base_url=http://127.0.0.1:8089/v1 --llm_model=mock
clip 03_house_spanish_x512 16 15 --simulate --sim_speed=512 --sim_clock=21:00 --office=house --lang=es
clip 04_charts_and_pip_tour 24 15 --simulate --sim_speed=128 --sim_clock=23:30 --chart_tour=4
clip 05_governors_zh_x512 12 15 --simulate --sim_speed=512 --sim_clock=20:00 --office=governor --lang=zh-TW
clip 06_arizona_ja_x512 12 15 --simulate --sim_speed=512 --sim_clock=23:00 --focus=AZ --office=governor --lang=ja

# Everything back to back.
LIST=$TMP/list.txt
: > "$LIST"
for f in 01_whole_night_x1024 02_georgia_senate_news_x128 03_house_spanish_x512 \
         04_charts_and_pip_tour 05_governors_zh_x512 06_arizona_ja_x512; do
  echo "file '$PWD/$OUT/$f.mp4'" >> "$LIST"
done
"$FFMPEG" -loglevel error -y -f concat -safe 0 -i "$LIST" -c copy "$OUT/election_night_demo.mp4"
ls -la "$OUT"
