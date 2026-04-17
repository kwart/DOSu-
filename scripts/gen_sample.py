#!/usr/bin/env python3
"""Generate a tiny sample beatmap + audio pair for DOSu!.

Writes:
    run/map.osu    osu! v14 beatmap (circles only, 120 BPM)
    run/audio.wav  8-bit mono PCM at 8 kHz (metronome clicks on each beat)

Run `scripts/run.sh` after this and the game will pick them up.
"""
import math
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUN = ROOT / "run"
RUN.mkdir(exist_ok=True)

SAMPLE_RATE = 8000
BPM = 120
BEAT_MS = int(60_000 / BPM)          # 500 ms per beat
DURATION_SEC = 20
FIRST_BEAT_MS = 1500                  # leave a moment before the first hit
LAST_BEAT_MS = (DURATION_SEC - 2) * 1000

n_samples = SAMPLE_RATE * DURATION_SEC
samples = bytearray(b"\x80" * n_samples)   # 128 = silence for 8-bit unsigned PCM


def add_click(start_ms: int, freq: float, decay_ms: int, amp: int) -> None:
    start = int(start_ms * SAMPLE_RATE / 1000)
    length = int(decay_ms * SAMPLE_RATE / 1000)
    for i in range(length):
        idx = start + i
        if idx >= n_samples:
            return
        env = math.exp(-3.0 * i / length)
        val = amp * env * math.sin(2.0 * math.pi * freq * i / SAMPLE_RATE)
        samples[idx] = max(0, min(255, int(128 + val)))


beat = 0
hit_times: list[int] = []
while True:
    t = FIRST_BEAT_MS + beat * BEAT_MS
    if t > LAST_BEAT_MS:
        break
    # Downbeats get a high click, off-beats a low one.
    if beat % 4 == 0:
        add_click(t, freq=1200, decay_ms=110, amp=80)
    elif beat % 2 == 0:
        add_click(t, freq=900, decay_ms=100, amp=65)
    else:
        add_click(t, freq=500, decay_ms=80, amp=55)
    hit_times.append(t)
    beat += 1

# Write a standard 44-byte RIFF/WAVE header via the stdlib.
with wave.open(str(RUN / "audio.wav"), "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(1)                 # 8-bit
    w.setframerate(SAMPLE_RATE)
    w.writeframes(bytes(samples))

# Hit object positions — zig-zag across a safe region of 640x480 VGA.
positions = [
    (160, 80), (320, 80), (480, 80),
    (480, 175), (320, 175), (160, 175),
    (160, 270), (320, 270), (480, 270),
    (320, 175),
]

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
    "Title:DOSu Sample",
    "Artist:Test",
    "Creator:Claude",
    "Version:Easy",
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
    f"{FIRST_BEAT_MS},{BEAT_MS}.0,4,1,0,50,1,0",
    "",
    "[HitObjects]",
]

for i, t in enumerate(hit_times):
    x, y = positions[i % len(positions)]
    type_flags = 1                     # bit 0 = circle
    if i % 4 == 0:
        type_flags |= 4                # bit 2 = new combo
    lines.append(f"{x},{y},{t},{type_flags},0")

# DOS tools are happiest with CRLF line endings.
content = "\r\n".join(lines) + "\r\n"
(RUN / "map.osu").write_bytes(content.encode("ascii"))

print(f"Wrote {RUN / 'map.osu'}   ({len(hit_times)} circles, {BPM} BPM)")
print(f"Wrote {RUN / 'audio.wav'} ({DURATION_SEC}s, {SAMPLE_RATE} Hz, 8-bit mono)")
