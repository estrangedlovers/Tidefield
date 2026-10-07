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


# ---------------------------------------------------------------------------------------
# The factory library (resources/samples/library): longer, richer material for the
# browser. numpy is needed for these; the four core sounds above stay pure Python and
# bit-identical because tests and scores use them.
# ---------------------------------------------------------------------------------------

LIB = OUT / "library"


def write_np(name, x, stereo=None):
    import numpy as np

    LIB.mkdir(parents=True, exist_ok=True)
    chans = [x] if stereo is None else [x, stereo]
    peak = max(1e-9, max(float(np.max(np.abs(c))) for c in chans))
    data = np.stack([c * (0.89 / peak) for c in chans], axis=1)
    pcm = (np.clip(data, -1, 1) * 32767).astype("<i2")
    with wave.open(str(LIB / name), "wb") as w:
        w.setnchannels(len(chans))
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def library():
    import numpy as np

    rng = np.random.default_rng(11)

    def t_axis(seconds):
        return np.arange(int(seconds * SR)) / SR

    def hz(midi):
        return 440.0 * 2 ** ((midi - 69) / 12)

    def onepole(x, cutoff):
        a = math.exp(-2 * math.pi * cutoff / SR)
        y = np.empty_like(x)
        s = 0.0
        for i, v in enumerate(x):
            s = (1 - a) * v + a * s
            y[i] = s
        return y

    def fade(x, attack=0.005, release=0.05):
        n = len(x)
        a, r = int(attack * SR), int(release * SR)
        env = np.ones(n)
        if a:
            env[:a] = np.linspace(0, 1, a)
        if r:
            env[-r:] *= np.linspace(1, 0, r)
        return x * env

    def partials(t, f0, spec, wobble=0.0, seed=0):
        """Sum of (ratio, amp, decay, detune_hz) partials with optional slow pitch wander."""
        r = np.random.default_rng(seed)
        out = np.zeros_like(t)
        for ratio, amp, decay, beat in spec:
            phase = r.uniform(0, 2 * math.pi)
            drift = wobble * np.sin(2 * math.pi * r.uniform(0.05, 0.2) * t + phase)
            f = f0 * ratio + beat + drift
            out += amp * np.exp(-t / decay) * np.sin(2 * math.pi * np.cumsum(f) / SR + phase)
        return out

    # Singing bowl: beating partial pairs, long ring (A3).
    t = t_axis(9.0)
    spec = [(1.0, 1.0, 6.0, 0.0), (1.0, 0.6, 6.0, 1.3), (2.71, 0.5, 4.0, 0.0), (2.71, 0.3, 4.0, 2.1), (5.12, 0.25, 2.2, 0.0), (8.3, 0.1, 1.2, 0.0)]
    bowl = partials(t, hz(57), spec, seed=1) * np.minimum(1, t / 0.01)
    write_np("bowl.wav", fade(bowl, 0.0, 0.3))

    # Kalimba tine (C5): bright click into a sine with a quick 4th partial.
    t = t_axis(3.0)
    tine = partials(t, hz(72), [(1.0, 1.0, 1.4, 0), (5.4, 0.35, 0.08, 0), (9.1, 0.15, 0.03, 0)], seed=2)
    write_np("kalimba.wav", fade(tine * np.minimum(1, t / 0.0015), 0.0, 0.2))

    # Felt piano (C4): stretched partials, soft hammer (few highs), damped.
    t = t_axis(5.0)
    B = 0.0004
    spec = [(n * math.sqrt(1 + B * n * n), 0.8 / n ** 1.6, 3.5 / n ** 0.6, 0.0) for n in range(1, 14)]
    piano = partials(t, hz(60), spec, seed=3)
    piano += 0.004 * rng.standard_normal(len(t)) * np.exp(-t / 0.02)  # felt thump
    write_np("felt_piano.wav", fade(piano * np.minimum(1, t / 0.004), 0.0, 0.4))

    # Marimba bar (C4): the bar's 1 : 4 : 10 modes.
    t = t_axis(2.5)
    marimba = partials(t, hz(60), [(1.0, 1.0, 0.7, 0), (3.93, 0.4, 0.15, 0), (9.2, 0.15, 0.05, 0)], seed=4)
    write_np("marimba.wav", fade(marimba * np.minimum(1, t / 0.002), 0.0, 0.2))

    # FM bell (A4).
    t = t_axis(5.0)
    env = np.exp(-t / 1.6)
    mod = 2.6 * np.exp(-t / 0.9) * np.sin(2 * math.pi * hz(69) * 1.4 * t)
    bell = env * np.sin(2 * math.pi * hz(69) * t + mod) * np.minimum(1, t / 0.002)
    write_np("bell.wav", fade(bell, 0.0, 0.3))

    # Wind chimes: a cluster of small tubes struck at random times, pentatonic on D.
    t = t_axis(7.0)
    chimes = np.zeros_like(t)
    notes = [74, 76, 78, 81, 83, 86, 88, 90]
    for k in range(26):
        start = int(rng.uniform(0, 5.5) * SR)
        n = notes[rng.integers(len(notes))]
        tt = t[: len(t) - start]
        hit = partials(tt, hz(n), [(1.0, 1.0, 1.6, 0), (2.76, 0.4, 0.6, 0), (5.4, 0.2, 0.25, 0)], seed=50 + k)
        chimes[start:] += hit * rng.uniform(0.25, 1.0) * np.minimum(1, tt / 0.001)
    write_np("wind_chimes.wav", fade(chimes, 0.0, 0.5))

    # Choir "aah" pad (D3 + A3): sawtooth ensemble through vowel formants.
    t = t_axis(7.0)
    voices = np.zeros_like(t)
    for k, n in enumerate([50, 50, 57, 57, 62]):
        f = hz(n) * (1 + 0.004 * np.sin(2 * math.pi * (0.2 + 0.07 * k) * t + k))
        ph = np.cumsum(f) / SR
        voices += 2 * (ph - np.floor(ph + 0.5))
    spectrum = np.fft.rfft(voices)
    freqs = np.fft.rfftfreq(len(voices), 1 / SR)
    gain = sum(a * np.exp(-0.5 * ((freqs - fc) / bw) ** 2) for fc, bw, a in [(730, 90, 1.0), (1090, 110, 0.5), (2440, 160, 0.25)])
    choir = np.fft.irfft(spectrum * gain, len(voices))
    env = np.minimum(1, t / 1.2) * np.minimum(1, (t[-1] - t) / 1.5)
    write_np("choir.wav", choir * env)

    # Bowed strings pad (D2 fifth): filtered saw ensemble, slow bow.
    t = t_axis(8.0)
    saw = np.zeros_like(t)
    for k, n in enumerate([38, 45, 50, 57]):
        for d in (-0.07, 0.0, 0.08):
            f = hz(n) * 2 ** (d / 12) * (1 + 0.002 * np.sin(2 * math.pi * 5.1 * t + k))
            ph = np.cumsum(f) / SR
            saw += 2 * (ph - np.floor(ph + 0.5))
    strings = onepole(onepole(saw, 1400.0), 2200.0)
    env = np.minimum(1, t / 2.0) * np.minimum(1, (t[-1] - t) / 2.0)
    write_np("bowed_strings.wav", strings * env)

    # Reed organ / harmonium (C4 + G4).
    t = t_axis(6.0)
    reed = np.zeros_like(t)
    for n in (60, 67, 72):
        reed += sum((0.9 ** h) / h ** 0.5 * np.sin(2 * math.pi * hz(n) * h * t + h) for h in range(1, 16))
    reed *= 1 + 0.08 * np.sin(2 * math.pi * 4.5 * t)
    env = np.minimum(1, t / 0.25) * np.minimum(1, (t[-1] - t) / 0.8)
    write_np("harmonium.wav", onepole(reed, 3000.0) * env)

    # Sub organ (D2): sine and octave, no attack, a floor under everything.
    t = t_axis(6.0)
    sub = np.sin(2 * math.pi * hz(38) * t) + 0.35 * np.sin(2 * math.pi * hz(50) * t) + 0.12 * np.sin(2 * math.pi * hz(57) * t)
    env = np.minimum(1, t / 1.0) * np.minimum(1, (t[-1] - t) / 1.0)
    write_np("sub_organ.wav", sub * env)

    # Shimmer: drifting high harmonics of D, like light on water.
    t = t_axis(9.0)
    shim = np.zeros_like(t)
    for k, n in enumerate([74, 81, 86, 90, 93, 98]):
        amp = 0.5 + 0.5 * np.sin(2 * math.pi * (0.07 + 0.031 * k) * t + k * 1.7)
        shim += amp ** 2 * np.sin(2 * math.pi * hz(n) * t * (1 + 0.0007 * np.sin(2 * math.pi * 0.3 * t + k)))
    env = np.minimum(1, t / 1.5) * np.minimum(1, (t[-1] - t) / 1.5)
    write_np("shimmer.wav", shim * env)

    # Ocean wash: noise swelling and drawing back, darker as it recedes (stereo).
    t = t_axis(10.0)
    swell = (0.5 - 0.5 * np.cos(2 * math.pi * t / 10.0)) ** 1.5
    left = onepole(rng.standard_normal(len(t)), 900.0) + 0.25 * onepole(rng.standard_normal(len(t)), 4000.0) * swell
    right = onepole(rng.standard_normal(len(t)), 900.0) + 0.25 * onepole(rng.standard_normal(len(t)), 4000.0) * swell
    write_np("ocean.wav", left * swell, right * swell)

    # Rain on leaves: thousands of tiny resonant drops (stereo).
    t = t_axis(8.0)
    rain = [np.zeros_like(t), np.zeros_like(t)]
    for k in range(2500):
        start = int(rng.uniform(0, 7.9) * SR)
        f = rng.uniform(1800, 6500)
        n = int(0.012 * SR)
        tt = np.arange(n) / SR
        drop = np.sin(2 * math.pi * f * tt * (1 + 0.6 * tt / 0.012)) * np.exp(-tt / 0.003) * rng.uniform(0.1, 1.0)
        ch = k % 2
        end = min(len(t), start + n)
        rain[ch][start:end] += drop[: end - start]
    bed = 0.08 * onepole(rng.standard_normal(len(t)), 2500.0)
    write_np("rain_leaves.wav", fade(rain[0] + bed, 0.3, 0.3), fade(rain[1] + bed, 0.3, 0.3))

    # Tape dust: hiss, crackle and the odd pop, for texture under everything (stereo).
    t = t_axis(6.0)
    hiss = 0.05 * onepole(rng.standard_normal(len(t)), 7000.0)
    crackle = np.zeros_like(t)
    idx = rng.integers(0, len(t) - 40, 900)
    for i in idx:
        crackle[i : i + 3] += rng.uniform(-1, 1, 3) * rng.uniform(0.05, 0.5)
    write_np("tape_dust.wav", fade(hiss + crackle, 0.2, 0.2), fade(hiss[::-1] + np.roll(crackle, 7919), 0.2, 0.2))


if __name__ == "__main__":
    import sys

    if "--library-only" not in sys.argv:
        write("glass.wav", glass())
        write("pluck.wav", pluck())
        write("breath.wav", breath())
        write("chord.wav", chord())
    library()
    print("wrote", sorted(str(p.relative_to(OUT)) for p in OUT.rglob("*.wav")))
