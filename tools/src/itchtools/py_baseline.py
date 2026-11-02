"""A deliberately plain Python parser and book (FR-26).

Two uses: the slow, independent book that checks `itch book-at` (AC-W-06), and the speed baseline
for AC-H-02. It shares no code with the C++ handler. It uses `struct` and dicts and aims to be easy
to read rather than fast.
"""

from __future__ import annotations

import mmap
import struct
import time
from collections import Counter
from collections.abc import Iterable, Iterator
from dataclasses import dataclass, field
from pathlib import Path

ADD = struct.Struct(">QcI8sI")
EXEC = struct.Struct(">QI")
EXEC_PRICE = struct.Struct(">QIQcI")
REF = struct.Struct(">Q")
REPLACE = struct.Struct(">QQII")
HEADER = struct.Struct(">HH")
NOII = struct.Struct(">QQc8sIIIcc")
DEPTH_HEADER = " LVL   BID_ORD  BID_QTY     BID_PX |     ASK_PX  ASK_QTY  ASK_ORD"


@dataclass
class SlowOrder:
    ref: int
    side: str
    price: int
    shares: int


@dataclass
class SlowSymbolBook:
    """One symbol: each side maps price to a FIFO list of orders."""

    bids: dict[int, list[SlowOrder]] = field(default_factory=dict)
    asks: dict[int, list[SlowOrder]] = field(default_factory=dict)

    def side(self, name: str) -> dict[int, list[SlowOrder]]:
        return self.bids if name == "B" else self.asks

    def levels(self, name: str, depth: int) -> list[tuple[int, int, int]]:
        """Best-first (price, quantity, order count) tuples."""
        book = self.side(name)
        prices = sorted(book, reverse=name == "B")[:depth]
        return [(p, sum(o.shares for o in book[p]), len(book[p])) for p in prices]


class SlowBook:
    """Applies messages for the locates in `only`, or for all of them when it is None."""

    def __init__(self, only: set[int] | None = None) -> None:
        self.only = only
        self.books: dict[int, SlowSymbolBook] = {}
        self.orders: dict[int, tuple[int, SlowOrder]] = {}
        self.symbols: dict[str, int] = {}
        self.noii: dict[int, tuple[int, int, str, int, int, int, str, str]] = {}

    def define(self, locate: int, symbol: str) -> None:
        self.symbols[symbol] = locate

    def book(self, symbol: str) -> SlowSymbolBook:
        return self.books.setdefault(self.symbols[symbol], SlowSymbolBook())

    def add(self, locate: int, body: bytes) -> None:
        ref, side, shares, _, price = ADD.unpack_from(body)
        order = SlowOrder(ref, side.decode(), price, shares)
        book = self.books.setdefault(locate, SlowSymbolBook())
        book.side(order.side).setdefault(price, []).append(order)
        self.orders[ref] = (locate, order)

    def remove(self, ref: int) -> None:
        locate, order = self.orders.pop(ref)
        side = self.books[locate].side(order.side)
        queue = side[order.price]
        queue.remove(order)
        if not queue:
            del side[order.price]

    def reduce(self, ref: int, shares: int) -> None:
        order = self.orders[ref][1]
        order.shares -= shares
        if order.shares <= 0:
            self.remove(ref)

    def replace(self, body: bytes) -> None:
        old_ref, new_ref, shares, price = REPLACE.unpack_from(body)
        locate, old = self.orders[old_ref]
        self.remove(old_ref)
        order = SlowOrder(new_ref, old.side, price, shares)
        self.books[locate].side(order.side).setdefault(price, []).append(order)
        self.orders[new_ref] = (locate, order)

    def apply(self, message: bytes) -> None:
        kind = message[0]
        (locate,) = struct.unpack_from(">H", message, 1)
        if kind == 0x52:  # R
            self.define(locate, message[11:19].decode("ascii").rstrip())
            return
        if self.only is not None and locate not in self.only:
            return
        body = message[11:]
        if kind in (0x41, 0x46):  # A, F
            self.add(locate, body)
        elif kind == 0x45:  # E
            ref, shares = EXEC.unpack_from(body)
            self.reduce(ref, shares)
        elif kind == 0x43:  # C
            ref, shares, _, _, _ = EXEC_PRICE.unpack_from(body)
            self.reduce(ref, shares)
        elif kind == 0x58:  # X
            ref, shares = EXEC.unpack_from(body)
            self.reduce(ref, shares)
        elif kind == 0x44:  # D
            self.remove(REF.unpack_from(body)[0])
        elif kind == 0x55:  # U
            self.replace(body)
        elif kind == 0x49:  # I
            paired, imbalance, direction, _, far, near, ref, cross, variation = NOII.unpack_from(
                body
            )
            self.noii[locate] = (
                paired,
                imbalance,
                direction.decode(),
                far,
                near,
                ref,
                cross.decode(),
                variation.decode(),
            )

    def noii_text(self, locate: int) -> str:
        """The NOII line of `itch book-at`."""
        if locate not in self.noii:
            return "none"
        paired, imbalance, direction, far, near, ref, cross, variation = self.noii[locate]
        return (
            f"cross={cross} paired={paired} imbalance={imbalance} dir={direction} "
            f"far={price_text(far)} near={price_text(near)} ref={price_text(ref)} var={variation}"
        )


def frames(data: bytes | mmap.mmap, limit: int | None = None) -> Iterator[bytes]:
    """Yields each message of a length-prefixed ITCH byte buffer."""
    offset = 0
    size = len(data)
    count = 0
    while offset + 2 <= size and (limit is None or count < limit):
        length = (data[offset] << 8) | data[offset + 1]
        yield data[offset + 2 : offset + 2 + length]
        offset += 2 + length
        count += 1


def timestamp(message: bytes) -> int:
    return int.from_bytes(message[5:11], "big")


def open_file(path: Path) -> mmap.mmap:
    with path.open("rb") as handle:
        return mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)


def snapshots(
    path: Path, queries: Iterable[tuple[str, int]], depth: int = 10
) -> dict[tuple[str, int], dict[str, list[tuple[int, int, int]]]]:
    """L2 books at each (symbol, nanoseconds since midnight), from one pass over the file.

    The directory comes first, so the symbols of interest are resolved to locates the first time
    they appear and only those locates are applied.
    """
    wanted = sorted(queries, key=lambda q: q[1])
    symbols = {symbol for symbol, _ in wanted}
    slow = SlowBook(only=set())
    results: dict[tuple[str, int], dict[str, list[tuple[int, int, int]]]] = {}
    position = 0
    for message in frames(open_file(path)):
        moment = timestamp(message)
        while position < len(wanted) and moment > wanted[position][1]:
            query = wanted[position]
            book = slow.book(query[0])
            results[query] = {"bids": book.levels("B", depth), "asks": book.levels("S", depth)}
            position += 1
        if position == len(wanted):
            return results
        slow.apply(message)
        if message[0] == 0x52:  # R
            symbol = message[11:19].decode("ascii").rstrip()
            if symbol in symbols:
                assert slow.only is not None
                slow.only.add(int.from_bytes(message[1:3], "big"))
    for query in wanted[position:]:
        book = slow.book(query[0])
        results[query] = {"bids": book.levels("B", depth), "asks": book.levels("S", depth)}
    return results


def price_text(ticks: int) -> str:
    return f"{ticks // 10000}.{ticks % 10000:04d}"


def depth_rows(book: SlowSymbolBook, depth: int) -> list[str]:
    """The depth table of `itch book-at`, row by row, built independently of the C++ code."""
    bids = book.levels("B", depth)
    asks = book.levels("S", depth)
    rows = [DEPTH_HEADER]
    for index in range(max(len(bids), len(asks))):
        left = ""
        right = ""
        if index < len(bids):
            price, qty, orders = bids[index]
            left = f"{orders:>10}{qty:>9}{price_text(price):>11}"
        if index < len(asks):
            price, qty, orders = asks[index]
            right = f"{price_text(price):>11}{qty:>9}{orders:>9}"
        rows.append(f"{index:>4}{left:<30} |{right}".rstrip())
    return rows


@dataclass(frozen=True)
class BaselineResult:
    messages: int
    seconds: float
    counts: dict[str, int]

    @property
    def messages_per_second(self) -> float:
        return self.messages / self.seconds if self.seconds else 0.0


def run_baseline(path: Path, limit: int | None, with_book: bool) -> BaselineResult:
    """Times a parse, or a parse plus book build, over the first `limit` messages."""
    counts: Counter[str] = Counter()
    slow = SlowBook() if with_book else None
    data = open_file(path)
    start = time.perf_counter()
    for message in frames(data, limit):
        counts[chr(message[0])] += 1
        HEADER.unpack_from(message, 1)
        if slow is not None:
            slow.apply(message)
    seconds = time.perf_counter() - start
    return BaselineResult(sum(counts.values()), seconds, dict(sorted(counts.items())))
