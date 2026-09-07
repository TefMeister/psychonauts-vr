#!/usr/bin/env python3
"""ppak_scan.py - enumerate the contents of Psychonauts' PPAK/APAK level packs, statically.

Written 2026-09-07 for the `[PD]` PVS-leaf-size row. The board costed that row as
"parse PPAK -> locate the level binary -> walk to Octree -> two fields", and a /gr
drop re-costed it to "dump the level record with a public tool". This script exists
to establish the cheap half without any third-party tool: ENUMERATING a pack needs
about ten lines, because names are stored as `u16 length` + NUL-terminated ASCII.

What it found, and why the row's middle step is wrong (see ENGINE-DOSSIER section 9c):
  * .ppf begins `PPAK`; per-asset FourCC is stored little-endian as `CYSP` ('PSYC').
  * .apf begins `KAPA` ('APAK') and holds ANIMATIONS only (.jan).
  * ASCO.ppf: 556 name records - 279 .dds, 89 .plb, 30 .lua.
  * NOT ONE .plb is a level scene, tested by the naming convention
    levels/<level>/<level>.plb across ASCO(89) MMI1(170) NIMP(100) WWMA(213)
    common(7) = 579 records, zero matches. They are props, vehicles, overlays,
    held objects, characters and globalmodels.
  * The remaining names end in ENTITY TYPE suffixes (.domaincontroller, .splineobject,
    .teleporter, .brainjar, .figment, .ladder, ...), not file extensions.
  => the level is a serialized entity graph inside the .ppf, not a carvable level file,
     so "locate the level binary" is not a step that exists.

Reads only. Never writes to the game folder. No game content is committed - this
prints names and offsets, which are interface metadata.

Usage:
  python ppak_scan.py <file.ppf|file.apf> [--names] [--limit N]
  python ppak_scan.py <PCLevelPackFiles dir> --census
"""
import collections
import os
import struct
import sys

BACKSLASH = chr(92)
MAGICS = (b"PPAK", b"KAPA", b"MPAK", b"CYSP", b"PSIC")


def read(path):
    with open(path, "rb") as f:
        return f.read()


def name_records(d, max_len=200):
    """Every `u16 len` + NUL-terminated printable-ASCII record in the blob.

    Deliberately permissive: it does not assume a directory or a fixed record
    header, because neither has been established. It resyncs byte by byte, so a
    misparse costs recall, never correctness.
    """
    out = []
    i, n = 0, len(d)
    while i < n - 4:
        ln = struct.unpack_from("<H", d, i)[0]
        if 4 < ln < max_len and i + 2 + ln <= n:
            s = d[i + 2:i + 2 + ln]
            if s.endswith(b"\x00"):
                body = s[:-1]
                if body and all(32 <= c < 127 for c in body) and (
                        BACKSLASH.encode() in body or b"." in body):
                    out.append((i, body.decode("ascii")))
                    i += 2 + ln
                    continue
        i += 1
    return out


def report(path, show_names=False, limit=20):
    d = read(path)
    print("%s   %d bytes" % (os.path.basename(path), len(d)))
    print("  magic: %r" % d[:4])
    for m in MAGICS:
        c = d.count(m)
        if c:
            print("  fourcc %-5s x%d" % (m.decode("latin1"), c))
    recs = name_records(d)
    ext = collections.Counter(os.path.splitext(s)[1].lower() for _, s in recs)
    print("  name records: %d" % len(recs))
    print("  extensions:   %s" % ", ".join("%s x%d" % (e or "(none)", n)
                                           for e, n in ext.most_common(12)))
    plb = [(o, s) for o, s in recs if s.lower().endswith(".plb")]
    if plb:
        tops = collections.Counter(s.split(BACKSLASH)[0].lower() for _, s in plb)
        print("  .plb top-level folders: %s" % dict(tops))
        # A level scene would conventionally be levels/<level>/<level>.plb.
        # An earlier version of this test just excluded props/globalmodels/characters
        # and produced FALSE POSITIVES - it flagged blacksedan.plb and
        # ww_overlaydefault.plb, which are models that simply do not live under
        # props/. Test the naming convention itself instead.
        scenes = []
        for _, s2 in plb:
            parts = s2.split(BACKSLASH)
            if len(parts) == 3 and parts[0].lower() == "levels":
                base = os.path.splitext(parts[-1])[0].lower()
                if base == parts[1].lower():
                    scenes.append(s2)
        print("  .plb named levels/<level>/<level>.plb (a level scene): %s"
              % (scenes if scenes else "NONE - see dossier 9c"))
    if show_names:
        for o, s in recs[:limit]:
            print("     @0x%08X  %s" % (o, s))


def census(d):
    files = sorted(f for f in os.listdir(d) if f.lower().endswith((".ppf", ".apf")))
    print("%d pack files in %s" % (len(files), d))
    tot = collections.Counter()
    for f in files:
        blob = read(os.path.join(d, f))
        for _, s in name_records(blob):
            tot[os.path.splitext(s)[1].lower()] += 1
    print("extension census across every pack:")
    for e, n in tot.most_common(20):
        print("   %-22s %d" % (e or "(none)", n))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    target = sys.argv[1]
    if "--census" in sys.argv:
        census(target)
    else:
        limit = 20
        if "--limit" in sys.argv:
            limit = int(sys.argv[sys.argv.index("--limit") + 1])
        report(target, show_names="--names" in sys.argv, limit=limit)


if __name__ == "__main__":
    main()
