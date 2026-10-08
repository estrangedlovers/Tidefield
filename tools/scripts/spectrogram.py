#!/usr/bin/env python3
import struct
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def read(path):
    with open(path, "rb") as f:
        data = f.read()
    pos, fmt, samples = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, ch, sr, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            fmt = (tag, ch, sr, bits)
        elif cid == b"data":
            samples = body
        pos += 8 + size + (size & 1)
    tag, ch, sr, bits = fmt
    if bits == 32 and tag in (3, 0xFFFE):
        x = np.frombuffer(samples, dtype="<f4")
    else:
        x = np.frombuffer(samples, dtype="<i2").astype(np.float32) / 32768.0
    return sr, x[: len(x) // ch * ch].reshape(-1, ch).mean(axis=1)


def main():
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".png"
    sr, x = read(src)
    fig, (a, b) = plt.subplots(2, 1, figsize=(12, 6), sharex=True, gridspec_kw={"height_ratios": [3, 1]})
    a.specgram(x + 1e-9, NFFT=4096, Fs=sr, noverlap=3072, cmap="magma", vmin=-140, vmax=-20)
    a.set_yscale("symlog", linthresh=200)
    a.set_ylim(30, sr / 2)
    a.set_ylabel("Hz")
    a.set_title(src)
    hop = sr // 10
    rms = [20 * np.log10(np.sqrt(np.mean(x[i:i + hop] ** 2)) + 1e-9) for i in range(0, len(x) - hop, hop)]
    b.plot(np.arange(len(rms)) / 10.0, rms, color="#7fb4c9")
    b.set_ylim(-90, 0)
    b.set_ylabel("dBFS")
    b.set_xlabel("seconds")
    fig.tight_layout()
    fig.savefig(dst, dpi=90)
    print(dst)


if __name__ == "__main__":
    main()
