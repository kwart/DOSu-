#!/usr/bin/env bash
#
# Run DOSu! in DOSBox.
#
# Stages a disposable run/ directory containing the EXE, map, audio,
# and a C:\TC\BGI tree that matches the path compiled into initgraph().
#
# Usage:
#   scripts/run.sh                              # runs bin/DOSU.EXE with run/map.osu + run/audio.wav
#   MAP=path/to/map.osu AUDIO=path/to/song.wav scripts/run.sh
#   EXE=DOSU.EXE scripts/run.sh                 # pick which binary in bin/ to run
#
# Env vars:
#   DOSBOX  dosbox binary name  (default: dosbox)
#   EXE     binary under bin/   (default: DOSU.EXE, falls back to "DOSU!.EXE")
#   MAP     osu!-format beatmap file to copy in as map.osu
#   AUDIO   8-bit mono PCM WAV to copy in as audio.wav
#
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOSBOX="${DOSBOX:-dosbox}"
EXE="${EXE:-DOSU.EXE}"
MAP="${MAP:-}"
AUDIO="${AUDIO:-}"

if ! command -v "$DOSBOX" >/dev/null 2>&1; then
    echo "Error: '$DOSBOX' not found on PATH." >&2
    exit 1
fi

# Resolve the EXE to run. Prefer the explicit name; fall back to the
# legacy "DOSU!.EXE" filename that ships in bin/.
EXE_PATH=""
if [[ -f "$PROJECT_ROOT/bin/$EXE" ]]; then
    EXE_PATH="$PROJECT_ROOT/bin/$EXE"
elif [[ -f "$PROJECT_ROOT/bin/DOSU!.EXE" ]]; then
    EXE="DOSU.EXE"          # rename on copy — '!' is awkward in DOSBox
    EXE_PATH="$PROJECT_ROOT/bin/DOSU!.EXE"
else
    echo "Error: no EXE found in bin/. Run scripts/build.sh first, or set EXE=..." >&2
    exit 1
fi

RUN_DIR="$PROJECT_ROOT/run"
mkdir -p "$RUN_DIR" "$RUN_DIR/TC/BGI"

cp -f "$EXE_PATH" "$RUN_DIR/$EXE"

# BGI drivers + fonts at C:\TC\BGI (hard-coded in initgraph()).
if [[ -d "$PROJECT_ROOT/BGI" ]]; then
    # Glob safely; ignore when a pattern has no matches.
    shopt -s nullglob
    for f in "$PROJECT_ROOT/BGI/"*.BGI "$PROJECT_ROOT/BGI/"*.CHR; do
        cp -f "$f" "$RUN_DIR/TC/BGI/"
    done
    shopt -u nullglob
else
    echo "Warning: BGI/ directory missing — graphics init will fail." >&2
fi

# Optional map/audio staging. If the user passes explicit paths,
# copy them in with the names the engine expects.
if [[ -n "$MAP" ]]; then
    [[ -f "$MAP" ]] || { echo "Error: MAP file '$MAP' not found." >&2; exit 1; }
    cp -f "$MAP" "$RUN_DIR/map.osu"
fi
if [[ -n "$AUDIO" ]]; then
    [[ -f "$AUDIO" ]] || { echo "Error: AUDIO file '$AUDIO' not found." >&2; exit 1; }
    cp -f "$AUDIO" "$RUN_DIR/audio.wav"
fi

if [[ ! -f "$RUN_DIR/map.osu" ]]; then
    echo "Warning: run/map.osu missing. Pass MAP=... or drop a map.osu into run/ first." >&2
fi
if [[ ! -f "$RUN_DIR/audio.wav" ]]; then
    echo "Warning: run/audio.wav missing. Pass AUDIO=... (8-bit mono PCM) or drop audio.wav into run/." >&2
fi

echo "Run dir = $RUN_DIR"
echo "EXE     = $EXE"
echo

"$DOSBOX" -exit \
    -c "mount c \"$RUN_DIR\"" \
    -c "c:" \
    -c "$EXE" \
    -c "exit"
