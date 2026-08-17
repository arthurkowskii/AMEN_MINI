#!/usr/bin/env python3
"""Prepare the Teensy sketch folder: copy the portable core under
teensy/amen_midi/src and rewrite internal includes to be relative to each
file's own directory, so arduino-cli resolves them without any -I flag
(its library-detection preprocess pass does not receive extra include dirs).

Generated tree is gitignored; re-run via scripts/build_teensy.sh.
"""
import re
import shutil
import sys
from pathlib import Path

SRC = Path(sys.argv[1]).resolve()
DEST = Path(sys.argv[2]).resolve()
SKETCH = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None

if DEST.exists():
    shutil.rmtree(DEST)
shutil.copytree(SRC, DEST, ignore=shutil.ignore_patterns("library.properties"))

INC_RE = re.compile(
    r'^(?P<indent>\s*#include\s+)"(?P<path>[a-z_][a-zA-Z0-9_/.-]*)"',
    re.MULTILINE,
)


def rewrite(path: Path, prefix: str) -> int:
    text = path.read_text()
    n = 0

    def repl(m: re.Match) -> str:
        nonlocal n
        p = m.group("path")
        if "/" in p and p.split("/")[0] in {
            "music", "midi", "profiles", "performance", "controls",
            "algorithms", "ui", "common", "teensy",
        }:
            n += 1
            return f'{m.group("indent")}"{prefix}{p}"'
        return m.group(0)

    out = INC_RE.sub(repl, text)
    if out != text:
        path.write_text(out)
    return n


total = 0
for f in DEST.rglob("*"):
    if f.is_file() and f.suffix in {".cpp", ".hpp", ".h"}:
        rel = f.relative_to(DEST)
        prefix = "../" * (len(rel.parts) - 1)
        total += rewrite(f, prefix)

# Sketch-root port files include engine headers via the generated src/ tree.
if SKETCH is not None:
    for f in SKETCH.glob("*"):
        if f.is_file() and f.suffix in {".cpp", ".hpp", ".h"}:
            total += rewrite(f, "src/")
print(f"prepared {DEST.name}/ : {total} includes rewritten")
