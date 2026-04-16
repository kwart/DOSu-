#!/usr/bin/env python3
"""Generate a DOSu! beatmap + audio pair from an mp3.

Converts the input via ffmpeg to the game's required format
(8 kHz 8-bit mono PCM WAV) and runs a pure-stdlib beat detector to
place hit circles on detected beats.

Writes:
    run/map.osu    osu! v14 beatmap
    run/audio.wav  8-bit mono PCM, 8 kHz

Usage:
    scripts/gen_from_song.py [path/to/song.mp3]

Default input is ./song.mp3 at the repo root. Requires ffmpeg on PATH.
"""
import math
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUN = ROOT / "run"
RUN.mkdir(exist_ok=True)

SAMPLE_RATE = 8000
HOP = 128                   # 16 ms per analysis frame
FRAME_MS = HOP * 1000.0 / SAMPLE_RATE
MAX_HITS = 240              # engine MAX_OBJECTS is 250; leave a little slack
LEAD_IN_MS = 1500           # delay before the first playable hit
MIN_BPM, MAX_BPM = 80, 180


def run_ffmpeg(src: Path, dst: Path) -> None:
    # Engine fseeks to a hard-coded offset of 44 for PCM data, so the
    # WAV must be a flat RIFF/fmt /data header with no LIST/INFO chunk.
    # -map_metadata -1 drops mp3 tags; -bitexact suppresses ffmpeg's
    # own "encoded by" tag.
    subprocess.run(
        [
            "ffmpeg", "-y", "-loglevel", "error",
            "-i", str(src),
            "-map_metadata", "-1",
            "-bitexact",
            "-ac", "1",
            "-ar", str(SAMPLE_RATE),
            "-acodec", "pcm_u8",
            str(dst),
        ],
        check=True,
    )


def load_samples(path: Path):
    with wave.open(str(path), "rb") as w:
        assert w.getsampwidth() == 1 and w.getnchannels() == 1
        assert w.getframerate() == SAMPLE_RATE
        n = w.getnframes()
        raw = w.readframes(n)
    return raw, n


def energy_envelope(raw: bytes, n_frames: int):
    n_hops = n_frames // HOP
    env = [0] * n_hops
    for h in range(n_hops):
        chunk = raw[h * HOP : (h + 1) * HOP]
        s = 0
        for b in chunk:
            v = b - 128
            if v < 0:
                v = -v
            s += v
        env[h] = s
    return env


def onset_strength(env):
    n = len(env)
    out = [0.0] * n
    for i in range(1, n):
        d = env[i] - env[i - 1]
        if d > 0:
            out[i] = float(d)
    mx = max(out) or 1.0
    return [x / mx for x in out]


def detect_bpm(onset):
    n = len(onset)
    min_lag = int(60000.0 / MAX_BPM / FRAME_MS)
    max_lag = int(60000.0 / MIN_BPM / FRAME_MS) + 1
    best_lag, best_score = min_lag, -1.0
    for lag in range(min_lag, max_lag + 1):
        taps = n - lag
        if taps <= 0:
            break
        s = 0.0
        for t in range(taps):
            s += onset[t] * onset[t + lag]
        s /= taps
        if s > best_score:
            best_score = s
            best_lag = lag
    bpm = 60000.0 / (best_lag * FRAME_MS)
    return bpm, best_lag


def detect_phase(onset, lag):
    n = len(onset)
    best_phase, best_score = 0, -1.0
    for phase in range(lag):
        k = 0
        t = phase
        s = 0.0
        while t < n:
            s += onset[t]
            t += lag
            k += 1
        if k:
            s /= k
        if s > best_score:
            best_score = s
            best_phase = phase
    return best_phase


def beats_from(total_ms, bpm, first_beat_ms):
    period = 60000.0 / bpm
    beats = []
    t = first_beat_ms
    while t < LEAD_IN_MS:
        t += period
    end = total_ms - 1000
    while t < end:
        beats.append(int(round(t)))
        t += period
    return beats, period


def zigzag_positions():
    # Safe rectangle inside 640x480 VGA.
    return [
        (160, 140), (320, 140), (480, 140),
        (480, 260), (320, 260), (160, 260),
        (160, 380), (320, 380), (480, 380),
        (320, 260),
    ]


def write_map(path: Path, hit_times, bpm, period_ms, first_beat_ms):
    pos = zigzag_positions()
    lines = [
        "osu file format v14",
        "",
        "[General]",
        "AudioFilename: audio.wav",
        "AudioLeadIn: 0",
        "PreviewTime: 0",
        "Countdown: 0",
        "SampleSet: Normal",
        "Mode: 0",
        "",
        "[Metadata]",
        "Title:DOSu Song Demo",
        "Artist:Unknown",
        "Creator:gen_from_song.py",
        "Version:Auto",
        "",
        "[Difficulty]",
        "HPDrainRate:3",
        "CircleSize:4",
        "OverallDifficulty:3",
        "ApproachRate:5",
        "SliderMultiplier:1.4",
        "SliderTickRate:1",
        "",
        "[Events]",
        "",
        "[TimingPoints]",
        "%d,%.2f,4,1,0,50,1,0" % (first_beat_ms, period_ms),
        "",
        "[HitObjects]",
    ]
    for i, t in enumerate(hit_times):
        x, y = pos[i % len(pos)]
        flags = 1
        if i % 4 == 0:
            flags |= 4
        lines.append("%d,%d,%d,%d,0" % (x, y, t, flags))
    path.write_bytes(("\r\n".join(lines) + "\r\n").encode("ascii"))


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "song.mp3"
    if not src.exists():
        sys.exit("input not found: %s" % src)

    wav_path = RUN / "audio.wav"
    map_path = RUN / "map.osu"

    print("converting %s -> %s" % (src.name, wav_path))
    run_ffmpeg(src, wav_path)

    raw, n_frames = load_samples(wav_path)
    total_ms = int(n_frames * 1000 / SAMPLE_RATE)
    print("loaded %d samples (%.1fs)" % (n_frames, total_ms / 1000.0))

    print("computing onset envelope...")
    env = energy_envelope(raw, n_frames)
    onset = onset_strength(env)

    print("estimating BPM...")
    bpm, lag = detect_bpm(onset)
    phase = detect_phase(onset, lag)
    first_beat_ms = int(round(phase * FRAME_MS))
    period_ms = 60000.0 / bpm
    print("  BPM ~= %.1f (lag=%d hops)" % (bpm, lag))
    print("  first beat offset = %d ms" % first_beat_ms)

    beats, period = beats_from(total_ms, bpm, first_beat_ms)
    print("  %d raw beats detected" % len(beats))

    if len(beats) > MAX_HITS:
        step = int(math.ceil(len(beats) / float(MAX_HITS)))
        beats = beats[::step]
        print("  subsampled every %d -> %d hits" % (step, len(beats)))

    first_timing = beats[0] if beats else first_beat_ms
    write_map(map_path, beats, bpm, period_ms, first_timing)
    print("wrote %s (%d hit objects)" % (map_path, len(beats)))
    print("wrote %s (%ds, %d Hz, 8-bit mono)" % (wav_path, total_ms // 1000, SAMPLE_RATE))


if __name__ == "__main__":
    main()
