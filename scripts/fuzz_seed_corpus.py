#!/usr/bin/env python3
"""Cuts the fixture day into small libFuzzer seeds.

Usage: fuzz_seed_corpus.py FIXTURE CORPUS_DIR

Writes parser/ (one file per message type plus a run of frames) and book/ (structured operations).
"""

import pathlib
import struct
import sys


def main() -> None:
    data = pathlib.Path(sys.argv[1]).read_bytes()
    corpus = pathlib.Path(sys.argv[2])
    out = corpus / "parser"
    out.mkdir(parents=True, exist_ok=True)
    seen = set()
    offset = 0
    while offset < len(data):
        (length,) = struct.unpack_from(">H", data, offset)
        frame = data[offset : offset + 2 + length]
        kind = frame[2]
        if kind not in seen:
            seen.add(kind)
            (out / f"type_{kind:02x}").write_bytes(frame)
        offset += 2 + length
    (out / "run").write_bytes(data[:4096])
    book = corpus / "book"
    book.mkdir(parents=True, exist_ok=True)
    (book / "ops").write_bytes(data[4096:4096 + 1024])
    print(f"{len(seen)} message types, {corpus}")


if __name__ == "__main__":
    main()
