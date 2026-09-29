#!/usr/bin/env bash
# Regenerates docs/screenshots/ with the headless renderer (works with Mesa
# lavapipe, no GPU or display needed). All election-night images use the
# clearly-labelled simulation (synthetic numbers, not forecasts).
set -euo pipefail
cd "$(dirname "$0")/.."
bazel build //src/app:usa_election
BIN=bazel-bin/src/app/usa_election
OUT=docs/screenshots
DB=$(mktemp -d)/shots.db
mkdir -p "$OUT"
shot() { local name=$1; shift; "$BIN" --headless --db="$DB" --screenshot="$OUT/$name.png" "$@"; }
shot 01_nation_preelection
shot 02_georgia_preelection --focus=GA --hover=13121
shot 03_senate_night --simulate --sim_clock=23:10 --hover=39
shot 04_georgia_counties --simulate --sim_clock=23:40 --focus=GA --hover=13121
shot 05_house_es --simulate --sim_clock=01:10 --office=house --lang=es
shot 06_governors_zh --simulate --sim_clock=00:20 --office=governor --lang=zh-TW
shot 07_arizona_ja --simulate --sim_clock=02:30 --focus=AZ --office=governor --lang=ja
shot 08_progress --simulate --sim_clock=00:40 --mode=1 --yaw=-20 --pitch=32
shot 09_trend --simulate --sim_clock=02:00 --chart=trend --focus=GA
shot 10_senate_seats --simulate --sim_clock=03:30 --chart=seats
shot 11_house_seats --simulate --sim_clock=03:30 --chart=seats --office=house
shot 12_margins --simulate --sim_clock=01:30 --chart=margins
shot 13_parties --simulate --sim_clock=01:30 --chart=parties --office=governor
shot 14_grid --simulate --sim_clock=01:30 --chart=grid
shot 15_house_grid --simulate --sim_clock=02:30 --chart=grid --office=house
shot 16_help --help_overlay
