#!/usr/bin/env python3
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
    partials = [(1.0, 1.0, 1.6), (2.32, 0.5, 0.9), (4.25, 0.3, 0.5), (6.63, 0.15, 0.3), (9.38, 0.08, 0.2)]
    out = []
    for i in range(int(seconds * SR)):
        t = i / SR
        s = sum(a * math.exp(-t / d) * math.sin(2 * math.pi * f0 * r * t) for r, a, d in partials)
        out.append(s * min(1.0, t * 2000))
    return out


def pluck(seconds=2.5, f0=146.83):
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
        r = np.random.default_rng(seed)
        out = np.zeros_like(t)
        for ratio, amp, decay, beat in spec:
            phase = r.uniform(0, 2 * math.pi)
            drift = wobble * np.sin(2 * math.pi * r.uniform(0.05, 0.2) * t + phase)
            f = f0 * ratio + beat + drift
            out += amp * np.exp(-t / decay) * np.sin(2 * math.pi * np.cumsum(f) / SR + phase)
        return out


    t = t_axis(9.0)
    spec = [(1.0, 1.0, 6.0, 0.0), (1.0, 0.6, 6.0, 1.3), (2.71, 0.5, 4.0, 0.0), (2.71, 0.3, 4.0, 2.1), (5.12, 0.25, 2.2, 0.0), (8.3, 0.1, 1.2, 0.0)]
    bowl = partials(t, hz(57), spec, seed=1) * np.minimum(1, t / 0.01)
    write_np("bowl.wav", fade(bowl, 0.0, 0.3))


    t = t_axis(3.0)
    tine = partials(t, hz(72), [(1.0, 1.0, 1.4, 0), (5.4, 0.35, 0.08, 0), (9.1, 0.15, 0.03, 0)], seed=2)
    write_np("kalimba.wav", fade(tine * np.minimum(1, t / 0.0015), 0.0, 0.2))

    t = t_axis(5.0)
    B = 0.0004
    spec = [(n * math.sqrt(1 + B * n * n), 0.8 / n ** 1.6, 3.5 / n ** 0.6, 0.0) for n in range(1, 14)]
    piano = partials(t, hz(60), spec, seed=3)
    piano += 0.004 * rng.standard_normal(len(t)) * np.exp(-t / 0.02)
    write_np("felt_piano.wav", fade(piano * np.minimum(1, t / 0.004), 0.0, 0.4))


    t = t_axis(2.5)
    marimba = partials(t, hz(60), [(1.0, 1.0, 0.7, 0), (3.93, 0.4, 0.15, 0), (9.2, 0.15, 0.05, 0)], seed=4)
    write_np("marimba.wav", fade(marimba * np.minimum(1, t / 0.002), 0.0, 0.2))


    t = t_axis(5.0)
    env = np.exp(-t / 1.6)
    mod = 2.6 * np.exp(-t / 0.9) * np.sin(2 * math.pi * hz(69) * 1.4 * t)
    bell = env * np.sin(2 * math.pi * hz(69) * t + mod) * np.minimum(1, t / 0.002)
    write_np("bell.wav", fade(bell, 0.0, 0.3))

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

    t = t_axis(6.0)
    reed = np.zeros_like(t)
    for n in (60, 67, 72):
        reed += sum((0.9 ** h) / h ** 0.5 * np.sin(2 * math.pi * hz(n) * h * t + h) for h in range(1, 16))
    reed *= 1 + 0.08 * np.sin(2 * math.pi * 4.5 * t)
    env = np.minimum(1, t / 0.25) * np.minimum(1, (t[-1] - t) / 0.8)
    write_np("harmonium.wav", onepole(reed, 3000.0) * env)


    t = t_axis(6.0)
    sub = np.sin(2 * math.pi * hz(38) * t) + 0.35 * np.sin(2 * math.pi * hz(50) * t) + 0.12 * np.sin(2 * math.pi * hz(57) * t)
    env = np.minimum(1, t / 1.0) * np.minimum(1, (t[-1] - t) / 1.0)
    write_np("sub_organ.wav", sub * env)


    t = t_axis(9.0)
    shim = np.zeros_like(t)
    for k, n in enumerate([74, 81, 86, 90, 93, 98]):
        amp = 0.5 + 0.5 * np.sin(2 * math.pi * (0.07 + 0.031 * k) * t + k * 1.7)
        shim += amp ** 2 * np.sin(2 * math.pi * hz(n) * t * (1 + 0.0007 * np.sin(2 * math.pi * 0.3 * t + k)))
    env = np.minimum(1, t / 1.5) * np.minimum(1, (t[-1] - t) / 1.5)
    write_np("shimmer.wav", shim * env)


    t = t_axis(10.0)
    swell = (0.5 - 0.5 * np.cos(2 * math.pi * t / 10.0)) ** 1.5
    left = onepole(rng.standard_normal(len(t)), 900.0) + 0.25 * onepole(rng.standard_normal(len(t)), 4000.0) * swell
    right = onepole(rng.standard_normal(len(t)), 900.0) + 0.25 * onepole(rng.standard_normal(len(t)), 4000.0) * swell
    write_np("ocean.wav", left * swell, right * swell)


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


    t = t_axis(6.0)
    hiss = 0.05 * onepole(rng.standard_normal(len(t)), 7000.0)
    crackle = np.zeros_like(t)
    idx = rng.integers(0, len(t) - 40, 900)
    for i in idx:
        crackle[i : i + 3] += rng.uniform(-1, 1, 3) * rng.uniform(0.05, 0.5)
    write_np("tape_dust.wav", fade(hiss + crackle, 0.2, 0.2), fade(hiss[::-1] + np.roll(crackle, 7919), 0.2, 0.2))


def expansion():
    import numpy as np

    TAU = 2 * math.pi
    NYQ = 0.45 * SR

    def hz(midi):
        return 440.0 * 2 ** ((midi - 69) / 12)

    def axis(seconds):
        return np.arange(int(round(seconds * SR))) / SR

    def rng_for(name):
        return np.random.default_rng(sum((k + 1) * ord(c) for k, c in enumerate(name)) + 1009)

    def snap(f, length):
        return max(1.0, round(f * length)) / length

    def rms(x):
        return float(np.sqrt(np.mean(np.square(x)))) + 1e-12

    def phase_of(f):
        return TAU * np.concatenate(([0.0], np.cumsum(f)[:-1])) / SR

    def looped_phase(f):
        total = float(np.sum(f)) / SR
        return phase_of(f + (round(total) - total) * SR / len(f))

    def edge(t, attack, release):
        total = t[-1]
        up = np.clip(t / max(attack, 1e-6), 0, 1)
        down = np.clip((total - t) / max(release, 1e-6), 0, 1)
        return (0.5 - 0.5 * np.cos(math.pi * up)) * (0.5 - 0.5 * np.cos(math.pi * down))

    def lowpass(f, fc, order=2):
        return 1 / np.sqrt(1 + (f / fc) ** (2 * order))

    def highpass(f, fc, order=2):
        safe = np.maximum(f, 1e-3)
        return 1 / np.sqrt(1 + (fc / safe) ** (2 * order))

    def band(f, lo, hi, order=2):
        return highpass(f, lo, order) * lowpass(f, hi, order)

    def bump(f, fc, bw, amp):
        return amp * np.exp(-0.5 * ((f - fc) / bw) ** 2)

    def resonant(f, fc, q):
        r = f / fc
        return 1 / np.sqrt((1 - r * r) ** 2 + (r / q) ** 2)

    def noise(rng, n, gain):
        freqs = np.fft.rfftfreq(n, 1 / SR)
        spec = (rng.standard_normal(len(freqs)) + 1j * rng.standard_normal(len(freqs))) * gain(freqs)
        spec[0] = 0
        x = np.fft.irfft(spec, n)
        return x / rms(x)

    def slow(rng, n, rate):
        x = noise(rng, n, lambda f: np.exp(-0.5 * (f / rate) ** 2))
        return x / max(1e-9, float(np.max(np.abs(x))))

    def filt(x, gain, circular=False):
        n = len(x)
        m = n if circular else 1 << int(math.ceil(math.log2(2 * n)))
        freqs = np.fft.rfftfreq(m, 1 / SR)
        return np.fft.irfft(np.fft.rfft(x, m) * gain(freqs), m)[:n]

    def convolve(x, ir, circular=False):
        n = len(x)
        if circular:
            h = np.zeros(n)
            k = min(n, len(ir))
            h[:k] = ir[:k]
            return np.fft.irfft(np.fft.rfft(x) * np.fft.rfft(h), n)
        m = 1 << int(math.ceil(math.log2(n + len(ir))))
        return np.fft.irfft(np.fft.rfft(x, m) * np.fft.rfft(ir, m), m)[:n]

    def room(rng, seconds, damping, predelay=0.0):
        t = axis(seconds)
        tail = rng.standard_normal(len(t)) * np.exp(-t * 6.9 / seconds)
        tail = filt(tail, lambda f: lowpass(f, damping, 1))
        tail[: int(predelay * SR)] = 0
        return tail / math.sqrt(float(np.sum(tail ** 2)))

    def place(buf, start, ev):
        n = len(buf)
        s = int(start) % n
        while len(ev):
            k = min(len(ev), n - s)
            buf[s : s + k] += ev[:k]
            ev = ev[k:]
            s = 0

    def dc_block(x):
        y = np.empty_like(x)
        a = 1 - TAU * 12.0 / SR
        prev_x = prev_y = 0.0
        for i, v in enumerate(x.tolist()):
            prev_y = v - prev_x + a * prev_y
            prev_x = v
            y[i] = prev_y
        return y

    def strip_dc_circular(x):
        return filt(x, lambda f: highpass(f, 18.0, 2), circular=True)

    def tame(x, crest, *more):
        level = crest * math.sqrt(np.mean([rms(c) ** 2 for c in (x,) + more]))
        out = [level * np.tanh(c / level) for c in (x,) + more]
        return out if more else out[0]

    def modal(t, f0, modes):
        out = np.zeros_like(t)
        for mode in modes:
            ratio, amp, decay = mode[:3]
            beat = mode[3] if len(mode) > 3 else 0.0
            f = f0 * ratio + beat
            if f < NYQ:
                out += amp * np.exp(-t / decay) * np.sin(TAU * f * t)
        return out

    def burst(rng, t, decay, gain, amp):
        x = rng.standard_normal(len(t)) * np.exp(-t / decay)
        x = filt(x, gain)
        return amp * x / max(1e-9, float(np.max(np.abs(x))))

    def ramp(t, seconds):
        return 0.5 - 0.5 * np.cos(math.pi * np.clip(t / seconds, 0, 1))

    def harmonics(phase, f_mean, amp, top=NYQ):
        out = np.zeros_like(phase)
        h = 1
        while h * f_mean < top:
            a = amp(h, h * f_mean)
            if np.any(a):
                out += a * np.sin(h * phase + 0.7 * h * h)
            h += 1
        return out

    def oneshot(name, x, seconds_release=0.05):
        t = axis(len(x) / SR)
        x = dc_block(x) * edge(t, 0.0, seconds_release)
        write_np(name, x)

    def sustained(name, x):
        write_np(name, strip_dc_circular(x))

    rng = rng_for("vibraphone")
    t = axis(6.0)
    f0 = hz(65)
    fund = modal(t, f0, [(1.0, 1.0, 4.2)])
    upper = modal(t, f0, [(4.0, 0.32, 0.9), (10.0, 0.11, 0.22), (13.4, 0.03, 0.07)])
    motor = 0.5 + 0.5 * np.cos(TAU * 5.4 * t)
    vib = fund * (0.62 + 0.38 * motor) + upper * (0.9 + 0.1 * motor)
    vib += burst(rng, t, 0.003, lambda f: lowpass(f, 3000.0), 0.06)
    oneshot("vibraphone.wav", vib * ramp(t, 0.0015), 0.5)

    rng = rng_for("glass harmonica")
    t = axis(7.0)
    f0 = hz(72)
    flutter = 1 + 0.06 * slow(rng, len(t), 3.0) + 0.08 * np.sin(TAU * 0.45 * t)
    ph = phase_of(f0 * (1 + 0.0006 * slow(rng, len(t), 0.8)))
    tone = np.sin(ph) + 0.12 * np.sin(2 * ph + 0.4) + 0.04 * np.sin(3 * ph + 1.1) + 0.015 * np.sin(5 * ph)
    rub = 0.05 * noise(rng, len(t), lambda f: bump(f, f0, 6.0, 1.0) + bump(f, 2 * f0, 8.0, 0.4))
    air = 0.006 * noise(rng, len(t), lambda f: band(f, 5000.0, 10000.0))
    harmonica = (tone + rub) * flutter + air
    oneshot("glass_harmonica.wav", harmonica * edge(t, 0.35, 1.6), 0.1)

    rng = rng_for("music box")
    t = axis(4.0)
    f0 = hz(84)
    tine = modal(t, f0, [(1.0, 1.0, 2.8), (1.0, 0.25, 2.8, 0.45), (6.27, 0.22, 0.32), (17.55, 0.07, 0.07), (34.39, 0.03, 0.02)])
    pin = burst(rng, t, 0.0007, lambda f: bump(f, 650.0, 200.0, 1.0) + bump(f, 1800.0, 500.0, 0.7) + highpass(f, 5000.0) * 0.4, 0.12)
    oneshot("music_box.wav", tine * ramp(t, 0.0005) + pin, 0.3)

    rng = rng_for("celesta")
    t = axis(4.0)
    f0 = hz(72)
    plate = modal(t, f0, [(1.0, 1.0, 2.2), (2.0, 0.12, 0.7), (5.4, 0.05, 0.08), (8.93, 0.02, 0.03)])
    hammer = burst(rng, t, 0.008, lambda f: lowpass(f, 800.0), 0.05)
    oneshot("celesta.wav", plate * ramp(t, 0.0015) + hammer, 0.3)

    def plucked(t, f0, position, tilt, base_decay, damping, inharm=0.0, glide=0.0, top=12000.0):
        out = np.zeros_like(t)
        bend = 1 + glide * np.exp(-t / 0.06)
        n = 1
        while n * f0 < min(top, NYQ):
            fn = n * f0 * math.sqrt(1 + inharm * n * n)
            amp = abs(math.sin(n * math.pi * position)) / n ** tilt
            decay = base_decay / (1 + damping * n * n)
            out += amp * np.exp(-t / decay) * np.sin(phase_of(fn * bend))
            n += 1
        return out

    rng = rng_for("harp")
    t = axis(5.0)
    harp = plucked(t, hz(55), 0.22, 1.5, 4.5, 0.004, 1.5e-5)
    harp = filt(harp, lambda f: 0.6 + bump(f, 190.0, 50.0, 0.7) + bump(f, 430.0, 110.0, 0.5) + bump(f, 1000.0, 300.0, 0.3))
    harp += burst(rng, t, 0.002, lambda f: band(f, 400.0, 3000.0), 0.03)
    oneshot("harp.wav", harp * ramp(t, 0.002), 0.4)

    rng = rng_for("koto")
    t = axis(4.0)
    koto = plucked(t, hz(62), 0.07, 1.1, 2.6, 0.01, 4e-5, 0.004)
    koto = filt(koto, lambda f: 0.5 + bump(f, 300.0, 80.0, 0.6) + bump(f, 900.0, 250.0, 0.5) + bump(f, 2300.0, 600.0, 0.35))
    koto += burst(rng, t, 0.0008, lambda f: band(f, 1500.0, 9000.0), 0.1)
    oneshot("koto.wav", koto * ramp(t, 0.0008), 0.3)

    rng = rng_for("tongue drum")
    t = axis(5.0)
    f0 = hz(62)
    tongue = modal(t, f0, [(1.0, 1.0, 3.0), (1.0, 0.35, 3.0, 0.7), (2.0, 0.28, 1.4), (3.0, 0.1, 0.6), (4.62, 0.04, 0.2)])
    thump = burst(rng, t, 0.015, lambda f: lowpass(f, 400.0), 0.12)
    oneshot("tongue_drum.wav", tongue * ramp(t, 0.003) + thump, 0.4)

    rng = rng_for("electric piano")
    t = axis(5.0)
    f0 = hz(60)
    tine = modal(t, f0, [(1.0, 1.0, 2.6), (2.0, 0.15, 1.2), (3.0, 0.05, 0.6), (7.2, 0.22, 0.05), (13.9, 0.07, 0.02)])
    tine *= ramp(t, 0.001)
    pickup = tine + 0.35 * tine ** 2 - 0.08 * tine ** 3
    pickup += burst(rng, t, 0.004, lambda f: lowpass(f, 1500.0), 0.04)
    oneshot("electric_piano.wav", pickup, 0.4)

    rng = rng_for("bonang")
    t = axis(5.0)
    f0 = hz(63)
    gong = modal(t, f0, [(1.0, 1.0, 3.2), (1.0, 0.4, 3.2, 0.9), (2.03, 0.18, 1.6), (2.78, 0.35, 1.2), (2.78, 0.2, 1.2, 1.7),
                         (4.13, 0.15, 0.5), (5.92, 0.06, 0.25)])
    gong += burst(rng, t, 0.006, lambda f: lowpass(f, 1000.0), 0.08)
    oneshot("bonang.wav", gong * ramp(t, 0.002), 0.4)

    rng = rng_for("temple bell")
    t = axis(9.0)
    f0 = hz(43)
    spec = []
    for ratio, amp, decay, beat in [(1.0, 1.0, 6.5, 0.45), (2.05, 0.55, 5.0, 0.8), (2.62, 0.45, 4.0, 1.1), (3.69, 0.3, 3.0, 0.6),
                                    (4.92, 0.18, 1.8, 1.4), (6.31, 0.1, 1.0, 0.9), (8.1, 0.05, 0.5, 1.7)]:
        spec += [(ratio, amp, decay), (ratio, 0.6 * amp, decay * 0.9, beat)]
    bell = modal(t, f0, spec) * ramp(t, 0.004)
    bell += burst(rng, t, 0.04, lambda f: lowpass(f, 300.0), 0.35) + burst(rng, t, 0.01, lambda f: band(f, 300.0, 2000.0), 0.08)
    oneshot("temple_bell.wav", bell, 1.8)

    rng = rng_for("crystal bowl")
    t = axis(8.0)
    f0 = hz(60)
    swell = 1 + 0.12 * np.sin(TAU * 0.21 * t) + 0.05 * slow(rng, len(t), 1.0)
    crystal = np.sin(TAU * f0 * t) + 0.7 * np.sin(TAU * (f0 + 0.8) * t + 1.0) + 0.05 * np.sin(TAU * 2 * f0 * t) + 0.03 * np.sin(TAU * 3 * f0 * t)
    crystal += 0.06 * noise(rng, len(t), lambda f: bump(f, f0, 4.0, 1.0))
    crystal = crystal * swell + 0.012 * noise(rng, len(t), lambda f: lowpass(f, 1200.0)) * swell
    oneshot("crystal_bowl.wav", crystal * edge(t, 2.0, 1.8), 0.1)

    def detuned(rng, base, cents):
        return base * 2 ** (rng.normal(0, cents) / 1200)

    def pad_voice(rng, t, f, vib_rate, vib_depth, amp, drift=0.001, delay=0.0):
        onset = np.clip((t - delay) / 1.5, 0, 1) if delay else 1.0
        vibrato = vib_depth * onset * np.sin(TAU * vib_rate * t + rng.uniform(0, TAU))
        wander = drift * slow(rng, len(t), 0.4)
        return harmonics(phase_of(f * (1 + vibrato + wander)), f, amp)

    rng = rng_for("warm analog")
    t = axis(7.0)
    cutoff = 800 + 1300 * (0.5 - 0.5 * np.cos(TAU * t / 7.0)) ** 1.2
    analog = np.zeros_like(t)
    for n in (48, 55, 60, 64, 67):
        for d in (-7.0, 0.0, 6.0):
            f = hz(n) * 2 ** (d / 1200)
            analog += pad_voice(rng, t, f, 0.0, 0.0, lambda h, fh: (1.0 / h) * resonant(fh, cutoff, 1.3) * lowpass(fh, 1.6 * cutoff, 1) * (fh < 12000), 0.0012)
    sustained("warm_analog.wav", analog * edge(t, 1.4, 1.8))

    rng = rng_for("string ensemble")
    t = axis(7.0)
    body = lambda fh: (0.4 + bump(fh, 280.0, 80.0, 1.0) + bump(fh, 500.0, 120.0, 0.7) + bump(fh, 1200.0, 300.0, 0.5) + bump(fh, 2800.0, 600.0, 0.6)) * lowpass(fh, 4500.0)
    ensemble = np.zeros_like(t)
    for n in (43, 50, 55, 59, 62):
        for _ in range(5):
            f = detuned(rng, hz(n), 6.0)
            ensemble += pad_voice(rng, t, f, rng.uniform(4.8, 5.6), 0.0025, lambda h, fh: body(fh) / h, 0.0008, rng.uniform(0.5, 1.5))
    ensemble += 0.03 * rms(ensemble) * noise(rng, len(t), lambda f: band(f, 2000.0, 6000.0))
    sustained("string_ensemble.wav", ensemble * edge(t, 1.6, 1.8))

    def formants(table):
        return lambda fh: sum(a / np.sqrt(1 + ((fh - fc) / bw) ** 2) for fc, bw, a in table)

    rng = rng_for("airy voices")
    t = axis(7.0)
    ooh = formants([(320.0, 60.0, 1.0), (800.0, 80.0, 0.35), (2300.0, 120.0, 0.08), (3000.0, 200.0, 0.03)])
    voices = np.zeros_like(t)
    for n in (57, 64, 69, 72):
        for _ in range(4):
            f = detuned(rng, hz(n), 5.0)
            voices += pad_voice(rng, t, f, rng.uniform(5.0, 5.6), 0.003, lambda h, fh: ooh(fh) / h, 0.0015, rng.uniform(0.8, 2.0))
    voices += 0.15 * rms(voices) * noise(rng, len(t), lambda f: ooh(f) * lowpass(f, 3500.0) + 0.05 * band(f, 4000.0, 9000.0))
    sustained("airy_voices.wav", voices * edge(t, 1.8, 1.8))

    rng = rng_for("reed organ")
    t = axis(7.0)
    bellows = 1 + 0.06 * slow(rng, len(t), 0.8)
    reeds = np.zeros_like(t)
    for n in (53, 60, 65, 69):
        for beat in (-0.6, 0.6):
            f = hz(n) + beat
            reeds += pad_voice(rng, t, f, 0.0, 0.0, lambda h, fh: h ** -0.7 * lowpass(fh, 3500.0), 0.0003)
    reeds = reeds * bellows + 0.01 * rms(reeds) * noise(rng, len(t), lambda f: band(f, 500.0, 4000.0))
    sustained("reed_organ.wav", reeds * edge(t, 0.15, 0.7))

    rng = rng_for("glass pad")
    t = axis(7.0)
    glasspad = np.zeros_like(t)
    for n in (64, 71, 76, 78):
        f0 = hz(n)
        for ratio, amp, beat in [(1.0, 1.0, 0.0), (1.0, 0.5, 0.6), (2.0, 0.3, 0.0), (3.01, 0.12, 0.0), (4.17, 0.06, 0.0), (5.43, 0.03, 0.0)]:
            lfo = 0.6 + 0.4 * np.sin(TAU * rng.uniform(0.05, 0.15) * t + rng.uniform(0, TAU))
            glasspad += amp * lfo * np.sin(TAU * (f0 * ratio + beat) * t + rng.uniform(0, TAU))
    glasspad += 0.02 * rms(glasspad) * noise(rng, len(t), lambda f: band(f, 6000.0, 10000.0))
    sustained("glass_pad.wav", glasspad * edge(t, 2.0, 2.0))

    rng = rng_for("cello section")
    t = axis(7.0)
    cbody = lambda fh: (0.3 + bump(fh, 220.0, 60.0, 1.0) + bump(fh, 450.0, 100.0, 0.7) + bump(fh, 900.0, 200.0, 0.4)) * lowpass(fh, 1800.0)
    pressure = 1 + 0.15 * np.sin(TAU * 0.13 * t) + 0.05 * slow(rng, len(t), 1.0)
    cellos = np.zeros_like(t)
    for n in (36, 43, 48):
        for _ in range(4):
            f = detuned(rng, hz(n), 5.0)
            cellos += pad_voice(rng, t, f, rng.uniform(4.5, 5.1), 0.0035, lambda h, fh: cbody(fh) / h ** 0.9, 0.0008, rng.uniform(0.6, 1.6))
    cellos = cellos * pressure + 0.02 * rms(cellos) * noise(rng, len(t), lambda f: band(f, 1000.0, 4000.0))
    sustained("cello_section.wav", cellos * edge(t, 1.8, 1.8))

    rng = rng_for("chamber choir")
    t = axis(7.0)
    low_a = [(600.0, 70.0, 1.0), (1040.0, 90.0, 0.45), (2250.0, 130.0, 0.2), (2800.0, 180.0, 0.15)]
    high_a = [(800.0, 90.0, 1.0), (1150.0, 100.0, 0.5), (2900.0, 160.0, 0.18), (3900.0, 220.0, 0.06)]
    choir = np.zeros_like(t)
    for n in (40, 47, 52, 59, 64):
        mix = (n - 40) / 24
        table = [(a[0] + mix * (b[0] - a[0]), a[1] + mix * (b[1] - a[1]), a[2] + mix * (b[2] - a[2])) for a, b in zip(low_a, high_a)]
        shape = formants(table)
        for _ in range(3):
            f = detuned(rng, hz(n), 7.0)
            choir += pad_voice(rng, t, f, rng.uniform(5.0, 6.0), 0.005, lambda h, fh: shape(fh) / h ** 0.8, 0.0015, rng.uniform(0.6, 1.5))
    choir += 0.05 * rms(choir) * noise(rng, len(t), formants(high_a))
    sustained("chamber_choir.wav", choir * edge(t, 1.6, 1.8))

    rng = rng_for("warped tape")
    t = axis(7.0)
    wow = 1 + 0.004 * np.sin(TAU * 0.55 * t) + 0.0012 * np.sin(TAU * 6.5 * t) + 0.0015 * slow(rng, len(t), 1.5)
    tape = np.zeros_like(t)
    for n in (50, 57, 60, 65):
        for d in (-4.0, 4.0):
            f = hz(n) * 2 ** (d / 1200)
            tape += harmonics(phase_of(f * wow), f, lambda h, fh: (0.6 / h ** 2 if h % 2 else 0.0) + 0.35 / h * lowpass(fh, 2500.0))
    tape = np.tanh(1.6 * tape / (3 * rms(tape)))
    tape = filt(tape, lambda f: lowpass(f, 5000.0))
    for when in rng.uniform(1.5, 6.5, 2):
        tape *= 1 - 0.5 * np.exp(-0.5 * ((t - when) / 0.05) ** 2)
    tape += 0.015 * noise(rng, len(t), lambda f: band(f, 1500.0, 12000.0))
    sustained("warped_tape.wav", tape * edge(t, 1.2, 1.6))

    L = 12.0
    t = axis(L)

    rng = rng_for("tanpura")
    tanpura = np.zeros_like(t)
    tp = axis(8.0)
    for string, offset in [(43, 0.0), (48, 1.1), (48, 1.75), (36, 2.4)]:
        f0 = hz(string)
        centre = 3 + 22 * np.exp(-tp / 1.8)
        pluck = np.zeros_like(tp)
        h = 1
        while h * f0 < 9000 and h <= 80:
            gain = (0.12 + np.exp(-0.5 * ((h - centre) / 4.0) ** 2)) / h * min(1.0, (9000 - h * f0) / 3000)
            pluck += gain * np.exp(-tp / (6.0 / (1 + 0.002 * h * h))) * np.sin(TAU * h * f0 * tp)
            h += 1
        pluck *= ramp(tp, 0.004) * edge(tp, 0.0, 1.0)
        for cycle in range(3):
            place(tanpura, (offset + 4.0 * cycle) * SR, pluck * rng.uniform(0.8, 1.0) * (1.3 if string == 36 else 1.0))
    sustained("tanpura.wav", tanpura)

    L = 10.0
    t = axis(L)

    rng = rng_for("cello drone")
    f0 = snap(hz(36), L)
    bow = 1 + 0.08 * slow(rng, len(t), 0.7) + 0.05 * slow(rng, len(t), 4.0)
    for change in (0.0, L / 2):
        dist = (t - change + L / 2) % L - L / 2
        bow *= 1 - 0.3 * np.exp(-0.5 * (dist / 0.18) ** 2)
    vib = 1 + 0.001 * np.sin(TAU * snap(4.6, L) * t)
    drone = harmonics(looped_phase(f0 * vib), f0, lambda h, fh: cbody(fh) / h)
    fifth = harmonics(looped_phase(snap(hz(43), L) * vib), hz(43), lambda h, fh: 0.35 * cbody(fh) / h)
    rosin = 0.03 * noise(rng, len(t), lambda f: band(f, 1500.0, 5000.0))
    sustained("cello_drone.wav", (drone + fifth) * bow + rosin * rms(drone) * bow)

    rng = rng_for("bowed metal")
    f0 = snap(hz(57), L)
    metal = np.zeros_like(t)
    bar = [(1.0, 1.0), (2.756, 0.55), (5.404, 0.3), (8.933, 0.15), (13.34, 0.06)]
    for ratio, amp in bar:
        f = snap(f0 * ratio, L)
        metal += amp * (0.5 + 0.5 * slow(rng, len(t), 0.25)) ** 2 * np.sin(TAU * f * t + rng.uniform(0, TAU))
    ring = axis(2.5)
    ir = sum(amp * np.exp(-ring / (2.0 / ratio ** 0.5)) * np.sin(TAU * f0 * ratio * ring) for ratio, amp in bar)
    friction = noise(rng, len(t), lambda f: band(f, 150.0, 6000.0)) * (0.6 + 0.4 * slow(rng, len(t), 2.0))
    excited = convolve(friction, ir, circular=True)
    metal = metal / rms(metal) + 0.5 * excited / rms(excited)
    sustained("bowed_metal.wav", metal)

    rng = rng_for("organ pedal")
    f0 = snap(hz(24), L)
    wind = 1 + 0.01 * slow(rng, len(t), 2.0)
    bourdon = sum(a * np.sin(TAU * h * f0 * t) for h, a in [(1, 1.0), (2, 0.04), (3, 0.22), (5, 0.07), (7, 0.02)])
    f8 = snap(2 * hz(24) + 0.15, L)
    principal = harmonics(TAU * f8 * t, f8, lambda h, fh: 0.6 / h ** 1.3 * (h <= 10))
    f4 = snap(4 * hz(24) - 0.1, L)
    octave = harmonics(TAU * f4 * t, f4, lambda h, fh: 0.25 / h ** 1.5 * (h <= 8))
    air = 0.01 * noise(rng, len(t), lambda f: band(f, 300.0, 3000.0))
    sustained("organ_pedal.wav", (bourdon + principal + octave) * wind + air * (1 + 0.3 * slow(rng, len(t), 1.0)))

    rng = rng_for("sub hum")
    f0 = snap(hz(28), L)
    beat = snap(hz(28) + 0.13, L)
    sub = np.sin(TAU * f0 * t) + 0.5 * np.sin(TAU * beat * t) + 0.3 * np.sin(TAU * 2 * f0 * t) + 0.1 * np.sin(TAU * 3 * f0 * t)
    sub += 0.03 * np.sin(TAU * 5 * f0 * t) * (0.5 + 0.5 * np.sin(TAU * snap(0.17, L) * t))
    rumble = noise(rng, len(t), lambda f: band(f, 25.0, 120.0)) * (0.7 + 0.3 * slow(rng, len(t), 0.3))
    sustained("sub_hum.wav", sub + 0.25 * rumble)

    def bird(rng, kind):
        segments = []
        if kind == "warbler":
            for _ in range(rng.integers(6, 11)):
                d = rng.uniform(0.04, 0.09)
                f1 = rng.uniform(3500, 6000)
                segments.append((d, f1, f1 + rng.uniform(-1200, 1000), 0.0))
                segments.append((rng.uniform(0.02, 0.04), 0, 0, 0.0))
        elif kind == "thrush":
            for _ in range(rng.integers(3, 6)):
                f1 = rng.uniform(1800, 3200)
                segments.append((rng.uniform(0.12, 0.25), f1, f1 * rng.uniform(0.9, 1.15), 0.15))
                segments.append((rng.uniform(0.05, 0.1), 0, 0, 0.0))
        elif kind == "trill":
            for _ in range(rng.integers(15, 26)):
                segments.append((0.025, 5500.0, 4000.0, 0.0))
                segments.append((0.03, 0, 0, 0.0))
        else:
            segments = [(0.15, 4200.0, 4300.0, 0.05), (0.06, 0, 0, 0.0), (0.25, 3300.0, 2800.0, 0.05)]
        out = []
        for d, fa, fb, second in segments:
            n = int(d * SR)
            if fa == 0:
                out.append(np.zeros(n))
                continue
            u = np.arange(n) / n
            f = fa + (fb - fa) * u + (fa * 0.01 * np.sin(TAU * 30 * u * d) if kind == "thrush" else 0)
            ph = phase_of(f)
            env = np.sin(math.pi * u) ** 1.5
            out.append(env * (np.sin(ph) + second * np.sin(2 * ph)))
        return np.concatenate(out)

    rng = rng_for("forest dawn")
    forest = [0.04 * noise(rng, len(t), lambda f: lowpass(f, 1500.0, 1) * highpass(f, 80.0)) * (1 + 0.5 * slow(rng, len(t), 0.3)) for _ in range(2)]
    kinds = ["warbler", "thrush", "trill", "call"]
    for k in range(16):
        kind = kinds[k % 4]
        call = bird(rng, kind)
        distance = rng.uniform(0.15, 1.0)
        call = filt(call, lambda f, d=distance: lowpass(f, 3000.0 + 6000.0 * d, 1)) * distance * 0.5
        pan = rng.uniform(0.15, 0.85)
        start = rng.uniform(0, L) * SR
        place(forest[0], start, call * math.sqrt(1 - pan))
        place(forest[1], start, call * math.sqrt(pan))
    space = [convolve(c, room(rng, 1.4, 4000.0, 0.02), circular=True) for c in forest]
    forest = [strip_dc_circular(c + 0.5 * s) for c, s in zip(forest, space)]
    write_np("forest_dawn.wav", forest[0], forest[1])

    def bubble(rng, f0, decay):
        n = int(decay * 6 * SR)
        u = np.arange(n) / SR
        f = f0 * (1 + 0.25 * u / decay / 6)
        return np.sin(phase_of(f)) * np.exp(-u / decay) * ramp(u, 0.0005)

    rng = rng_for("stream")
    stream = []
    for side in range(2):
        water = 0.35 * noise(rng, len(t), lambda f: band(f, 200.0, 3000.0, 1) * (1000.0 / np.maximum(f, 200.0)) ** 0.5)
        water *= 0.7 + 0.3 * slow(rng, len(t), 0.4)
        for _ in range(int(220 * L)):
            f0 = math.exp(rng.uniform(math.log(500), math.log(3000)))
            decay = 0.006 * (3000 / f0) ** 0.7
            place(water, rng.uniform(0, L) * SR, bubble(rng, f0, decay) * rng.uniform(0.02, 0.25))
        for _ in range(int(12 * L)):
            f0 = rng.uniform(150, 400)
            place(water, rng.uniform(0, L) * SR, bubble(rng, f0, rng.uniform(0.02, 0.05)) * rng.uniform(0.1, 0.3))
        stream.append(water)
    stream = [strip_dc_circular(c + 0.25 * convolve(c, room(rng, 0.6, 5000.0), circular=True)) for c in stream]
    write_np("stream.wav", stream[0], stream[1])

    rng = rng_for("distant thunder")
    tl = axis(12.0)
    envelope = np.zeros_like(tl)
    for start, length, strength in [(0.8, 6.0, 1.0), (7.4, 4.5, 0.55)]:
        roll_t = axis(length + 4.0)
        roll = np.zeros_like(roll_t)
        for _ in range(int(length * 9)):
            at = rng.exponential(length / 3)
            if at < length:
                roll += rng.uniform(0.2, 1.0) * np.exp(-np.maximum(0, roll_t - at) / rng.uniform(0.3, 1.5)) * (roll_t >= at)
        roll = filt(roll, lambda f: lowpass(f, 12.0, 1))
        place(envelope, start * SR, strength * roll / max(1e-9, float(np.max(roll))))
    rumble = noise(rng, len(tl), lambda f: band(f, 22.0, 180.0, 3) + 0.25 * band(f, 180.0, 700.0, 3))
    thunder = rumble * envelope ** 1.3 + 0.03 * noise(rng, len(tl), lambda f: band(f, 60.0, 900.0, 3))
    write_np("distant_thunder.wav", strip_dc_circular(thunder))

    rng = rng_for("radio static")
    L2 = 10.0
    t2 = axis(L2)
    fading = 0.65 + 0.25 * np.sin(TAU * snap(0.3, L2) * t2) + 0.1 * slow(rng, len(t2), 0.8)
    static = noise(rng, len(t2), lambda f: band(f, 150.0, 4500.0, 3) * (1 + bump(f, 1200.0, 500.0, 0.5))) * fading
    clicks = np.zeros_like(t2)
    for _ in range(int(20 * L2)):
        place(clicks, rng.uniform(0, L2) * SR, rng.standard_normal(4) * rng.pareto(2.5) * 0.6)
    static += filt(clicks, lambda f: band(f, 300.0, 4000.0), circular=True)
    drift = 1200 + 300 * np.sin(TAU * snap(0.1, L2) * t2)
    whistle = 0.08 * np.sin(looped_phase(drift)) * (0.5 + 0.5 * np.sin(TAU * snap(0.2, L2) * t2))
    key = np.zeros_like(t2)
    pos = 0.6
    for dur in rng.choice([0.08, 0.24], 24):
        if pos + dur > L2 - 0.4:
            break
        key[int(pos * SR) : int((pos + dur) * SR)] = 1.0
        pos += dur + 0.08 + (0.24 if rng.random() < 0.25 else 0.0)
    key = filt(key, lambda f: lowpass(f, 120.0, 1), circular=True)
    morse = 0.06 * key * np.sin(TAU * snap(700.0, L2) * t2)
    write_np("radio_static.wav", strip_dc_circular(tame(static + whistle + morse, 5.0)))

    rng = rng_for("vinyl crackle")
    rev = 1.8
    L3 = 6 * rev
    t3 = axis(L3)
    surface = 0.05 * noise(rng, len(t3), lambda f: band(f, 1000.0, 9000.0)) * (1 + 0.2 * np.sin(TAU * t3 / rev))
    surface += 0.02 * noise(rng, len(t3), lambda f: band(f, 30.0, 120.0))
    ticks = np.zeros_like(t3)
    for _ in range(int(45 * L3)):
        place(ticks, rng.uniform(0, L3) * SR, rng.standard_normal(rng.integers(2, 6)) * min(6.0, rng.pareto(2.0)) * 0.15)
    for k in range(6):
        place(ticks, (0.73 + k * rev) * SR, np.array([0.9, -0.6, 0.3, -0.1]))
    pops = np.zeros_like(t3)
    pt = axis(0.03)
    for _ in range(5):
        place(pops, rng.uniform(0, L3) * SR, np.sin(TAU * rng.uniform(300, 900) * pt) * np.exp(-pt / 0.005) * rng.uniform(0.4, 0.8))
    crackle = surface + filt(ticks, lambda f: band(f, 1500.0, 9000.0, 1), circular=True) + pops
    write_np("vinyl_crackle.wav", strip_dc_circular(tame(crackle, 7.0)))

    rng = rng_for("fire crackle")
    breath = 0.7 + 0.3 * slow(rng, len(t), 0.5)
    fire = 0.3 * noise(rng, len(t), lambda f: band(f, 30.0, 400.0, 1) * (100.0 / np.maximum(f, 30.0))) * breath
    fire += 0.05 * noise(rng, len(t), lambda f: band(f, 3000.0, 8000.0)) * breath
    pt = axis(0.06)
    for _ in range(int(1.6 * L)):
        centre = rng.uniform(0, L)
        for _ in range(rng.integers(3, 16)):
            fc = rng.uniform(1000, 5000)
            click = np.sin(TAU * fc * pt) * np.exp(-pt / rng.uniform(0.001, 0.008)) * min(4.0, rng.pareto(1.8) + 0.2) * 0.4
            place(fire, (centre + rng.uniform(0, 0.15)) * SR, click)
    for _ in range(3):
        snapping = rng.standard_normal(len(pt)) * np.exp(-pt / 0.015)
        place(fire, rng.uniform(0, L) * SR, filt(snapping, lambda f: band(f, 400.0, 6000.0)) * 1.5)
    write_np("fire_crackle.wav", strip_dc_circular(tame(fire, 6.0)))

    rng = rng_for("night insects")
    insects = 0.03 * noise(rng, len(t), lambda f: lowpass(f, 600.0) * highpass(f, 60.0))
    for carrier, period, pulses, amp in [(4500.0, 0.5, 4, 0.5), (4850.0, 0.62, 3, 0.35), (4200.0, 0.44, 5, 0.22)]:
        chirps = round(L / period)
        pu = axis(0.012)
        shape = np.sin(math.pi * pu / 0.012) ** 2
        offset = rng.uniform(0, L / chirps)
        for c in range(chirps):
            for p in range(pulses):
                fc = carrier * (1 + 0.004 * rng.standard_normal())
                ph = TAU * fc * pu
                place(insects, (offset + c * L / chirps + p * 0.03) * SR, amp * shape * (np.sin(ph) + 0.08 * np.sin(2 * ph)))
    trill = 0.18 * (0.5 + 0.5 * np.sin(TAU * snap(45.0, L) * t)) ** 2 * np.sin(TAU * snap(2900.0, L) * t)
    insects += trill * (0.7 + 0.3 * np.sin(TAU * snap(0.08, L) * t))
    rasp = noise(rng, len(t), lambda f: bump(f, 8000.0, 800.0, 1.0))
    gate = np.zeros_like(t)
    groups = round(L / 1.5)
    for g in range(groups):
        for p in range(6):
            s = int((g * L / groups + 0.4 + p * 0.06) * SR)
            gate[s : s + int(0.035 * SR)] = 1.0
    insects += 0.1 * rasp * filt(gate, lambda f: lowpass(f, 200.0, 1), circular=True)
    insects += 0.3 * convolve(insects, room(rng, 0.9, 6000.0), circular=True)
    write_np("night_insects.wav", strip_dc_circular(insects))

    rng = rng_for("wind wires")
    speed = 0.5 + 0.5 * slow(rng, len(t), 0.12)
    wires = np.zeros_like(t)
    for base, centre, amp in [(520.0, 0.35, 0.5), (780.0, 0.6, 0.35), (1170.0, 0.8, 0.25)]:
        f = base * (0.85 + 0.3 * speed)
        lock = np.exp(-0.5 * ((speed - centre) / 0.18) ** 2)
        ph = looped_phase(f)
        wires += 3 * amp * lock * (np.sin(ph) + 0.1 * np.sin(2 * ph) + 0.15 * np.sin(3 * ph))
    gust = 0.25 + 0.75 * speed ** 2
    air = noise(rng, len(t), lambda f: band(f, 200.0, 1500.0, 1)) * gust * 0.5
    air += 0.15 * noise(rng, len(t), lambda f: bump(f, 2800.0, 900.0, 1.0)) * speed ** 3
    air += 0.3 * noise(rng, len(t), lambda f: band(f, 40.0, 150.0)) * gust
    write_np("wind_wires.wav", strip_dc_circular(wires + air))

    rng = rng_for("rain tin roof")
    panel = np.sort(np.exp(rng.uniform(math.log(900), math.log(7000), 160)))
    lows = rng.uniform(120, 400, 6)
    roof = []
    for side in range(2):
        hits = 0.06 * noise(rng, len(t), lambda f: band(f, 2000.0, 12000.0, 1))
        hits += 0.04 * noise(rng, len(t), lambda f: band(f, 300.0, 3000.0, 1))
        dt = axis(0.2)
        for _ in range(int(180 * L)):
            drop = np.zeros_like(dt)
            for f in rng.choice(panel, 3, replace=False):
                drop += np.sin(TAU * f * rng.uniform(0.97, 1.03) * dt) * np.exp(-dt / rng.uniform(0.005, 0.025))
            drop[:24] += rng.standard_normal(24) * 0.5
            place(hits, rng.uniform(0, L) * SR, drop * ramp(dt, 0.0003) * min(3.0, rng.pareto(2.2) + 0.1) * 0.12)
        bt = axis(0.6)
        for _ in range(int(8 * L)):
            boom = sum(np.sin(TAU * f * bt) * np.exp(-bt / 0.15) for f in rng.choice(lows, 2, replace=False))
            place(hits, rng.uniform(0, L) * SR, boom * ramp(bt, 0.002) * rng.uniform(0.05, 0.15))
        drip = bubble(rng, 1200.0 + 300 * side, 0.03)
        every = L / round(L / (0.8 + 0.15 * side))
        for k in range(round(L / every)):
            place(hits, (k * every + 0.3 * side) * SR, drip * rng.uniform(0.15, 0.25))
        roof.append(hits)
    write_np("rain_tin_roof.wav", strip_dc_circular(roof[0]), strip_dc_circular(roof[1]))

    rng = rng_for("underwater")
    surge = 0.65 + 0.35 * np.sin(TAU * snap(0.09, L) * t) * (0.8 + 0.2 * slow(rng, len(t), 0.2))
    under = noise(rng, len(t), lambda f: band(f, 20.0, 250.0, 1) * (60.0 / np.maximum(f, 20.0)) ** 0.5) * surge
    under += 0.1 * noise(rng, len(t), lambda f: band(f, 300.0, 900.0)) * surge
    for stream_k in range(3):
        burst_at = rng.uniform(0, L)
        for _ in range(rng.integers(25, 45)):
            f0 = rng.uniform(250, 900)
            place(under, (burst_at + rng.exponential(0.6)) * SR, bubble(rng, f0, rng.uniform(0.02, 0.06)) * rng.uniform(0.1, 0.4))
    for _ in range(int(4 * L)):
        place(under, rng.uniform(0, L) * SR, rng.standard_normal(6) * 0.15)
    under = filt(under, lambda f: lowpass(f, 2000.0), circular=True)
    under += 0.4 * convolve(under, room(rng, 2.0, 1200.0), circular=True)
    write_np("underwater.wav", strip_dc_circular(under))

    rng = rng_for("wood knock")
    t = axis(1.5)
    f0 = hz(74)
    wood = modal(t, f0, [(1.0, 1.0, 0.09), (2.32, 0.5, 0.04), (3.9, 0.3, 0.025), (5.4, 0.15, 0.015)]) * ramp(t, 0.0004)
    wood += burst(rng, t, 0.025, lambda f: lowpass(f, 250.0), 0.25) + burst(rng, t, 0.001, lambda f: band(f, 2000.0, 6000.0), 0.15)
    oneshot("wood_knock.wav", wood, 0.3)

    rng = rng_for("bowl strike")
    t = axis(7.0)
    f0 = hz(62)
    brass = modal(t, f0, [(1.0, 1.0, 5.5), (1.0, 0.5, 5.5, 1.1), (2.74, 0.6, 3.0), (2.74, 0.3, 3.0, 2.3), (5.2, 0.35, 1.4), (5.2, 0.2, 1.4, 3.1),
                          (8.3, 0.18, 0.6), (11.9, 0.09, 0.3), (16.0, 0.04, 0.15)])
    brass = brass * ramp(t, 0.0006) + burst(rng, t, 0.002, lambda f: band(f, 1500.0, 9000.0), 0.12)
    oneshot("bowl_strike.wav", brass, 1.0)

    rng = rng_for("piano harmonic")
    t = axis(6.0)
    B = 0.0003
    f1 = hz(60) / (2 * math.sqrt(1 + 4 * B))
    harmonic = np.zeros_like(t)
    for n, amp, decay in [(2, 1.0, 5.0), (4, 0.35, 3.0), (6, 0.15, 1.8), (8, 0.06, 1.0), (10, 0.03, 0.6)]:
        fn = n * f1 * math.sqrt(1 + B * n * n)
        harmonic += amp * (0.6 * np.exp(-t / 0.4) + 0.4 * np.exp(-t / decay)) * np.sin(TAU * fn * t)
    for n in (1, 3, 5):
        harmonic += 0.02 * np.exp(-t / 0.15) * np.sin(TAU * n * f1 * math.sqrt(1 + B * n * n) * t)
    harmonic = harmonic * ramp(t, 0.003) + burst(rng, t, 0.006, lambda f: lowpass(f, 500.0), 0.05)
    oneshot("piano_harmonic.wav", harmonic, 0.6)

    rng = rng_for("metal scrape")
    t = axis(3.5)
    pressure = ramp(t, 0.3) * np.clip((2.8 - t) / 0.6, 0, 1)
    rate = 60 + 160 * np.sin(math.pi * np.clip(t / 2.6, 0, 1)) + 20 * slow(rng, len(t), 8.0)
    ph = np.cumsum(rate) / SR
    pulses = np.diff(np.floor(ph), prepend=0.0) * (0.6 + 0.4 * rng.random(len(t)))
    excitation = (pulses + 0.08 * rng.standard_normal(len(t))) * pressure
    modes_f = np.exp(rng.uniform(math.log(300), math.log(8000), 40))
    ring = axis(1.5)
    ir = sum(np.sin(TAU * f * ring) * np.exp(-ring / rng.uniform(0.3, 1.5)) * (300.0 / f) ** 0.5 for f in modes_f)
    scrape = convolve(excitation, ir)
    oneshot("metal_scrape.wav", tame(scrape, 6.0), 0.5)

    rng = rng_for("breath swell")
    t = axis(4.5)
    shape = np.clip(t / 3.0, 0, 1) ** 2 * np.clip((4.5 - t) / 1.5, 0, 1) ** 1.5
    dark = noise(rng, len(t), lambda f: lowpass(f, 900.0) + bump(f, 500.0, 150.0, 0.8))
    bright = noise(rng, len(t), lambda f: band(f, 2000.0, 6000.0) + bump(f, 1500.0, 300.0, 0.6) + bump(f, 2500.0, 400.0, 0.4))
    oneshot("breath_swell.wav", (dark * (1 - 0.6 * shape) + bright * shape) * shape, 0.05)

    rng = rng_for("vocal swell")
    t = axis(5.0)
    effort = np.clip(t / 3.5, 0, 1) ** 1.5 * np.clip((5.0 - t) / 1.3, 0, 1)
    f0 = hz(57)
    vibrato = 0.006 * np.clip((t - 1.2) / 1.0, 0, 1) * np.sin(TAU * 5.4 * t) + 0.0015 * slow(rng, len(t), 6.0)
    ah = formants([(700.0, 80.0, 1.0), (1100.0, 90.0, 0.5), (2600.0, 140.0, 0.25), (3300.0, 200.0, 0.1)])
    tilt = 1.8 - 0.7 * effort
    voice = harmonics(phase_of(f0 * (1 + vibrato)), f0, lambda h, fh: ah(fh) * h ** -tilt)
    voice += 0.06 * rms(voice) * noise(rng, len(t), lambda f: ah(f) + 0.1 * band(f, 4000.0, 8000.0))
    oneshot("vocal_swell.wav", voice * effort, 0.05)

    rng = rng_for("felt mallet")
    t = axis(4.0)
    f0 = hz(48)
    felt = modal(t, f0, [(1.0, 1.0, 1.6), (3.99, 0.12, 0.25), (9.0, 0.03, 0.06)]) * ramp(t, 0.008)
    felt += burst(rng, t, 0.03, lambda f: lowpass(f, 200.0), 0.2)
    oneshot("felt_mallet.wav", felt, 0.4)

    rng = rng_for("hang drum")
    t = axis(6.0)
    f0 = hz(57)
    hang = modal(t, f0, [(1.0, 1.0, 3.4), (1.0, 0.3, 3.4, 0.45), (2.0, 0.42, 2.3), (2.0, 0.18, 2.3, 0.8), (3.0, 0.2, 1.4), (3.0, 0.08, 1.4, 1.2),
                         (4.08, 0.05, 0.45)])
    hang = hang * ramp(t, 0.004) + 0.12 * np.exp(-t / 0.7) * np.sin(TAU * 92.0 * t) * ramp(t, 0.01)
    hang += burst(rng, t, 0.006, lambda f: lowpass(f, 900.0), 0.07)
    oneshot("hang_drum.wav", hang, 0.4)

    rng = rng_for("glockenspiel")
    t = axis(4.0)
    f0 = hz(84)
    glock = modal(t, f0, [(1.0, 1.0, 2.6), (1.0, 0.15, 2.6, 0.6), (2.71, 0.32, 0.8), (5.15, 0.16, 0.3), (8.43, 0.05, 0.1)]) * ramp(t, 0.0004)
    glock += burst(rng, t, 0.0012, lambda f: band(f, 2500.0, 10000.0), 0.1)
    oneshot("glockenspiel.wav", glock, 0.3)

    rng = rng_for("dulcimer")
    t = axis(5.0)
    course = sum(plucked(t, hz(62) * 2 ** (c / 1200), 0.11, 0.9, 3.2, 0.008, 2e-5) for c in (-1.6, 0.0, 1.9))
    course = filt(course, lambda f: 0.5 + bump(f, 260.0, 70.0, 0.6) + bump(f, 700.0, 200.0, 0.5) + bump(f, 2600.0, 700.0, 0.3))
    course += burst(rng, t, 0.0015, lambda f: band(f, 600.0, 6000.0), 0.12)
    oneshot("dulcimer.wav", course * ramp(t, 0.0006), 0.4)

    rng = rng_for("prepared piano")
    t = axis(5.0)
    f0 = hz(55)
    B = 0.0025
    strings = modal(t, f0, [(n * math.sqrt(1 + B * n * n), 0.9 / n ** 1.3, 2.2 / n ** 0.5) for n in range(1, 17)])
    bolt = modal(t, f0, [(3.37, 0.3, 0.6), (5.81, 0.2, 0.35), (7.93, 0.12, 0.2), (11.4, 0.06, 0.1)])
    rattle = 0.15 * noise(rng, len(t), lambda f: band(f, 2500.0, 7000.0)) * np.maximum(0, np.sin(TAU * f0 * t)) ** 4 * np.exp(-t / 0.5)
    prepared = (strings + bolt) * ramp(t, 0.003) + rattle + burst(rng, t, 0.01, lambda f: lowpass(f, 600.0), 0.06)
    oneshot("prepared_piano.wav", prepared, 0.4)

    rng = rng_for("gong")
    t = axis(9.0)
    f0 = hz(45)
    gong = modal(t, f0, [(1.0, 1.0, 7.5), (1.0, 0.5, 7.5, 0.35), (2.0, 0.32, 4.5), (2.0, 0.2, 4.5, 0.7), (2.93, 0.24, 3.2), (4.12, 0.14, 2.0)])
    wash = np.zeros_like(t)
    for ratio in np.sort(rng.uniform(5.0, 34.0, 16)):
        wash += (ratio / 5.0) ** -0.8 * np.sin(TAU * f0 * ratio * t + rng.uniform(0, TAU))
    gong += 0.35 * wash * (1 - np.exp(-t / 0.7)) * np.exp(-t / 2.4)
    gong = gong * ramp(t, 0.006) + burst(rng, t, 0.05, lambda f: lowpass(f, 160.0), 0.3)
    oneshot("gong.wav", gong, 1.8)

    rng = rng_for("lyre")
    t = axis(5.0)
    lyre = plucked(t, hz(57), 0.28, 1.9, 3.8, 0.012, 8e-6)
    lyre = filt(lyre, lambda f: 0.4 + bump(f, 240.0, 70.0, 0.7) + bump(f, 520.0, 140.0, 0.5) + bump(f, 1300.0, 400.0, 0.2))
    lyre += burst(rng, t, 0.004, lambda f: lowpass(f, 1800.0), 0.04)
    oneshot("lyre.wav", lyre * ramp(t, 0.003), 0.4)

    rng = rng_for("bowed vibraphone")
    t = axis(7.0)
    f0 = hz(65)
    pressure = 1 + 0.05 * slow(rng, len(t), 1.5)
    bar = np.sin(TAU * f0 * t) + 0.07 * np.sin(TAU * 4 * f0 * t + 0.6) + 0.015 * np.sin(TAU * 10 * f0 * t + 1.3)
    rosin = 0.04 * noise(rng, len(t), lambda f: bump(f, f0, 5.0, 1.0) + bump(f, 4 * f0, 12.0, 0.3))
    rosin += 0.006 * noise(rng, len(t), lambda f: band(f, 2000.0, 6000.0))
    oneshot("bowed_vibraphone.wav", (bar + rosin) * pressure * edge(t, 0.9, 2.4), 0.1)

    rng = rng_for("drifting pad")
    t = axis(7.0)
    sweep = 700 + 2300 * (0.5 + 0.5 * slow(rng, len(t), 0.25)) * ramp(t, 3.0)
    drifting = np.zeros_like(t)
    for n in (48, 55, 62, 64):
        for d in (-9.0, 0.0, 8.0):
            f = hz(n) * 2 ** (d / 1200)
            drifting += pad_voice(rng, t, f, 0.0, 0.0, lambda h, fh: lowpass(fh, sweep, 2) / h if fh < 10000 else 0.0, 0.0015)
    drifting += 0.02 * rms(drifting) * noise(rng, len(t), lambda f: band(f, 3000.0, 9000.0))
    sustained("drifting_pad.wav", drifting * edge(t, 1.8, 2.0))

    rng = rng_for("frost")
    t = axis(7.0)
    frost = np.zeros_like(t)
    for n in (64, 71, 76, 83):
        f = detuned(rng, hz(n), 4.0)
        index = 0.2 + 0.9 * (0.5 + 0.5 * slow(rng, len(t), 0.3))
        ph = phase_of(f * (1 + 0.0008 * slow(rng, len(t), 0.5)))
        frost += np.sin(ph + index * np.sin(2 * ph + rng.uniform(0, TAU))) * (0.6 + 0.4 * slow(rng, len(t), 0.2)) ** 2
    sparkle = np.zeros_like(t)
    gt = axis(0.25)
    for _ in range(140):
        n = (88, 95, 100, 107)[rng.integers(4)]
        place(sparkle, rng.uniform(0.3, 6.5) * SR, np.sin(TAU * hz(n) * gt) * np.sin(math.pi * gt / 0.25) ** 2 * rng.uniform(0.1, 0.5))
    frost += 0.25 * sparkle + 0.01 * noise(rng, len(t), lambda f: band(f, 6000.0, 12000.0))
    sustained("frost.wav", frost * edge(t, 2.0, 2.0))

    rng = rng_for("hollow fifths")
    t = axis(7.0)
    centre = 500 + 900 * (0.5 - 0.5 * np.cos(TAU * t / 7.0)) + 150 * slow(rng, len(t), 0.4)
    hollow = np.zeros_like(t)
    for n in (43, 50, 55, 62):
        for d in (-5.0, 5.0):
            f = hz(n) * 2 ** (d / 1200)
            hollow += pad_voice(rng, t, f, 0.0, 0.0,
                                lambda h, fh: (1.0 / h if h % 2 else 0.12 / h) * (0.25 + resonant(fh, centre, 2.5)) * lowpass(fh, 5000.0) if fh < 9000 else 0.0,
                                0.001)
    sustained("hollow_fifths.wav", hollow * edge(t, 1.5, 2.0))

    rng = rng_for("vowel morph")
    t = axis(7.0)
    u = np.clip((t - 0.8) / 5.4, 0, 1)
    shape = formants([(np.interp(u, [0, 0.5, 1], [320.0, 730.0, 290.0]), 70.0, 1.0),
                      (np.interp(u, [0, 0.5, 1], [800.0, 1100.0, 2250.0]), 90.0, np.interp(u, [0, 0.5, 1], [0.35, 0.5, 0.3])),
                      (np.interp(u, [0, 0.5, 1], [2300.0, 2500.0, 2950.0]), 130.0, 0.12), (3300.0, 200.0, 0.05)])
    morphing = np.zeros_like(t)
    for n in (52, 59, 64, 68):
        for _ in range(3):
            f = detuned(rng, hz(n), 6.0)
            morphing += pad_voice(rng, t, f, rng.uniform(4.8, 5.6), 0.003, lambda h, fh: shape(fh) / h ** 0.7 if fh < 6000 else 0.0, 0.0012,
                                  rng.uniform(0.6, 1.6))
    morphing += 0.05 * rms(morphing) * noise(rng, len(t), lambda f: band(f, 500.0, 3500.0))
    sustained("vowel_morph.wav", morphing * edge(t, 1.6, 1.8))

    rng = rng_for("midnight pad")
    t = axis(7.0)
    glow = 650 + 450 * (0.5 + 0.5 * slow(rng, len(t), 0.2))
    midnight = np.zeros_like(t)
    for n in (45, 52, 60, 67, 71):
        for d in (-6.0, 6.0):
            f = hz(n) * 2 ** (d / 1200)
            midnight += pad_voice(rng, t, f, 0.0, 0.0, lambda h, fh: (h ** -2.0 if h % 2 else 0.05 / h) * lowpass(fh, glow, 2) if fh < 5000 else 0.0, 0.0012)
    midnight += 0.4 * rms(midnight) * np.sin(TAU * hz(33) * t)
    midnight *= 1 + 0.1 * np.sin(TAU * 0.11 * t)
    sustained("midnight_pad.wav", midnight * edge(t, 2.2, 2.2))

    L = 10.0
    t = axis(L)

    rng = rng_for("shruti box")
    pump = 1 + 0.12 * np.sin(TAU * snap(0.4, L) * t) + 0.04 * slow(rng, len(t), 0.6)
    shruti = np.zeros_like(t)
    for n, gain in [(48, 1.0), (55, 0.7), (60, 0.55)]:
        for beat in (-0.15, 0.15):
            f = snap(hz(n) + beat, L)
            reed = lambda h, fh: h ** -0.8 * lowpass(fh, 2800.0) * (1 + bump(fh, 1100.0, 300.0, 0.6)) if fh < 9000 else 0.0
            shruti += gain * harmonics(TAU * f * t + rng.uniform(0, TAU), f, reed)
    shruti = shruti * pump + 0.02 * rms(shruti) * noise(rng, len(t), lambda f: band(f, 600.0, 5000.0)) * pump
    sustained("shruti_box.wav", shruti)

    rng = rng_for("bowed glass")
    rubbed = np.zeros_like(t)
    for n, gain in [(67, 1.0), (74, 0.55), (79, 0.3)]:
        f = snap(hz(n), L)
        swell = (0.55 + 0.45 * slow(rng, len(t), 0.2)) ** 2
        tone = np.sin(TAU * f * t + rng.uniform(0, TAU)) + 0.1 * np.sin(TAU * 2 * f * t) + 0.03 * np.sin(TAU * 3 * f * t)
        rubbed += gain * swell * (tone + 0.04 * noise(rng, len(t), lambda fr, f=f: bump(fr, f, 5.0, 1.0)))
    flutter = 1 + 0.04 * np.sin(TAU * snap(0.5, L) * t) + 0.03 * slow(rng, len(t), 3.0)
    rubbed = rubbed * flutter + 0.004 * noise(rng, len(t), lambda f: band(f, 5000.0, 11000.0))
    sustained("bowed_glass.wav", rubbed)

    rng = rng_for("overtone choir")
    whistle = 700 + 500 * (0.5 - 0.5 * np.cos(TAU * snap(0.1, L) * t)) + 200 * slow(rng, len(t), 0.3)
    vowel = formants([(600.0, 80.0, 1.0), (1000.0, 100.0, 0.4), (2400.0, 150.0, 0.15)])
    overtone = np.zeros_like(t)
    for k, (n, gain) in enumerate([(45, 1.0), (45, 0.8), (45, 0.7), (52, 0.5), (57, 0.35)]):
        f = snap(hz(n) + 0.15 * k * (-1) ** k, L)
        voice = lambda h, fh: h ** -0.8 * (vowel(fh) + 0.12 * resonant(fh, whistle, 14.0)) if fh < 5000 else 0.0
        overtone += gain * harmonics(TAU * f * t + rng.uniform(0, TAU), f, voice)
    overtone *= 1 + 0.05 * slow(rng, len(t), 0.5)
    overtone += 0.04 * rms(overtone) * noise(rng, len(t), lambda f: vowel(f) * lowpass(f, 3000.0))
    sustained("overtone_choir.wav", overtone)

    rng = rng_for("granular hum")
    f0 = snap(hz(40), L)
    hum = harmonics(TAU * f0 * t, f0, lambda h, fh: h ** -1.4 if fh < 3000 else 0.0)
    grains = np.zeros_like(t)
    choices = (1, 2, 3, 4, 5, 6, 8, 10, 12)
    for _ in range(int(140 * L)):
        h = choices[rng.integers(len(choices))]
        length = rng.uniform(0.04, 0.16)
        gt = axis(length)
        f = h * f0 * 2 ** (rng.normal(0, 6) / 1200)
        grain = np.sin(TAU * f * gt + rng.uniform(0, TAU)) * np.sin(math.pi * gt / length) ** 2 * h ** -0.7 * rng.uniform(0.3, 1.0)
        place(grains, rng.uniform(0, L) * SR, grain)
    granular = (0.35 * hum / rms(hum) + 1.2 * grains / rms(grains)) * (1 + 0.15 * slow(rng, len(t), 0.3))
    sustained("granular_hum.wav", granular)

    rng = rng_for("hurdy gurdy")
    wheel = 1 + 0.07 * slow(rng, len(t), 0.5) + 0.03 * np.sin(TAU * snap(1.3, L) * t)
    gbody = lambda fh: (0.4 + bump(fh, 300.0, 90.0, 0.8) + bump(fh, 750.0, 180.0, 0.6) + bump(fh, 2100.0, 500.0, 0.4)) * lowpass(fh, 5000.0)
    gurdy = np.zeros_like(t)
    for n, gain in [(31, 0.8), (43, 1.0), (50, 0.6)]:
        f = snap(hz(n), L)
        gurdy += gain * harmonics(TAU * f * t + rng.uniform(0, TAU), f, lambda h, fh: gbody(fh) / h if fh < 8000 else 0.0)
    gurdy *= wheel
    f = snap(hz(43), L)
    rattle = filt(np.tanh(6 * np.sin(TAU * f * t)), lambda fr: band(fr, 1200.0, 7000.0), circular=True)
    gate = np.zeros_like(t)
    strokes = 16
    for k in range(strokes):
        s = int(k * L / strokes * SR)
        gate[s : s + int((0.14 if k % 2 == 0 else 0.07) * SR)] = 1.0 if k % 2 == 0 else 0.6
    gate = filt(gate, lambda fr: lowpass(fr, 40.0, 1), circular=True)
    gurdy += 0.25 * rms(gurdy) / rms(rattle) * rattle * gate * wheel
    sustained("hurdy_gurdy.wav", gurdy)

    rng = rng_for("analog drone")
    cutoff = 400 + 1600 * (0.5 - 0.5 * np.cos(TAU * snap(0.1, L) * t)) ** 1.5 + 200 * slow(rng, len(t), 0.3)
    synth = np.zeros_like(t)
    for n, gain in [(38, 1.0), (45, 0.7), (50, 0.55), (57, 0.35)]:
        for d in (-0.2, 0.2):
            f = snap(hz(n) + d, L)
            saw = lambda h, fh: resonant(fh, cutoff, 1.8) * lowpass(fh, 1.5 * cutoff, 1) / h if fh < 6000 else 0.0
            synth += gain * harmonics(TAU * f * t + rng.uniform(0, TAU), f, saw)
    synth = np.tanh(1.2 * synth / (2.5 * rms(synth)))
    sustained("analog_drone.wav", synth)

    L = 12.0
    t = axis(L)

    rng = rng_for("low brass")
    effort = 0.3 + 0.7 * (0.5 - 0.5 * np.cos(TAU * snap(1 / 6.0, L) * t)) ** 1.3 * (0.9 + 0.1 * slow(rng, len(t), 0.5))
    brass = np.zeros_like(t)
    for n, gain in [(34, 1.0), (41, 0.45), (46, 0.3)]:
        for beat in (-0.1, 0.1):
            f = snap(hz(n) + beat, L)
            bell = lambda h, fh: h ** (-2.4 + 1.5 * effort) * lowpass(fh, 3500.0) * (1 + bump(fh, 600.0, 250.0, 0.8)) if fh < 7000 else 0.0
            brass += gain * harmonics(TAU * f * t + rng.uniform(0, TAU), f, bell)
    brass *= effort
    brass += 0.015 * rms(brass) * noise(rng, len(t), lambda f: band(f, 200.0, 2500.0)) * effort
    sustained("low_brass.wav", brass)

    L = 10.0
    t = axis(L)

    rng = rng_for("snowfall")
    hush = noise(rng, len(t), lambda f: band(f, 40.0, 500.0, 1)) * (0.6 + 0.4 * slow(rng, len(t), 0.1))
    flakes = np.zeros_like(t)
    ft = axis(0.004)
    for _ in range(int(400 * L)):
        place(flakes, rng.uniform(0, L) * SR, rng.standard_normal(len(ft)) * np.exp(-ft / 0.0006) * rng.uniform(0.02, 0.12))
    flakes = filt(flakes, lambda f: band(f, 2500.0, 9000.0, 1), circular=True)
    thumps = np.zeros_like(t)
    bt = axis(0.9)
    for _ in range(3):
        fall = filt(rng.standard_normal(len(bt)) * np.exp(-bt / 0.18) * ramp(bt, 0.03), lambda f: lowpass(f, 350.0, 2))
        place(thumps, rng.uniform(0, L) * SR, fall * rng.uniform(0.5, 1.0))
    snow = 0.25 * hush + 0.05 * flakes / rms(flakes) + 0.6 * thumps / max(1e-9, float(np.max(np.abs(thumps))))
    snow += 0.3 * convolve(snow, room(rng, 1.2, 3000.0), circular=True)
    write_np("snowfall.wav", strip_dc_circular(snow))

    rng = rng_for("cave drips")
    cave = 0.02 * noise(rng, len(t), lambda f: band(f, 25.0, 140.0)) * (0.7 + 0.3 * slow(rng, len(t), 0.15))
    du = axis(0.12)
    for _ in range(5):
        f0 = rng.uniform(700, 1900)
        count = max(1, round(L / rng.uniform(0.9, 2.6)))
        offset = rng.uniform(0, L)
        for k in range(count):
            drop = np.sin(phase_of(f0 * (1 + 1.2 * (1 - np.exp(-du / 0.015))))) * np.exp(-du / rng.uniform(0.02, 0.045)) * ramp(du, 0.0004)
            place(cave, (offset + k * L / count + rng.normal(0, 0.03)) * SR, drop * rng.uniform(0.3, 0.8))
    cave += 1.2 * convolve(cave, room(rng, 3.2, 2500.0, 0.04), circular=True)
    write_np("cave_drips.wav", strip_dc_circular(tame(cave, 6.0)))

    rng = rng_for("harbour")
    waves = 0.5 + 0.5 * np.sin(TAU * snap(0.4, L) * t)
    st = axis(0.6)
    ct = axis(0.08)
    timber = modal(ct, 1.0, [(420.0, 1.0, 0.02), (1130.0, 0.6, 0.012), (2400.0, 0.3, 0.006)])
    harbour = []
    for side in range(2):
        lap = 0.3 * noise(rng, len(t), lambda f: band(f, 120.0, 1400.0, 1)) * (0.25 + 0.75 * np.roll(waves, side * 4000) ** 3)
        for k in range(round(0.4 * L)):
            slosh = filt(rng.standard_normal(len(st)) * np.exp(-st / 0.15) * ramp(st, 0.05), lambda f: band(f, 300.0, 2500.0))
            place(lap, (k * 2.5 + 0.55 + 0.1 * side + rng.normal(0, 0.05)) * SR, 0.5 * slosh)
        harbour.append(lap)
    for _ in range(4):
        length = rng.uniform(0.6, 1.2)
        cu = axis(length)
        rate = 25 + 50 * ramp(cu, length) + 6 * np.sin(TAU * 3.0 * cu)
        pulses = np.diff(np.floor(np.cumsum(rate) / SR), prepend=0.0)
        creak = convolve(pulses, timber) * edge(cu, 0.1, 0.2)
        pan = rng.uniform(0.2, 0.8)
        at = rng.uniform(0, L) * SR
        place(harbour[0], at, 0.4 * creak * math.sqrt(1 - pan))
        place(harbour[1], at, 0.4 * creak * math.sqrt(pan))
    ring = axis(4.0)
    buoy = filt(modal(ring, 610.0, [(1.0, 1.0, 1.8), (2.0, 0.3, 1.2), (2.76, 0.35, 0.9), (5.4, 0.12, 0.4)]) * ramp(ring, 0.001), lambda f: lowpass(f, 2000.0, 1))
    for at in (1.7, 6.4):
        place(harbour[0], at * SR, 0.08 * buoy)
        place(harbour[1], at * SR, 0.18 * buoy)
    harbour = [strip_dc_circular(c + 0.3 * convolve(c, room(rng, 1.5, 3000.0), circular=True)) for c in harbour]
    write_np("harbour.wav", harbour[0], harbour[1])

    rng = rng_for("rain on glass")
    pane = np.exp(rng.uniform(math.log(1800), math.log(6000), 12))
    outside = 0.12 * noise(rng, len(t), lambda f: band(f, 200.0, 2000.0, 1))
    dt = axis(0.05)
    window = []
    for side in range(2):
        taps = outside + 0.04 * noise(rng, len(t), lambda f: band(f, 300.0, 1500.0, 1))
        for _ in range(int(35 * L)):
            tap = np.zeros_like(dt)
            for f in rng.choice(pane, 2, replace=False):
                tap += np.sin(TAU * f * dt) * np.exp(-dt / rng.uniform(0.002, 0.006))
            tap[:12] += rng.standard_normal(12) * 0.4
            place(taps, rng.uniform(0, L) * SR, tap * min(3.0, rng.pareto(2.5) + 0.1) * 0.15)
        for _ in range(3):
            at = rng.uniform(0, L)
            for j in range(rng.integers(8, 16)):
                place(taps, (at + j * rng.uniform(0.03, 0.08)) * SR, bubble(rng, rng.uniform(1500, 3000), 0.004) * rng.uniform(0.05, 0.15))
        window.append(taps)
    window = [strip_dc_circular(c + 0.2 * convolve(c, room(rng, 0.5, 4000.0), circular=True)) for c in window]
    write_np("rain_glass.wav", window[0], window[1])

    rng = rng_for("pine wind")
    gusts = 0.5 + 0.5 * slow(rng, len(t), 0.12)
    needles = noise(rng, len(t), lambda f: band(f, 1500.0, 7000.0, 1) * (1 + bump(f, 3500.0, 1200.0, 0.6))) * (0.15 + gusts ** 2)
    roar = noise(rng, len(t), lambda f: band(f, 60.0, 600.0, 1)) * (0.3 + 0.7 * gusts)
    pine = 0.35 * needles * (1 + 0.25 * slow(rng, len(t), 6.0)) + 0.5 * roar
    for _ in range(2):
        length = rng.uniform(0.8, 1.6)
        cu = axis(length)
        pulses = np.diff(np.floor(np.cumsum(8 + 12 * ramp(cu, length)) / SR), prepend=0.0)
        trunk = convolve(pulses, modal(axis(0.15), 1.0, [(rng.uniform(150, 300), 1.0, 0.04), (rng.uniform(500, 900), 0.4, 0.02)])) * edge(cu, 0.2, 0.3)
        place(pine, rng.uniform(0, L) * SR, 0.6 * trunk)
    write_np("pine_wind.wav", strip_dc_circular(pine))

    rng = rng_for("frozen lake")
    lake = 0.15 * noise(rng, len(t), lambda f: band(f, 100.0, 1200.0, 1)) * (0.6 + 0.4 * slow(rng, len(t), 0.2))
    for _ in range(9):
        length = rng.uniform(0.25, 0.7)
        pu = axis(length)
        ping = np.sin(phase_of(150 + rng.uniform(1500, 4500) * np.exp(-pu / (length / 4)))) * np.exp(-pu / (length / 3)) * ramp(pu, 0.001)
        ping *= rng.uniform(0.4, 1.0)
        at = rng.uniform(0, L)
        for k in range(3):
            place(lake, (at + k * 0.11) * SR, ping * 0.5 ** k)
    for _ in range(2):
        gl = axis(rng.uniform(1.5, 2.5))
        fc = rng.uniform(60, 110)
        place(lake, rng.uniform(0, L) * SR, 0.5 * noise(rng, len(gl), lambda f: bump(f, fc, 20.0, 1.0)) * edge(gl, 0.4, 0.8))
    lake += 0.4 * convolve(lake, room(rng, 2.0, 3000.0), circular=True)
    write_np("frozen_lake.wav", strip_dc_circular(tame(lake, 6.0)))

    L = 12.0
    t = axis(L)

    rng = rng_for("distant bells")
    peal = np.zeros_like(t)
    tower = (64, 62, 60, 55)
    bt = axis(6.0)
    for r, row in enumerate([(0, 1, 2, 3), (0, 1, 2, 3), (1, 0, 3, 2), (1, 3, 0, 2)]):
        for k, b in enumerate(row):
            strike = modal(bt, hz(tower[b]), [(0.5, 0.6, 5.0), (1.0, 1.0, 3.0), (1.2, 0.5, 2.2), (1.5, 0.35, 1.8), (2.0, 0.45, 1.4), (2.5, 0.15, 0.8),
                                              (3.0, 0.1, 0.6)])
            place(peal, ((r * 4 + k) * 0.75 + rng.normal(0, 0.02)) * SR, strike * ramp(bt, 0.002) * rng.uniform(0.7, 1.0))
    peal = filt(peal, lambda f: lowpass(f, 1800.0, 2) * highpass(f, 120.0), circular=True)
    peal += 0.6 * convolve(peal, room(rng, 2.5, 1500.0, 0.05), circular=True)
    air = noise(rng, len(t), lambda f: band(f, 60.0, 2500.0, 1)) * (0.6 + 0.4 * slow(rng, len(t), 0.15))
    write_np("distant_bells.wav", strip_dc_circular(peal / rms(peal) + 0.35 * air))

    rng = rng_for("reverse bell")
    t = axis(4.0)
    f0 = hz(69)
    struck = modal(t, f0, [(1.0, 1.0, 1.6), (1.0, 0.4, 1.6, 0.8), (2.0, 0.3, 1.0), (2.76, 0.35, 0.7), (5.4, 0.15, 0.3), (8.9, 0.06, 0.15)])
    struck = struck * ramp(t, 0.001) + burst(rng, t, 0.003, lambda f: band(f, 1500.0, 8000.0), 0.08)
    oneshot("reverse_bell.wav", struck[::-1].copy(), 0.02)

    rng = rng_for("bowed cymbal")
    t = axis(6.0)
    pressure = ramp(t, 1.2) * np.clip((5.0 - t) / 1.5, 0, 1)
    ring = axis(2.0)
    ir = sum(np.sin(TAU * f * ring + rng.uniform(0, TAU)) * np.exp(-ring / rng.uniform(0.4, 1.8)) * (500.0 / f) ** 0.4
             for f in np.exp(rng.uniform(math.log(400), math.log(9000), 30)))
    cymbal = convolve(noise(rng, len(t), lambda f: band(f, 300.0, 8000.0)) * pressure, ir)
    sing = sum(a * np.sin(TAU * f * t + rng.uniform(0, TAU)) for f, a in [(1873.0, 1.0), (2931.0, 0.6), (4410.0, 0.3)])
    sing *= np.clip((t - 1.0) / 2.0, 0, 1) ** 2 * pressure * (1 + 0.1 * slow(rng, len(t), 2.0))
    oneshot("bowed_cymbal.wav", cymbal / rms(cymbal) + 0.8 * sing / rms(sing), 0.8)

    rng = rng_for("rain stick")
    t = axis(5.0)
    flow = ramp(t, 0.4) * np.exp(-np.maximum(0, t - 0.6) / 1.4)
    stick = np.zeros_like(t)
    pt = axis(0.03)
    for _ in range(2200):
        at = rng.uniform(0, 4.6)
        if rng.random() < flow[int(at * SR)]:
            click = np.sin(TAU * rng.uniform(2000, 7000) * pt) * np.exp(-pt / rng.uniform(0.0015, 0.005)) * rng.uniform(0.2, 1.0)
            place(stick, at * SR, click)
    stick = filt(stick, lambda f: 0.4 + bump(f, 3200.0, 900.0, 0.8) + bump(f, 5500.0, 1200.0, 0.4))
    stick += 0.04 * noise(rng, len(t), lambda f: band(f, 2000.0, 8000.0)) * flow
    stick += 0.2 * convolve(stick, room(rng, 0.4, 6000.0))
    oneshot("rain_stick.wav", stick, 0.5)

    rng = rng_for("breath flute")
    t = axis(4.5)
    f0 = hz(67)
    blow = np.clip(t / 0.8, 0, 1) ** 1.2 * np.clip((4.5 - t) / 1.4, 0, 1)
    vibrato = 0.004 * np.clip((t - 1.0) / 1.0, 0, 1) * np.sin(TAU * 5.0 * t) + 0.001 * slow(rng, len(t), 4.0)
    ph = phase_of(f0 * (1 + vibrato))
    flute = np.sin(ph) + 0.25 * np.sin(2 * ph + 0.3) + 0.08 * np.sin(3 * ph + 0.9) + 0.03 * np.sin(4 * ph)
    hiss = noise(rng, len(t), lambda f: bump(f, f0, 40.0, 0.6) + bump(f, 2 * f0, 60.0, 0.3) + 0.2 * band(f, 1500.0, 8000.0))
    chiff = burst(rng, t, 0.05, lambda f: band(f, 1500.0, 6000.0), 0.15) * ramp(t, 0.01)
    oneshot("breath_flute.wav", flute * blow + 0.12 * hiss * (0.3 + blow) * np.clip((4.5 - t) / 1.4, 0, 1) + chiff, 0.1)

    rng = rng_for("sub bloom")
    t = axis(5.0)
    f0 = hz(36)
    swell = np.clip(t / 1.6, 0, 1) ** 2 * np.exp(-np.maximum(0, t - 1.6) / 1.6)
    sub = np.sin(TAU * f0 * t) + 0.3 * np.sin(TAU * 2 * f0 * t + 0.5) + 0.08 * np.sin(TAU * 3 * f0 * t + 1.0)
    sub = np.tanh(1.5 * sub * swell) + 0.004 * noise(rng, len(t), lambda f: lowpass(f, 300.0)) * swell
    oneshot("sub_bloom.wav", sub, 0.3)

if __name__ == "__main__":
    import sys

    if "--library-only" not in sys.argv:
        write("glass.wav", glass())
        write("pluck.wav", pluck())
        write("breath.wav", breath())
        write("chord.wav", chord())
    library()
    expansion()
    print("wrote", sorted(str(p.relative_to(OUT)) for p in OUT.rglob("*.wav")))
