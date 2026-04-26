#!/usr/bin/env bash
#
# Build DOSu! in DOSBox using a local Turbo C++ 3.0 install.
#
# Requirements:
#   - dosbox (or dosbox-staging / dosbox-x) on PATH
#   - Turbo C++ 3.0 installed locally (TCC.EXE or BCC.EXE + INCLUDE/ + LIB/)
#
# Configure via env vars:
#   TC_DIR     path to Turbo C install   (default: ./tools/TC)
#   DOSBOX     dosbox binary name        (default: dosbox)
#   SRC        source file basename      (default: Dosu.c)
#
# Output:
#   build/<NAME>.EXE  - raw DOSBox build artifact
#   bin/<NAME>.EXE    - copy placed alongside the prebuilt binaries
#
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC_DIR="${TC_DIR:-$PROJECT_ROOT/tools/TC}"
DOSBOX="${DOSBOX:-dosbox}"
SRC="${SRC:-Dosu.c}"

if [[ ! -f "$PROJECT_ROOT/src/$SRC" ]]; then
    echo "Error: src/$SRC not found." >&2
    exit 1
fi

if ! command -v "$DOSBOX" >/dev/null 2>&1; then
    cat >&2 <<EOF
Error: '$DOSBOX' not found on PATH.
  Debian/Ubuntu: sudo apt install dosbox
  Fedora:        sudo dnf install dosbox
  Arch:          sudo pacman -S dosbox
Or set DOSBOX=dosbox-staging / dosbox-x if you use a different build.
EOF
    exit 1
fi

if [[ ! -d "$TC_DIR" ]]; then
    cat >&2 <<EOF
Error: Turbo C++ 3.0 not found at: $TC_DIR

Install Turbo C 2.01 or Turbo C++ 3.0 there (or set TC_DIR=/path/to/tc).
Expected layout:
  \$TC_DIR/BIN/TCC.EXE   (Turbo C 2.01)
       or
  \$TC_DIR/BIN/BCC.EXE   (Turbo C++ 3.0)
  \$TC_DIR/BGI/BGIOBJ.EXE
  \$TC_DIR/BGI/EGAVGA.BGI
  \$TC_DIR/INCLUDE/
  \$TC_DIR/LIB/

Turbo C++ 3.0 is available as abandonware from Embarcadero's
"Antique Software" page or the Internet Archive.
EOF
    exit 1
fi

BGIOBJ_HOST=""
BGIOBJ_DOS=""
if [[ -f "$TC_DIR/BGI/BGIOBJ.EXE" ]]; then
    BGIOBJ_HOST="$TC_DIR/BGI/BGIOBJ.EXE"
    BGIOBJ_DOS="C:\\BGI\\BGIOBJ.EXE"
elif [[ -f "$TC_DIR/BIN/BGIOBJ.EXE" ]]; then
    BGIOBJ_HOST="$TC_DIR/BIN/BGIOBJ.EXE"
    BGIOBJ_DOS="C:\\BIN\\BGIOBJ.EXE"
else
    echo "Error: BGIOBJ.EXE not found in $TC_DIR/BGI/ or $TC_DIR/BIN/" >&2
    exit 1
fi
if [[ ! -f "$TC_DIR/BGI/EGAVGA.BGI" ]]; then
    echo "Error: EGAVGA.BGI not found at $TC_DIR/BGI/EGAVGA.BGI" >&2
    exit 1
fi

# Pick compiler: prefer TCC (smaller CLI surface) then BCC.
if [[ -f "$TC_DIR/BIN/TCC.EXE" ]]; then
    COMPILER="TCC"
elif [[ -f "$TC_DIR/BIN/BCC.EXE" ]]; then
    COMPILER="BCC"
else
    echo "Error: neither TCC.EXE nor BCC.EXE found in $TC_DIR/BIN/" >&2
    exit 1
fi

BASE="${SRC%.*}"                 # Dosu
UPPER_BASE="$(echo "$BASE" | tr '[:lower:]' '[:upper:]')"  # DOSU
BUILD_DIR="$PROJECT_ROOT/build"
mkdir -p "$BUILD_DIR"

# Copy source and EGAVGA.BGI into build dir so DOSBox sees them on D:.
cp "$PROJECT_ROOT/src/$SRC" "$BUILD_DIR/$SRC"
cp "$TC_DIR/BGI/EGAVGA.BGI" "$BUILD_DIR/EGAVGA.BGI"

# Step 1: convert EGAVGA.BGI -> EGAVGA.OBJ (run from D: so output lands there).
# Step 2: compile + link, including the generated OBJ.
BGIOBJ_CMD="$BGIOBJ_DOS EGAVGA"
COMPILE_CMD="C:\\BIN\\$COMPILER.EXE -ml -IC:\\INCLUDE -LC:\\LIB $SRC EGAVGA.OBJ GRAPHICS.LIB"

echo "TC_DIR      = $TC_DIR"
echo "Compiler    = $COMPILER"
echo "BGIOBJ      = $BGIOBJ_HOST"
echo "Source      = src/$SRC"
echo "Build dir   = $BUILD_DIR"
echo "BGIOBJ cmd  = $BGIOBJ_CMD"
echo "Compile cmd = $COMPILE_CMD"
echo

# TCC compiles in-process but spawns TLINK as a child; if PATH doesn't
# include C:\BIN, the link step silently fails and leaves only a .OBJ.
# -exit quits DOSBox after the -c queue drains.
"$DOSBOX" -exit \
    -c "mount c \"$TC_DIR\"" \
    -c "mount d \"$BUILD_DIR\"" \
    -c "path=c:\\bin" \
    -c "d:" \
    -c "$BGIOBJ_CMD" \
    -c "$COMPILE_CMD" \
    -c "exit"

# Locate the resulting EXE (DOS is case-insensitive; it usually lands uppercase).
OUT=""
for candidate in "$BUILD_DIR/$UPPER_BASE.EXE" "$BUILD_DIR/$BASE.exe" "$BUILD_DIR/$BASE.EXE"; do
    if [[ -f "$candidate" ]]; then OUT="$candidate"; break; fi
done

if [[ -z "$OUT" ]]; then
    echo "Build failed: no EXE produced in $BUILD_DIR." >&2
    echo "Check DOSBox output above for TCC/BCC errors." >&2
    exit 1
fi

mkdir -p "$PROJECT_ROOT/bin"
cp "$OUT" "$PROJECT_ROOT/bin/$UPPER_BASE.EXE"
echo
echo "OK: bin/$UPPER_BASE.EXE (from $(basename "$OUT"))"
