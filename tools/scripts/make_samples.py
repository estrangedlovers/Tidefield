#!/usr/bin/env python3
"""Synthesizes the small built-in sample set in resources/samples.

These are original, generated sounds (no third-party audio), used as default cloud
and Bloom material and by tests and render scores. Re-run to regenerate:
    python3 tools/scripts/make_samples.py
"""
import math
import random
import struct
import wave
from pathlib import Path

SR = 48000
OUT = Path(__file__).resolve().parents[2] / "resources" / "samples"


def write(name, samples):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    scale = 0.89 / peak
    with wave.open(str(OUT / name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s * scale)) * 32767)) for s in samples))


def glass(seconds=2.0, f0=880.0):
    """Struck glass: inharmonic partials with independent decays."""
    partials = [(1.0, 1.0, 1.6), (2.32, 0.5, 0.9), (4.25, 0.3, 0.5), (6.63, 0.15, 0.3), (9.38, 0.08, 0.2)]
    out = []
    for i in range(int(seconds * SR)):
        t = i / SR
        s = sum(a * math.exp(-t / d) * math.sin(2 * math.pi * f0 * r * t) for r, a, d in partials)
        out.append(s * min(1.0, t * 2000))  # 0.5 ms attack
    return out


def pluck(seconds=2.5, f0=146.83):
    """Karplus-Strong string, D3."""
    rng = random.Random(7)
    period = int(SR / f0)
    buf = [rng.uniform(-1, 1) for _ in range(period)]
    out = []
    for i in range(int(seconds * SR)):
        j = i % period
        nxt = buf[(j + 1) % period]
        buf[j] = 0.4985 * (buf[j] + nxt)
        out.append(buf[j])
    return out


def breath(seconds=2.0):
    """Shaped noise swell, like a breath across a bottle."""
    rng = random.Random(3)
    out, lp, bp1, bp2 = [], 0.0, 0.0, 0.0
    f = 2 * math.sin(math.pi * 590.0 / SR)
    q = 0.12
    for i in range(int(seconds * SR)):
        t = i / seconds / SR
        env = math.sin(math.pi * t) ** 2
        x = rng.uniform(-1, 1)
        bp1 += f * (x - bp1 - q * bp2)
        bp2 += f * bp1
        out.append((bp1 * 0.7 + x * 0.05) * env)
    return out


def chord(seconds=3.0):
    """Soft organ-like D minor chord with slow attack, for pad-style clouds."""
    notes = [50, 53, 57, 62]
    out = []
    for i in range(int(seconds * SR)):
        t = i / SR
        env = min(1.0, t / 0.4) * math.exp(-max(0.0, t - 1.5) / 0.8)
        s = 0.0
        for n in notes:
            f = 440.0 * 2 ** ((n - 69) / 12)
            s += math.sin(2 * math.pi * f * t) + 0.3 * math.sin(4 * math.pi * f * t) + 0.1 * math.sin(6 * math.pi * f * t)
        out.append(s * env)
    return out


if __name__ == "__main__":
    write("glass.wav", glass())
    write("pluck.wav", pluck())
    write("breath.wav", breath())
    write("chord.wav", chord())
    print("wrote", sorted(p.name for p in OUT.glob("*.wav")))
