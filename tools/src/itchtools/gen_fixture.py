"""Deterministic synthetic ITCH day with a known outcome (spec section 11).

Nothing here comes from Nasdaq data: symbols, prices and order flow are all simulated. The
generator keeps its own order book while it writes, so it can only emit valid sequences, and it
records what must come out of a correct handler next to the file.
"""

from __future__ import annotations

import json
import random
import struct
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from itchtools.timeutil import format_et, to_utc_ns

SYMBOL_COUNT = 20
DEFAULT_MESSAGES = 25_000
DEFAULT_DATE = "2019-01-30"
TICK = 100  # One cent in Price(4) units.
SECOND = 1_000_000_000
MINUTE = 60 * SECOND
HOUR = 60 * MINUTE


def clock(hours: int, minutes: int = 0, seconds: int = 0) -> int:
    return hours * HOUR + minutes * MINUTE + seconds * SECOND


def symbol_name(index: int) -> str:
    return f"FIX{index:02d}"


def alpha(text: str, width: int) -> bytes:
    return text.ljust(width).encode("ascii")


@dataclass
class Order:
    ref: int
    side: str
    price: int
    shares: int
    mpid: str | None


@dataclass
class Book:
    """Price-time book for one symbol: price to a FIFO list of orders on each side."""

    locate: int
    symbol: str
    mid: int
    bids: dict[int, list[Order]] = field(default_factory=dict)
    asks: dict[int, list[Order]] = field(default_factory=dict)

    def levels(self, side: str) -> dict[int, list[Order]]:
        return self.bids if side == "B" else self.asks

    def best_bid(self) -> int | None:
        return max(self.bids) if self.bids else None

    def best_ask(self) -> int | None:
        return min(self.asks) if self.asks else None

    def insert(self, order: Order) -> None:
        self.levels(order.side).setdefault(order.price, []).append(order)

    def remove(self, order: Order) -> None:
        levels = self.levels(order.side)
        queue = levels[order.price]
        queue.remove(order)
        if not queue:
            del levels[order.price]

    def resting(self) -> list[Order]:
        out: list[Order] = []
        for levels in (self.bids, self.asks):
            for queue in levels.values():
                out.extend(queue)
        return out


@dataclass
class Tally:
    """Per-symbol totals that a correct handler must reproduce (DQ-09, DQ-13)."""

    added: int = 0
    executed: int = 0
    cancelled: int = 0
    deleted: int = 0
    replaced_out: int = 0
    volume: int = 0
    closing_cross_price: int = 0
    closing_cross_shares: int = 0


class FixtureDay:
    def __init__(self, seed: int, messages: int) -> None:
        self.rng = random.Random(seed)
        self.target = messages
        self.frames: list[bytes] = []
        self.counts: Counter[str] = Counter()
        self.ts = clock(3, 5)
        self.tracking = 0
        self.next_ref = 1000
        self.next_match = 1
        self.books = [
            Book(i + 1, symbol_name(i), (self.rng.randrange(10, 300) * 100) * TICK // 100 * 100)
            for i in range(SYMBOL_COUNT)
        ]
        self.tallies: dict[str, Tally] = defaultdict(Tally)
        self.orders: dict[int, tuple[Book, Order]] = {}
        self.trades: dict[int, tuple[str, int, int]] = {}  # match -> symbol, shares, price
        self.system_events: list[tuple[str, int]] = []

    def emit(self, type_byte: str, locate: int, body: bytes) -> None:
        """Appends one length-prefixed message with the common ITCH header."""
        self.tracking = (self.tracking + 1) % 65_536
        message = (
            type_byte.encode("ascii")
            + struct.pack(">HH", locate, self.tracking)
            + self.ts.to_bytes(6, "big")
            + body
        )
        self.frames.append(struct.pack(">H", len(message)) + message)
        self.counts[type_byte] += 1

    def advance(self, until: int) -> None:
        """Moves the clock forward by a little, never past `until`."""
        self.ts = max(self.ts, min(until, self.ts + self.rng.randrange(1, 40_000_000)))

    def at(self, moment: int) -> None:
        self.ts = max(self.ts, moment)

    def match_number(self) -> int:
        self.next_match += 1
        return self.next_match

    # ---- Administrative messages ---------------------------------------------------------

    def system_event(self, code: str, moment: int) -> None:
        self.at(moment)
        self.emit("S", 0, code.encode("ascii"))
        self.system_events.append((code, self.ts))

    def directory(self, book: Book) -> None:
        body = (
            alpha(book.symbol, 8)
            + b"Q"  # market category
            + b"N"  # financial status
            + struct.pack(">I", 100)
            + b"N"  # round lots only
            + b"C"  # issue classification
            + b"Z "  # issue sub-type
            + b"P"  # authenticity
            + b"N"  # short sale threshold
            + b"N"  # IPO flag
            + b"1"  # LULD tier
            + (b"Y" if book.locate == 2 else b"N")  # ETP flag
            + struct.pack(">I", 1)
            + b"N"  # inverse indicator
        )
        self.emit("R", book.locate, body)

    def trading_action(self, book: Book, state: str, reason: str = "") -> None:
        self.emit(
            "H", book.locate, alpha(book.symbol, 8) + state.encode() + b" " + alpha(reason, 4)
        )

    def opening_spin(self) -> None:
        self.system_event("O", clock(3, 5))
        for book in self.books:
            self.directory(book)
        self.emit(
            "V", 0, struct.pack(">QQQ", 2_400_000_000_000, 2_200_000_000_000, 2_000_000_000_000)
        )
        self.system_event("S", clock(4))
        for book in self.books:
            self.trading_action(book, "T")
            self.emit("Y", book.locate, alpha(book.symbol, 8) + b"0")
        for book in self.books[:3]:
            body = alpha("GSCO", 4) + alpha(book.symbol, 8) + b"YNA"
            self.emit("L", book.locate, body)
        ipo = self.books[-1]
        self.emit(
            "K",
            ipo.locate,
            alpha(ipo.symbol, 8) + struct.pack(">I", 36_000) + b"A" + struct.pack(">I", ipo.mid),
        )
        self.emit("N", self.books[0].locate, alpha(self.books[0].symbol, 8) + b"B")
        self.emit("O", ipo.locate, self.dlcr_body(ipo))

    def dlcr_body(self, book: Book) -> bytes:
        return (
            alpha(book.symbol, 8)
            + b"Y"
            + struct.pack(">III", book.mid // 2, book.mid * 2, book.mid)
            + struct.pack(">Q", self.ts)
            + struct.pack(">II", book.mid - 10 * TICK, book.mid + 10 * TICK)
        )

    def noii(self, book: Book, cross_type: str) -> None:
        body = (
            struct.pack(">QQ", 5000, 300)
            + b"B"
            + alpha(book.symbol, 8)
            + struct.pack(">III", book.mid, book.mid, book.mid)
            + cross_type.encode()
            + b"L"
        )
        self.emit("I", book.locate, body)

    # ---- Order flow ----------------------------------------------------------------------

    def legal_price(self, book: Book, side: str) -> int:
        """A price that does not cross or lock the opposite side."""
        offset = self.rng.randrange(1, 12) * TICK
        best_bid, best_ask = book.best_bid(), book.best_ask()
        if side == "B":
            price = book.mid - offset
            return min(price, best_ask - TICK) if best_ask is not None else price
        price = book.mid + offset
        return max(price, best_bid + TICK) if best_bid is not None else price

    def add(
        self,
        book: Book,
        *,
        side: str | None = None,
        price: int | None = None,
        shares: int | None = None,
    ) -> Order:
        chosen = side or self.rng.choice("BS")
        order = Order(
            ref=self.next_ref,
            side=chosen,
            price=self.legal_price(book, chosen) if price is None else price,
            shares=self.rng.randrange(1, 10) * 100 if shares is None else shares,
            mpid=self.rng.choice(["VIRT", "GSCO", None, None, None]),
        )
        self.next_ref += self.rng.randrange(1, 5)
        body = (
            struct.pack(">Q", order.ref)
            + order.side.encode()
            + struct.pack(">I", order.shares)
            + alpha(book.symbol, 8)
            + struct.pack(">I", order.price)
        )
        if order.mpid:
            self.emit("F", book.locate, body + order.mpid.encode())
        else:
            self.emit("A", book.locate, body)
        book.insert(order)
        self.orders[order.ref] = (book, order)
        self.tallies[book.symbol].added += order.shares
        return order

    def best_order(self, book: Book) -> Order | None:
        """The order at the front of the queue on a random non-empty side."""
        sides = [s for s in "BS" if book.levels(s)]
        if not sides:
            return None
        side = self.rng.choice(sides)
        price = book.best_bid() if side == "B" else book.best_ask()
        assert price is not None
        return book.levels(side)[price][0]

    def any_order(self, book: Book) -> Order | None:
        resting = book.resting()
        return self.rng.choice(resting) if resting else None

    def reduce(self, book: Book, order: Order, shares: int) -> None:
        order.shares -= shares
        if order.shares == 0:
            book.remove(order)
            del self.orders[order.ref]

    def execute(self, book: Book, order: Order) -> None:
        shares = self.rng.choice([order.shares, self.rng.randrange(1, order.shares + 1)])
        match = self.match_number()
        self.emit("E", book.locate, struct.pack(">QIQ", order.ref, shares, match))
        tally = self.tallies[book.symbol]
        tally.executed += shares
        tally.volume += shares
        self.trades[match] = (book.symbol, shares, order.price)
        self.reduce(book, order, shares)

    def execute_at(
        self, book: Book, order: Order, shares: int, price: int, printable: bool
    ) -> None:
        match = self.match_number()
        flag = b"Y" if printable else b"N"
        body = struct.pack(">QIQ", order.ref, shares, match) + flag + struct.pack(">I", price)
        self.emit("C", book.locate, body)
        tally = self.tallies[book.symbol]
        tally.executed += shares
        if printable:
            tally.volume += shares
            self.trades[match] = (book.symbol, shares, price)
        self.reduce(book, order, shares)

    def cancel(self, book: Book, order: Order) -> None:
        if order.shares < 2:
            self.delete(book, order)
            return
        shares = self.rng.randrange(1, order.shares)
        self.emit("X", book.locate, struct.pack(">QI", order.ref, shares))
        self.tallies[book.symbol].cancelled += shares
        self.reduce(book, order, shares)

    def delete(self, book: Book, order: Order) -> None:
        self.emit("D", book.locate, struct.pack(">Q", order.ref))
        self.tallies[book.symbol].deleted += order.shares
        book.remove(order)
        del self.orders[order.ref]

    def replace(self, book: Book, order: Order, *, keep_price: bool) -> None:
        new_shares = self.rng.randrange(1, 10) * 100
        book.remove(order)
        del self.orders[order.ref]
        price = order.price if keep_price else self.legal_price(book, order.side)
        new_ref = self.next_ref
        self.next_ref += self.rng.randrange(1, 5)
        body = struct.pack(">QQII", order.ref, new_ref, new_shares, price)
        self.emit("U", book.locate, body)
        tally = self.tallies[book.symbol]
        tally.replaced_out += order.shares
        tally.added += new_shares
        replacement = Order(new_ref, order.side, price, new_shares, order.mpid)
        book.insert(replacement)
        self.orders[new_ref] = (book, replacement)

    def hidden_trade(self, book: Book) -> None:
        shares = self.rng.randrange(1, 5) * 100
        match = self.match_number()
        body = (
            struct.pack(">Q", 0)
            + b"B"
            + struct.pack(">I", shares)
            + alpha(book.symbol, 8)
            + struct.pack(">IQ", book.mid, match)
        )
        self.emit("P", book.locate, body)
        self.tallies[book.symbol].volume += shares
        self.trades[match] = (book.symbol, shares, book.mid)

    def random_op(self, book: Book, *, executions: bool = True) -> None:
        roll = self.rng.random()
        target = self.best_order(book)
        if target is None or roll < 0.42:
            self.add(book)
        elif roll < 0.54:
            victim = self.any_order(book)
            assert victim is not None
            self.cancel(book, victim)
        elif roll < 0.76:
            victim = self.any_order(book)
            assert victim is not None
            self.delete(book, victim)
        elif roll < 0.86:
            victim = self.any_order(book)
            assert victim is not None
            self.replace(book, victim, keep_price=self.rng.random() < 0.3)
        elif not executions:
            self.add(book)
        elif roll < 0.96:
            self.execute(book, target)
        elif roll < 0.98:
            price = target.price + TICK // 2 if target.side == "B" else target.price - TICK // 2
            shares = self.rng.randrange(1, target.shares + 1)
            self.execute_at(book, target, shares, price, printable=True)
        else:
            self.hidden_trade(book)

    def flow(self, messages: int, until: int) -> None:
        for _ in range(messages):
            self.random_op(self.rng.choice(self.books))
            self.advance(until)

    # ---- Crosses and halts ---------------------------------------------------------------

    def cross(self, book: Book, cross_type: str) -> None:
        """Runs one cross: crossing interest is built, matched non-printably and printed by Q."""
        best_bid, best_ask = book.best_bid(), book.best_ask()
        reference = book.mid if best_ask is None else best_ask
        price = reference if best_bid is None else max(reference, best_bid)
        shares = self.rng.randrange(2, 8) * 100
        buy = self.add(book, side="B", price=price + TICK, shares=shares)
        sell = self.add(book, side="S", price=price - TICK, shares=shares)
        self.at(self.ts + SECOND)
        self.execute_at(book, buy, shares, price, printable=False)
        self.execute_at(book, sell, shares, price, printable=False)
        match = self.match_number()
        crossed = 2 * shares
        body = (
            struct.pack(">Q", crossed)
            + alpha(book.symbol, 8)
            + struct.pack(">IQ", price, match)
            + cross_type.encode()
        )
        self.emit("Q", book.locate, body)
        tally = self.tallies[book.symbol]
        tally.volume += crossed
        if cross_type == "C":
            tally.closing_cross_price = price
            tally.closing_cross_shares = crossed

    def opening_cross(self) -> None:
        self.at(clock(9, 25))
        for book in self.books:
            self.noii(book, "O")
        self.at(clock(9, 29, 55))
        for book in self.books:
            self.cross(book, "O")
        self.system_event("Q", clock(9, 30))

    def closing_cross(self) -> None:
        self.at(clock(15, 50))
        for book in self.books:
            self.noii(book, "C")
        self.at(clock(15, 59, 55))
        for book in self.books:
            self.cross(book, "C")
        self.system_event("M", clock(16))

    def halt_and_resume(self, book: Book) -> None:
        self.at(clock(11))
        self.trading_action(book, "H", "T1")
        self.at(clock(11, 4))
        self.emit(
            "J",
            book.locate,
            alpha(book.symbol, 8)
            + struct.pack(">IIII", book.mid, book.mid + 5 * TICK, book.mid - 5 * TICK, 1),
        )
        self.trading_action(book, "Q", "T1")
        self.noii(book, "H")
        self.at(clock(11, 5))
        self.cross(book, "H")
        self.trading_action(book, "T", "T1")

    def operational_halt(self, book: Book) -> None:
        self.at(clock(12))
        self.emit("h", book.locate, alpha(book.symbol, 8) + b"QH")
        self.at(clock(12, 1))
        self.emit("h", book.locate, alpha(book.symbol, 8) + b"QT")

    def broken_trade(self) -> None:
        match = min(self.trades)
        symbol, shares, _ = self.trades.pop(match)
        self.emit("B", 0, struct.pack(">Q", match))
        self.tallies[symbol].volume -= shares

    # ---- The day -------------------------------------------------------------------------

    def run(self) -> None:
        self.opening_spin()
        for book in self.books:
            for side in "BS":
                self.add(book, side=side)
        fixed = 9 * SYMBOL_COUNT + 60
        remaining = max(self.target - len(self.frames) - fixed, 500)
        self.flow(remaining // 8, clock(9, 24))
        self.opening_cross()
        self.flow(remaining // 4, clock(10, 59))
        self.halt_and_resume(self.books[3])
        self.flow(remaining // 8, clock(11, 59))
        self.operational_halt(self.books[4])
        self.at(clock(12, 30))
        self.emit("W", 0, b"1")
        self.flow(remaining // 3, clock(15, 49))
        self.closing_cross()
        self.at(clock(16, 1))
        self.flow(remaining - remaining // 8 * 2 - remaining // 4 - remaining // 3, clock(19, 59))
        self.broken_trade()
        self.system_event("E", clock(20))
        self.system_event("C", clock(20, 5))

    def stats(self, date: str, total_bytes: int) -> dict[str, Any]:
        """The report `itch stats` must produce for this day."""
        first = int.from_bytes(self.frames[0][7:13], "big")
        last = int.from_bytes(self.frames[-1][7:13], "big")
        return {
            "by_type": dict(sorted(self.counts.items())),
            "bytes": total_bytes,
            "date": date,
            "first_timestamp_ns": first,
            "last_timestamp_ns": last,
            "malformed": 0,
            "system_events": [
                {"code": code, "et": format_et(ts), "utc_ns": to_utc_ns(date, ts)}
                for code, ts in self.system_events
            ],
            "total": len(self.frames),
        }

    def symbols(self) -> list[dict[str, Any]]:
        return [
            {
                "etp": "Y" if book.locate == 2 else "N",
                "locate": book.locate,
                "market_category": "Q",
                "round_lot_size": 100,
                "symbol": book.symbol,
            }
            for book in self.books
        ]

    def outcome(self) -> dict[str, Any]:
        symbols: dict[str, Any] = {}
        for book in self.books:
            tally = self.tallies[book.symbol]
            resting = book.resting()
            symbols[book.symbol] = {
                "added": tally.added,
                "cancelled": tally.cancelled,
                "closing_cross_price": tally.closing_cross_price,
                "closing_cross_shares": tally.closing_cross_shares,
                "deleted": tally.deleted,
                "executed": tally.executed,
                "replaced_out": tally.replaced_out,
                "resting_orders": len(resting),
                "resting_shares": sum(o.shares for o in resting),
                "volume": tally.volume,
            }
        return {"symbols": symbols}


def compact(value: Any) -> str:
    return json.dumps(value, separators=(",", ":"))


def generate(out: Path, seed: int = 7, messages: int = DEFAULT_MESSAGES) -> dict[str, Any]:
    """Writes the fixture and its golden files next to it, returning the stats report."""
    day = FixtureDay(seed, messages)
    day.run()
    data = b"".join(day.frames)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    stats = day.stats(DEFAULT_DATE, len(data))
    (out.parent / "golden_stats.json").write_text(compact(stats) + "\n", encoding="utf-8")
    (out.parent / "golden_symbols.json").write_text(compact(day.symbols()) + "\n", encoding="utf-8")
    outcome = json.dumps(day.outcome(), indent=1, sort_keys=True) + "\n"
    (out.parent / "golden_outcome.json").write_text(outcome, encoding="utf-8")
    return stats
