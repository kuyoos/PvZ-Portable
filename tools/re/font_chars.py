import re
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

path = sys.argv[1]
text = open(path, "rb").read().decode("utf-8-sig", "replace")

m = re.search(r"Define\s+CharList0\s*\((.*?)\)\s*;", text, re.S)
if not m:
    raise SystemExit("CharList0 not found")

body = m.group(1)
chars = []
i = 0
while i < len(body):
    if body[i] == "'":
        i += 1
        if i < len(body) and body[i] == "\\":
            i += 1
            chars.append(body[i])
            i += 1
        else:
            chars.append(body[i])
            i += 1
    i += 1

cjk = [c for c in chars if "\u4e00" <= c <= "\u9fff"]
print(f"file={path}")
print(f"glyphs={len(chars)}  unique={len(set(chars))}  cjk={len(cjk)}")
if cjk:
    print("cjk sample:", "".join(cjk[:40]))
print("first 60:", "".join(chars[:60]))
print("last 60:", "".join(chars[-60:]))
