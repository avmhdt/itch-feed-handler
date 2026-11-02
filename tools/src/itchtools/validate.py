"""Comparison of handler output with external references (FR-21)."""

from __future__ import annotations

import json
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import polars as pl

from itchtools import outputs


@dataclass(frozen=True)
class CountsComparison:
    date: str
    rows: list[tuple[str, int, int]]  # type, handler count, reference count
    handler_total: int
    reference_total: int
    malformed: int
    reference_errors: int

    @property
    def matches(self) -> bool:
        return (
            all(ours == theirs for _, ours, theirs in self.rows)
            and self.handler_total == self.reference_total
            and self.malformed == 0
            and self.reference_errors == 0
        )


def compare_counts(ours: dict[str, Any], reference: dict[str, Any]) -> CountsComparison:
    """AC-W-01: every type count and the total must be equal, with nothing malformed."""
    ours_by_type: dict[str, int] = ours["by_type"]
    theirs_by_type: dict[str, int] = reference["by_type"]
    types = sorted(set(ours_by_type) | set(theirs_by_type))
    rows = [(t, ours_by_type.get(t, 0), theirs_by_type.get(t, 0)) for t in types]
    return CountsComparison(
        date=str(ours.get("date")),
        rows=rows,
        handler_total=int(ours["total"]),
        reference_total=int(reference["total"]),
        malformed=int(ours["malformed"]),
        reference_errors=int(reference.get("errors", 0)),
    )


def counts_report(comparison: CountsComparison, reference: dict[str, Any]) -> str:
    verdict = "PASS" if comparison.matches else "FAIL"
    lines = [
        f"# Message counts, {comparison.date}: {verdict}",
        "",
        "AC-W-01: `itch stats` against the `itchy` reference parser, exact equality by type.",
        "",
        "| Type | itch | reference | Difference |",
        "|---|---:|---:|---:|",
    ]
    lines += [f"| {t} | {a:,} | {b:,} | {a - b:,} |" for t, a, b in comparison.rows]
    lines += [
        f"| Total | {comparison.handler_total:,} | {comparison.reference_total:,} | "
        f"{comparison.handler_total - comparison.reference_total:,} |",
        "",
        f"Malformed frames: {comparison.malformed}. Reference parse errors: "
        f"{comparison.reference_errors}.",
    ]
    framed_only = reference.get("framed_only", {})
    if framed_only:
        kinds = ", ".join(sorted(framed_only))
        lines += [
            "",
            f"`itchy` has no parser for type(s) {kinds}, so `ref_counts` counts those from the",
            "frame header and passes everything else through `itchy`.",
        ]
    return "\n".join(lines) + "\n"


def validate_counts(date: str, validation_dir: Path) -> CountsComparison:
    """Compares the two count files for a date and writes the report next to them."""
    ours = json.loads((validation_dir / f"counts_{date}.json").read_text(encoding="utf-8"))
    reference_path = validation_dir / f"ref_counts_{date}.json"
    reference = json.loads(reference_path.read_text(encoding="utf-8"))
    comparison = compare_counts(ours, reference)
    report = counts_report(comparison, reference)
    (validation_dir / f"counts_compare_{date}.md").write_text(report, encoding="utf-8")
    return comparison


CLOSE_TOLERANCE = 0.01 + 1e-9
REQUIRED_CLOSE_MATCHES = 48
SPLIT_FACTORS = (2, 3, 4, 5, 10, 15, 20)
SPLIT_RATIO_TOLERANCE = 0.02
VOLUME_RATIO_BAND = (0.05, 0.60)


def adjustment_suspected(ours: float, official: float) -> bool:
    """DQ-10: a price ratio near a common split factor points at an adjusted source."""
    if ours <= 0 or official <= 0:
        return False
    ratio = max(ours, official) / min(ours, official)
    return any(abs(ratio / factor - 1) < SPLIT_RATIO_TOLERANCE for factor in SPLIT_FACTORS)


@dataclass(frozen=True)
class CloseResult:
    symbol: str
    matched: bool
    reason: str


@dataclass(frozen=True)
class CloseComparison:
    date: str
    results: list[CloseResult]
    sources: dict[str, int]

    @property
    def matches(self) -> int:
        return sum(result.matched for result in self.results)

    @property
    def passed(self) -> bool:
        return self.matches >= REQUIRED_CLOSE_MATCHES and all(
            result.matched or result.reason for result in self.results
        )


def classify_close(
    symbol: str,
    cross_price: float | None,
    official: float | None,
    dividend_adjusted: bool,
) -> CloseResult:
    """Compares one closing cross with the official close, and explains any mismatch."""
    if official is None:
        return CloseResult(symbol, False, "no official close in the reference source")
    if cross_price is None or cross_price == 0:
        return CloseResult(symbol, False, "no closing cross on Nasdaq for this symbol")
    if abs(cross_price - official) <= CLOSE_TOLERANCE:
        return CloseResult(symbol, True, "")
    if adjustment_suspected(cross_price, official):
        return CloseResult(symbol, False, "split adjustment suspected in the reference source")
    if dividend_adjusted:
        return CloseResult(symbol, False, "reference close is dividend-adjusted (Stooq snapshot)")
    return CloseResult(symbol, False, "unexplained difference")


def close_report(comparison: CloseComparison) -> str:
    """Aggregate-only: reference prices are licensed, so no close appears here (DECISIONS.md)."""
    verdict = "PASS" if comparison.passed else "FAIL"
    total = len(comparison.results)
    lines = [
        f"# Closing cross against official close, {comparison.date}: {verdict}",
        "",
        f"AC-W-02: {comparison.matches} of {total} symbols equal the official unadjusted close "
        f"to $0.01; at least {REQUIRED_CLOSE_MATCHES} are required.",
        "",
        "Reference rows by source: "
        + ", ".join(f"{name} {count}" for name, count in sorted(comparison.sources.items()))
        + ".",
        "",
    ]
    mismatches = [result for result in comparison.results if not result.matched]
    if mismatches:
        lines += ["| Symbol | Explanation |", "|---|---|"]
        lines += [f"| {result.symbol} | {result.reason} |" for result in mismatches]
    else:
        lines.append("No mismatches.")
    return "\n".join(lines) + "\n"


def validate_close(
    date: str, watchlist: list[str], data_root: Path, reference_path: Path, validation_dir: Path
) -> CloseComparison:
    reference = pl.read_parquet(reference_path)
    official = {row["symbol"]: row for row in reference.iter_rows(named=True)}
    results = []
    for symbol in watchlist:
        row = official.get(symbol)
        results.append(
            classify_close(
                symbol,
                closing_cross_price(data_root, symbol, date),
                row["close"] if row else None,
                bool(row["dividend_adjusted"]) if row else False,
            )
        )
    sources = dict(Counter(str(row["source"]) for row in official.values()))
    comparison = CloseComparison(date, results, sources)
    (validation_dir / f"closing_cross_{date}.md").write_text(
        close_report(comparison), encoding="utf-8"
    )
    return comparison


def closing_cross_price(data_root: Path, symbol: str, date: str) -> float | None:
    trades = outputs.read_partition(data_root, "trades", symbol, date)
    crosses = trades.filter(pl.col("cross_type") == "C")
    return None if crosses.is_empty() else float(crosses["price"][-1])


@dataclass(frozen=True)
class VolumeComparison:
    date: str
    internal_mismatches: list[str]
    cross_mismatches: list[str]
    ratios: dict[str, float]
    outliers: list[str]

    @property
    def median_ratio(self) -> float:
        ordered = sorted(self.ratios.values())
        return ordered[len(ordered) // 2] if ordered else 0.0

    @property
    def passed(self) -> bool:
        return not (self.internal_mismatches or self.cross_mismatches or self.outliers)


def volume_report(comparison: VolumeComparison) -> str:
    """Aggregate-only, like the close report."""
    verdict = "PASS" if comparison.passed else "FAIL"
    ratios = sorted(comparison.ratios.values())
    low, high = VOLUME_RATIO_BAND
    lines = [
        f"# Volume consistency, {comparison.date}: {verdict}",
        "",
        "AC-W-03 (a): trades, bars and the internal counter agree exactly for "
        f"{len(comparison.ratios) - len(comparison.internal_mismatches)} of "
        f"{len(comparison.ratios)} symbols.",
        "",
        "AC-W-03 (b): closing-cross shares in `trades` equal the Q shares for "
        f"{len(comparison.ratios) - len(comparison.cross_mismatches)} of "
        f"{len(comparison.ratios)} symbols.",
        "",
        f"AC-W-03 (c): Nasdaq-venue over official consolidated volume, band [{low}, {high}]: "
        f"median {comparison.median_ratio:.3f}, minimum {ratios[0] if ratios else 0:.3f}, "
        f"maximum {ratios[-1] if ratios else 0:.3f}, {len(comparison.outliers)} outside the band.",
    ]
    for label, symbols in (
        ("Internal mismatches", comparison.internal_mismatches),
        ("Cross mismatches", comparison.cross_mismatches),
        ("Outside the band", comparison.outliers),
    ):
        if symbols:
            lines += ["", f"{label}: {', '.join(symbols)}."]
    return "\n".join(lines) + "\n"


def validate_volume(
    date: str, watchlist: list[str], data_root: Path, reference_path: Path, validation_dir: Path
) -> VolumeComparison:
    report = outputs.read_build_report(data_root, date)["symbols"]
    official = {
        row["symbol"]: row["volume"]
        for row in pl.read_parquet(reference_path).iter_rows(named=True)
    }
    internal: list[str] = []
    cross: list[str] = []
    ratios: dict[str, float] = {}
    outliers: list[str] = []
    low, high = VOLUME_RATIO_BAND
    for symbol in watchlist:
        trades = outputs.read_partition(data_root, "trades", symbol, date)
        counted = trades.filter(pl.col("printable") & ~pl.col("broken"))
        trade_volume = int(counted["qty"].sum())
        bars = outputs.read_partition(data_root, "bars", symbol, date)
        by_interval = bars.group_by("interval").agg(pl.col("volume").sum())["volume"].to_list()
        figures = report[symbol]
        if any(v != trade_volume for v in [*by_interval, figures["counter_volume"]]):
            internal.append(symbol)
        crossed = trades.filter(pl.col("cross_type") == "C")
        if int(crossed["qty"].sum()) != figures["closing_cross_shares"]:
            cross.append(symbol)
        consolidated = official.get(symbol)
        if consolidated:
            ratio = trade_volume / consolidated
            ratios[symbol] = ratio
            if not low <= ratio <= high:
                outliers.append(symbol)
        else:
            outliers.append(symbol)
    comparison = VolumeComparison(date, internal, cross, ratios, outliers)
    (validation_dir / f"volume_{date}.md").write_text(volume_report(comparison), encoding="utf-8")
    return comparison
