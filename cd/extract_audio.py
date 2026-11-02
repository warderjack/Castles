#!/usr/bin/env python3
"""Save the audio track(s) of a CD image (.cue + .bin) as WAV files so you can listen to them.
usage: cd/extract_audio.py disc.cue [outdir]"""
import sys, os, re, struct
cue = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser("~/castles_audio")
os.makedirs(out, exist_ok=True)
base = os.path.dirname(os.path.abspath(cue)); text = open(cue, errors="replace").read()
tracks = []; fil = None
for line in text.splitlines():
    m = re.match(r'\s*FILE\s+"(.*)"', line)
    if m: fil = os.path.join(base, m.group(1))
    m = re.match(r'\s*TRACK\s+(\d+)\s+(\S+)', line)
    if m: tracks.append({"n": int(m.group(1)), "type": m.group(2), "file": fil, "idx": None})
    m = re.match(r'\s*INDEX\s+01\s+(\d+):(\d+):(\d+)', line)
    if m and tracks: a, b, c = map(int, m.groups()); tracks[-1]["idx"] = (a * 60 + b) * 75 + c
for i, t in enumerate(tracks):
    size = os.path.getsize(t["file"]) // 2352
    nxt = [u for u in tracks[i + 1:] if u["file"] == t["file"] and u["idx"] is not None]
    t["end"] = nxt[0]["idx"] if nxt else size
found = 0
for t in tracks:
    if t["type"].upper() != "AUDIO": continue
    found += 1; n = t["end"] - t["idx"]; fn = os.path.join(out, "track%02d.wav" % t["n"]); data = n * 2352
    with open(t["file"], "rb") as f, open(fn, "wb") as o:
        o.write(b"RIFF" + struct.pack("<I", 36 + data) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, 44100, 176400, 4, 16) + b"data" + struct.pack("<I", data))
        f.seek(t["idx"] * 2352); left = data
        while left: b = f.read(min(left, 1 << 20)); o.write(b); left -= len(b)
    print("track %d: %d:%02d long -> %s (%.0f MB)" % (t["n"], n // 75 // 60, n // 75 % 60, fn, data / 1e6))
if not found: print("no audio tracks found in", cue)
