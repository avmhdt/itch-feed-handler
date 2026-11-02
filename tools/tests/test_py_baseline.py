"""Tests of the slow Python book, on the fixture day and on hand-built messages."""

from __future__ import annotations

import json
import struct
from pathlib import Path

import pytest
from hypothesis import given
from hypothesis import strategies as st
from typer.testing import CliRunner

from itchtools import cli, gen_fixture, py_baseline
from itchtools.settings import REPO_ROOT

FIXTURE = REPO_ROOT / "tests" / "fixtures" / "mini_day.itchbin"


def message(kind: str, locate: int, body: bytes, ts: int = 0) -> bytes:
    return kind.encode() + struct.pack(">HH", locate, 0) + ts.to_bytes(6, "big") + body


def add(ref: int, side: bytes, shares: int, price: int, locate: int = 1) -> bytes:
    return message("A", locate, struct.pack(">Qc I8sI", ref, side, shares, b"AAA     ", price))


def directory(symbol: bytes, locate: int = 1) -> bytes:
    return message("R", locate, symbol.ljust(8) + b" " * 20)


def test_fifo_levels_replace_and_reductions() -> None:
    book = py_baseline.SlowBook()
    book.apply(directory(b"AAA"))
    book.apply(add(1, b"B", 100, 500))
    book.apply(add(2, b"B", 200, 500))
    book.apply(add(3, b"S", 300, 510))
    assert book.book("AAA").levels("B", 5) == [(500, 300, 2)]

    book.apply(message("E", 1, struct.pack(">QIQ", 1, 40, 9)))
    book.apply(message("C", 1, struct.pack(">QIQcI", 1, 10, 9, b"Y", 500)))
    book.apply(message("X", 1, struct.pack(">QI", 3, 300)))
    assert book.book("AAA").levels("S", 5) == []
    assert book.book("AAA").levels("B", 5) == [(500, 250, 2)]

    book.apply(message("U", 1, struct.pack(">QQII", 1, 4, 70, 505)))
    assert book.book("AAA").levels("B", 5) == [(505, 70, 1), (500, 200, 1)]

    book.apply(message("D", 1, struct.pack(">Q", 2)))
    assert book.book("AAA").levels("B", 5) == [(505, 70, 1)]


def test_filter_skips_other_locates() -> None:
    book = py_baseline.SlowBook(only={2})
    book.apply(directory(b"AAA"))
    book.apply(add(1, b"B", 100, 500))
    assert book.orders == {}


def test_frames_stops_at_the_limit() -> None:
    data = struct.pack(">H", 2) + b"ab" + struct.pack(">H", 1) + b"c"
    assert list(py_baseline.frames(data)) == [b"ab", b"c"]
    assert list(py_baseline.frames(data, limit=1)) == [b"ab"]


@given(st.integers(0, 2_000_000_000))
def test_price_text_round_trips(ticks: int) -> None:
    whole, fraction = py_baseline.price_text(ticks).split(".")
    assert int(whole) * 10000 + int(fraction) == ticks


def test_fixture_replay_matches_the_generator_outcome() -> None:
    outcome = json.loads((FIXTURE.parent / "golden_outcome.json").read_text())["symbols"]
    book = py_baseline.SlowBook()
    for frame in py_baseline.frames(py_baseline.open_file(FIXTURE)):
        book.apply(frame)
    for symbol, expected in outcome.items():
        slow = book.book(symbol)
        rows = slow.levels("B", 1000) + slow.levels("S", 1000)
        assert sum(r[1] for r in rows) == expected["resting_shares"]
        assert sum(r[2] for r in rows) == expected["resting_orders"]


def test_snapshots_agree_with_a_full_replay() -> None:
    moment = gen_fixture.clock(10, 30)
    late = gen_fixture.clock(23)
    result = py_baseline.snapshots(FIXTURE, [("FIX01", moment), ("FIX02", late)])
    book = py_baseline.SlowBook()
    for frame in py_baseline.frames(py_baseline.open_file(FIXTURE)):
        if py_baseline.timestamp(frame) > moment:
            break
        book.apply(frame)
    assert result[("FIX01", moment)]["bids"] == book.book("FIX01").levels("B", 10)
    assert result[("FIX01", moment)]["asks"] == book.book("FIX01").levels("S", 10)
    assert ("FIX02", late) in result


def test_depth_rows_pad_a_one_sided_row() -> None:
    book = py_baseline.SlowSymbolBook()
    book.bids[1000000] = [py_baseline.SlowOrder(1, "B", 1000000, 300)]
    rows = py_baseline.depth_rows(book, 5)
    assert rows[0] == py_baseline.DEPTH_HEADER
    assert rows[1] == "   0         1      300   100.0000 |"


def test_baseline_counts_match_the_golden_stats() -> None:
    stats = json.loads((FIXTURE.parent / "golden_stats.json").read_text())
    result = py_baseline.run_baseline(FIXTURE, None, with_book=True)
    assert result.counts == stats["by_type"]
    assert result.messages == stats["total"]
    assert result.messages_per_second > 0
    assert py_baseline.BaselineResult(0, 0.0, {}).messages_per_second == 0.0


def test_cli_py_baseline(tmp_path: Path) -> None:
    result = CliRunner().invoke(cli.app, ["py-baseline", "--file", str(FIXTURE), "--limit", "1000"])
    assert result.exit_code == 0, result.output
    assert "1000 messages" in result.output


@pytest.mark.parametrize("kind", ["D", "E"])
def test_unknown_reference_raises(kind: str) -> None:
    book = py_baseline.SlowBook()
    body = struct.pack(">Q", 9) + b"\0" * 12
    with pytest.raises(KeyError):
        book.apply(message(kind, 1, body))
