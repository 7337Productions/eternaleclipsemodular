#!/usr/bin/env python3
"""Generate placeholder loops for the Litany Engine (res/litany/).

Stdlib only (wave, math, random, struct). Deterministic: re-running produces
byte-identical files. These are stand-ins until Nick's final branded loops
arrive -- see res/litany/README.md for the drop-in contract.

Every loop is seamless: tonal content is loop-periodic (all frequencies are an
integer number of cycles per loop), and noise/decay content is end-crossfaded.
"""

import math
import os
import random
import struct
import wave

RATE = 48000
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "res", "litany")

TWO_PI = 2.0 * math.pi


def periodic(freq, seconds):
    """Snap freq to an integer number of cycles over the loop length."""
    cycles = max(1, round(freq * seconds))
    return cycles / seconds


def crossfade_loop(samples, fade_frames):
    """Blend the tail into the head so the loop point is seamless."""
    n = len(samples)
    out = samples[:n - fade_frames]
    for i in range(fade_frames):
        t = i / fade_frames
        out[i] = samples[n - fade_frames + i] * (1.0 - t) + samples[i] * t
    return out


def normalize(l, r, peak=0.7):
    m = max(1e-9, max(max(abs(x) for x in l), max(abs(x) for x in r)))
    g = peak / m
    return [x * g for x in l], [x * g for x in r]


def write_wav(name, l, r):
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name)
    frames = bytearray()
    for a, b in zip(l, r):
        frames += struct.pack("<hh", int(max(-1.0, min(1.0, a)) * 32767),
                              int(max(-1.0, min(1.0, b)) * 32767))
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))
    print(f"  {name}: {len(l) / RATE:.2f}s, {os.path.getsize(path) // 1024} KB")


def first_utterance():
    """Detuned sine drone with slow beating, root + fifth."""
    secs = 5.0
    n = int(RATE * secs)
    f1 = periodic(110.0, secs)
    f2 = periodic(110.6, secs)
    f5 = periodic(165.0, secs)
    lfo = periodic(0.2, secs)
    l, r = [], []
    for i in range(n):
        t = i / RATE
        sway = 0.5 + 0.5 * math.sin(TWO_PI * lfo * t)
        a = 0.5 * math.sin(TWO_PI * f1 * t) + 0.35 * math.sin(TWO_PI * f2 * t)
        b = 0.3 * math.sin(TWO_PI * f5 * t) * sway
        l.append(a + 0.6 * b)
        r.append(a * 0.9 + b)
    return normalize(l, r)


def iron_psalm():
    """Sub-octave pulse train: decaying 55 Hz strikes on a slow grid."""
    secs = 4.8
    n = int(RATE * secs)
    hits = 6  # every 0.8 s -- divides the loop exactly
    f = periodic(55.0, secs)
    l, r = [0.0] * n, [0.0] * n
    for h in range(hits):
        start = int(h * n / hits)
        strong = 1.0 if h % 3 == 0 else 0.55
        for i in range(start, min(n, start + int(0.7 * RATE))):
            t = (i - start) / RATE
            env = strong * math.exp(-6.0 * t)
            s = env * math.sin(TWO_PI * f * (i / RATE))
            click = env * 0.3 * math.sin(TWO_PI * f * 4 * (i / RATE)) * math.exp(-40.0 * t)
            l[i] += s + click
            r[i] += s * 0.85 + click * 1.2
    return normalize(l, r)


def veiled_breath():
    """Amplitude-breathing filtered noise, end-crossfaded."""
    secs = 5.5
    fade = int(0.5 * RATE)
    n = int(RATE * secs) + fade
    rng = random.Random(23)
    lfo = periodic(0.36, secs)
    l, r = [], []
    lpL = lpR = 0.0
    k = 0.04  # one-pole lowpass coefficient, dark hiss
    for i in range(n):
        t = i / RATE
        breath = (0.5 + 0.5 * math.sin(TWO_PI * lfo * t)) ** 2
        lpL += k * (rng.uniform(-1, 1) - lpL)
        lpR += k * (rng.uniform(-1, 1) - lpR)
        l.append(lpL * breath)
        r.append(lpR * breath)
    return normalize(crossfade_loop(l, fade), crossfade_loop(r, fade))


def bell_of_ash():
    """Inharmonic bell partials, struck twice per loop, end-crossfaded."""
    secs = 6.0
    fade = int(0.8 * RATE)
    n = int(RATE * secs) + fade
    partials = [(220.0, 1.0, 1.2), (513.7, 0.6, 1.8), (846.2, 0.4, 2.6),
                (1291.9, 0.25, 3.5), (1718.3, 0.15, 4.5)]
    strikes = [0.0, 3.1]
    l, r = [0.0] * n, [0.0] * n
    for s0 in strikes:
        start = int(s0 * RATE)
        for i in range(start, n):
            t = (i - start) / RATE
            v = 0.0
            for f, amp, decay in partials:
                v += amp * math.exp(-decay * t) * math.sin(TWO_PI * f * t)
            l[i] += v
            r[i] += 0.8 * v * math.cos(TWO_PI * 0.7 * t)  # slow spatial wobble
    return normalize(crossfade_loop(l, fade), crossfade_loop(r, fade))


def black_meridian():
    """Low two-operator FM drone, slowly seething."""
    secs = 5.2
    n = int(RATE * secs)
    fc = periodic(65.4, secs)
    fm = periodic(32.7, secs)
    lfo = periodic(0.19, secs)
    l, r = [], []
    for i in range(n):
        t = i / RATE
        idx = 1.5 + 1.2 * math.sin(TWO_PI * lfo * t)
        m = math.sin(TWO_PI * fm * t)
        a = math.sin(TWO_PI * fc * t + idx * m)
        b = math.sin(TWO_PI * fc * t + idx * m + 0.6)
        l.append(a)
        r.append(0.5 * a + 0.5 * b)
    return normalize(l, r)


def final_orison():
    """Detuned organ-ish stack with slow vibrato, hymn-like."""
    secs = 5.4
    n = int(RATE * secs)
    voices = [(146.8, 0.0), (147.5, 0.3), (220.2, 0.6), (293.6, 0.9), (369.9, 1.4)]
    voices = [(periodic(f, secs), ph) for f, ph in voices]
    vib = periodic(0.31, secs)
    l, r = [], []
    for i in range(n):
        t = i / RATE
        depth = 0.4 + 0.6 * (0.5 + 0.5 * math.sin(TWO_PI * vib * t))
        a = b = 0.0
        for v in range(len(voices)):
            f, ph = voices[v]
            amp = (0.8 ** v) * (depth if v >= 2 else 1.0)
            s = amp * math.sin(TWO_PI * f * t + ph)
            if v % 2 == 0:
                a += s
                b += 0.7 * s
            else:
                a += 0.7 * s
                b += s
        l.append(a)
        r.append(b)
    return normalize(l, r)


LOOPS = [
    ("01-first-utterance.wav", first_utterance),
    ("02-iron-psalm.wav", iron_psalm),
    ("03-veiled-breath.wav", veiled_breath),
    ("04-bell-of-ash.wav", bell_of_ash),
    ("05-black-meridian.wav", black_meridian),
    ("06-final-orison.wav", final_orison),
]

if __name__ == "__main__":
    print("Generating Litany Engine placeholder loops:")
    for name, fn in LOOPS:
        l, r = fn()
        write_wav(name, l, r)
