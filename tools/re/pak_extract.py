"""List / extract entries from the PopCap main.pak used by PvZ GOTY (whole-file XOR 0xF7).

The record table is terminated by an entry whose first byte is 0x80; a fixed
9-byte tail follows, then the payload area.

Usage:
  python pak_extract.py list <pak> [substring]
  python pak_extract.py cat  <pak> <substring> [max-bytes]
  python pak_extract.py dump <pak> <out-dir> <substring>
"""
import os
import struct
import sys
import zlib

sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def load(path):
    raw = open(path, "rb").read()
    data = raw.translate(bytes(b ^ 0xF7 for b in range(256)))
    if data[:4] != b"\xc0\x4a\xc0\xba":
        raise SystemExit("magic mismatch after XOR")
    return data


def parse(data):
    recs = []
    o = 8
    running = 0
    while True:
        flags = data[o]
        name_len = data[o + 1]
        if name_len == 0 or flags == 0x80:
            break
        o += 2
        name = data[o:o + name_len].decode("latin1").replace("\\", "/")
        o += name_len
        (size,) = struct.unpack_from("<I", data, o)
        o += 4
        o += 8  # FILETIME
        recs.append((name, size, running))
        running += size
    data_start = o + 1
    return recs, data_start


def payload(data, data_start, rec):
    name, size, offset = rec
    body = data[data_start + offset:data_start + offset + size]
    try:
        return zlib.decompress(body), "zlib"
    except zlib.error:
        return body, "raw"


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    mode, pak = sys.argv[1], sys.argv[2]
    data = load(pak)
    recs, data_start = parse(data)

    if mode == "list":
        needle = sys.argv[3].lower() if len(sys.argv) > 3 else ""
        for rec in recs:
            if needle in rec[0].lower():
                print(f"{rec[1]:>9}  {rec[0]}")
        print(f"# {len(recs)} entries, data_start={data_start}")
    elif mode in ("cat", "dump"):
        is_cat = mode == "cat"
        needle = (sys.argv[3] if is_cat else sys.argv[4]).lower()
        hits = [r for r in recs if needle in r[0].lower()]
        if not hits:
            raise SystemExit(f"no entry matching {needle}")
        if is_cat:
            limit = int(sys.argv[4]) if len(sys.argv) > 4 else 3000
            for rec in hits[:4]:
                body, how = payload(data, data_start, rec)
                print(f"--- {rec[0]}  {len(body)} bytes ({how}) ---")
                sys.stdout.write(body[:limit].decode("latin1"))
                print()
        else:
            out_dir = sys.argv[3]
            needle = sys.argv[4].lower()
            n = 0
            for rec in recs:
                if needle in rec[0].lower():
                    body, _ = payload(data, data_start, rec)
                    path = os.path.join(out_dir, rec[0])
                    os.makedirs(os.path.dirname(path), exist_ok=True)
                    with open(path, "wb") as f:
                        f.write(body)
                    n += 1
            print(f"# dumped {n} entries to {out_dir}")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
