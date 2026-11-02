"""AC-W-06 on real data: L2 depth 10 from `itch book-at` equals the Python slow book.

Local only (marker `data`). One Python pass answers all 25 queries; the C++ side runs once per
query.
"""

from __future__ import annotations

import json
import os
import random
import subprocess
from pathlib import Path

import pytest

from itchtools import py_baseline
from itchtools.settings import REPO_ROOT, Settings

DAYS = {"2019-01-30": "01302019", "2019-03-27": "03272019", "2020-01-30": "01302020"}
QUERY_COUNT = 25
OPEN_S = 9 * 3600 + 30 * 60
CLOSE_S = 16 * 3600


def itch_binary() -> str:
    default = Path.home() / "build" / "itch-feed-handler" / "release" / "src" / "itch"
    return os.environ.get("ITCH_BIN", str(default))


def watchlist() -> list[str]:
    lines = (REPO_ROOT / "configs" / "watchlist.txt").read_text().splitlines()
    return [line for line in lines if line and not line.startswith("#")]


def sampled_queries(seed: int) -> list[tuple[str, int]]:
    """AAPL at 10:30:00.000 plus random (symbol, millisecond) pairs in market hours."""
    rng = random.Random(seed)
    symbols = watchlist()
    queries = [("AAPL", (10 * 3600 + 30 * 60) * 1_000_000_000)]
    while len(queries) < QUERY_COUNT:
        millis = rng.randrange(OPEN_S * 1000, CLOSE_S * 1000)
        queries.append((rng.choice(symbols), millis * 1_000_000))
    return queries


def time_text(ns: int) -> str:
    seconds, nanos = divmod(ns, 1_000_000_000)
    return (
        f"{seconds // 3600:02d}:{seconds // 60 % 60:02d}:{seconds % 60:02d}"
        f".{nanos // 1_000_000:03d}"
    )


@pytest.mark.data
@pytest.mark.parametrize("date", sorted(DAYS))
def test_book_at_matches_the_slow_book(date: str) -> None:
    path = Settings().interim_dir / f"{DAYS[date]}.NASDAQ_ITCH50"
    queries = sampled_queries(seed=int(DAYS[date]))
    slow = py_baseline.snapshots(path, queries)
    for symbol, moment in queries:
        result = subprocess.run(
            [itch_binary(), "book-at", "--file", str(path), "--symbol", symbol,
             "--time", time_text(moment), "--format", "json"],
            check=True, capture_output=True, text=True,
        )  # fmt: skip
        book = json.loads(result.stdout)
        for side, key in (("bids", "bids"), ("asks", "asks")):
            actual = [(lv["price_ticks"], lv["qty"], lv["orders"]) for lv in book[key]]
            assert actual == slow[(symbol, moment)][side], f"{symbol} {time_text(moment)} {side}"
