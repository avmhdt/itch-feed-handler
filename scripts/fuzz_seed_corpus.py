#!/usr/bin/env python3
"""Cuts the fixture day into small libFuzzer seeds.

Usage: fuzz_seed_corpus.py FIXTURE CORPUS_DIR

Writes parser/ (one file per message type plus a run of frames), book/ (structured operations)
and mold/ (size-prefixed MoldUDP64 datagrams).
"""

import pathlib
import struct
import sys


def mold_seed(data: bytes) -> bytes:
    """A few downstream packets, a heartbeat and end of session, each with a 2-byte size."""
    session = b"ITCHDEMO01"
    packets = []
    offset = 0
    seq = 1
    for _ in range(4):
        start = offset
        count = 0
        while count < 6:
            (length,) = struct.unpack_from(">H", data, offset)
            offset += 2 + length
            count += 1
        packets.append(session + struct.pack(">QH", seq, count) + data[start:offset])
        seq += count
    packets.append(session + struct.pack(">QH", seq, 0))
    packets.append(session + struct.pack(">QH", seq, 0xFFFF))
    return b"".join(struct.pack(">H", len(p)) + p for p in packets)


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
    mold = corpus / "mold"
    mold.mkdir(parents=True, exist_ok=True)
    (mold / "packets").write_bytes(mold_seed(data))
    print(f"{len(seen)} message types, {corpus}")


if __name__ == "__main__":
    main()
