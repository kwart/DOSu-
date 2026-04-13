#!/usr/bin/env bash
#
# Run DOSu! in DOSBox.
#
# Stages a disposable run/ directory containing the EXE, map, audio,
# and a C:\TC\BGI tree that matches the path compiled into initgraph().
# Generates a dosbox.conf that caps the emulated CPU speed — by default
# DOSBox runs cycles=auto, which overclocks the game on modern hardware.
#
# Usage:
#   scripts/run.sh
#   MAP=path/to/map.osu AUDIO=path/to/song.wav scripts/run.sh
#   SPEED=slow scripts/run.sh
#   CYCLES=12000 scripts/run.sh
#   RECALIBRATE=1 scripts/run.sh
#
# Env vars:
#   DOSBOX       dosbox binary name                  (default: dosbox)
#   EXE          binary under bin/                   (default: DOSU.EXE)
#   MAP          osu!-format beatmap to stage        (copied to run/map.osu)
#   AUDIO        8-bit mono PCM WAV to stage         (copied to run/audio.wav)
#   SPEED        preset: slow | normal | fast | max  (overrides autoconfig)
#   CYCLES       explicit DOSBox cycles value        (overrides SPEED)
#   RECALIBRATE  1 = re-run CPU autoconfig probe
#
# Live speed tuning inside DOSBox: Ctrl+F11 (slower) / Ctrl+F12 (faster).
#
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOSBOX="${DOSBOX:-dosbox}"
EXE="${EXE:-DOSU.EXE}"
MAP="${MAP:-}"
AUDIO="${AUDIO:-}"
SPEED="${SPEED:-}"
CYCLES="${CYCLES:-}"
RECALIBRATE="${RECALIBRATE:-}"

CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/dosu"
CALIB_FILE="$CACHE_DIR/cycles"

if ! command -v "$DOSBOX" >/dev/null 2>&1; then
    echo "Error: '$DOSBOX' not found on PATH." >&2
    exit 1
fi

# ---- resolve EXE --------------------------------------------------------
EXE_PATH=""
if [[ -f "$PROJECT_ROOT/bin/$EXE" ]]; then
    EXE_PATH="$PROJECT_ROOT/bin/$EXE"
elif [[ -f "$PROJECT_ROOT/bin/DOSU!.EXE" ]]; then
    EXE="DOSU.EXE"
    EXE_PATH="$PROJECT_ROOT/bin/DOSU!.EXE"
else
    echo "Error: no EXE found in bin/. Run scripts/build.sh first, or set EXE=..." >&2
    exit 1
fi

# ---- autoconfigure cycles ----------------------------------------------
# Preset table (cycles = DOSBox "instructions per ms"):
#   slow    ~386/486 era, for very old laptops
#   normal  ~486/early Pentium, safe default for notebooks
#   fast    ~Pentium, snappy
#   max     cycles=auto (DOSBox decides — usually too fast on modern hw)
preset_cycles() {
    case "$1" in
        slow)   echo 3000  ;;
        normal) echo 8000  ;;
        fast)   echo 20000 ;;
        max)    echo max   ;;
        *)      return 1   ;;
    esac
}

# One-shot host probe: read CPU MHz from /proc/cpuinfo and map it to a
# conservative DOSBox cycles value. We target a Pentium-era feel, so
# faster hosts don't translate 1:1 to more emulated cycles — we just
# want enough headroom that the game doesn't stutter.
autoconfig_cycles() {
    local mhz cycles
    mhz=""
    if [[ -r /proc/cpuinfo ]]; then
        # First "cpu MHz" line — current freq on any one core is fine
        # as a rough host-class indicator.
        mhz=$(awk '/^cpu MHz/{printf "%d", $4; exit}' /proc/cpuinfo 2>/dev/null || true)
    fi
    # Fallback when cpuinfo doesn't expose MHz (ARM, some VMs, containers).
    [[ -z "$mhz" || "$mhz" == "0" ]] && mhz=2000

    if   (( mhz < 600 ));  then cycles=3000     # very old netbook / Pi-class
    elif (( mhz < 1200 )); then cycles=5000
    elif (( mhz < 2000 )); then cycles=8000     # typical notebook low-freq state
    elif (( mhz < 3000 )); then cycles=12000
    else                        cycles=15000    # modern desktop
    fi
    echo "$cycles"
}

if [[ -n "$CYCLES" ]]; then
    CHOSEN_CYCLES="$CYCLES"
    SRC="explicit CYCLES env var"
elif [[ -n "$SPEED" ]]; then
    CHOSEN_CYCLES="$(preset_cycles "$SPEED")" || {
        echo "Error: SPEED must be one of slow|normal|fast|max (got '$SPEED')." >&2
        exit 1
    }
    SRC="SPEED=$SPEED preset"
else
    mkdir -p "$CACHE_DIR"
    if [[ -n "$RECALIBRATE" || ! -s "$CALIB_FILE" ]]; then
        echo "Calibrating host speed (one-time)..."
        autoconfig_cycles > "$CALIB_FILE"
    fi
    CHOSEN_CYCLES="$(cat "$CALIB_FILE")"
    SRC="autoconfig ($CALIB_FILE)"
fi

# Translate "max" → DOSBox's cycles=auto; everything else is a fixed count.
if [[ "$CHOSEN_CYCLES" == "max" ]]; then
    CYCLES_LINE="cycles=auto"
else
    CYCLES_LINE="cycles=fixed $CHOSEN_CYCLES"
fi

# ---- stage run dir ------------------------------------------------------
RUN_DIR="$PROJECT_ROOT/run"
mkdir -p "$RUN_DIR" "$RUN_DIR/TC/BGI"

cp -f "$EXE_PATH" "$RUN_DIR/$EXE"

if [[ -d "$PROJECT_ROOT/BGI" ]]; then
    shopt -s nullglob
    for f in "$PROJECT_ROOT/BGI/"*.BGI "$PROJECT_ROOT/BGI/"*.CHR; do
        cp -f "$f" "$RUN_DIR/TC/BGI/"
    done
    shopt -u nullglob
else
    echo "Warning: BGI/ directory missing — graphics init will fail." >&2
fi

if [[ -n "$MAP" ]]; then
    [[ -f "$MAP" ]] || { echo "Error: MAP file '$MAP' not found." >&2; exit 1; }
    cp -f "$MAP" "$RUN_DIR/map.osu"
fi
if [[ -n "$AUDIO" ]]; then
    [[ -f "$AUDIO" ]] || { echo "Error: AUDIO file '$AUDIO' not found." >&2; exit 1; }
    cp -f "$AUDIO" "$RUN_DIR/audio.wav"
fi

if [[ ! -f "$RUN_DIR/map.osu" ]]; then
    echo "Warning: run/map.osu missing. Pass MAP=... or run scripts/gen_sample.py." >&2
fi
if [[ ! -f "$RUN_DIR/audio.wav" ]]; then
    echo "Warning: run/audio.wav missing. Pass AUDIO=... or run scripts/gen_sample.py." >&2
fi

# ---- write dosbox.conf --------------------------------------------------
# Minimal conf: only override the speed-sensitive bits and leave the rest
# to DOSBox defaults. Using -conf so nothing else about the user's env
# needs changing.
CONF_FILE="$RUN_DIR/dosbox.conf"
cat > "$CONF_FILE" <<EOF
[sdl]
autolock=true

[cpu]
core=auto
cputype=auto
$CYCLES_LINE
cycleup=500
cycledown=500
EOF

echo "Run dir = $RUN_DIR"
echo "EXE     = $EXE"
echo "Speed   = $CHOSEN_CYCLES  ($SRC)"
echo "Tip: inside DOSBox, Ctrl+F11 slower / Ctrl+F12 faster."
echo

"$DOSBOX" -exit -conf "$CONF_FILE" \
    -c "mount c \"$RUN_DIR\"" \
    -c "c:" \
    -c "$EXE" \
    -c "exit"
