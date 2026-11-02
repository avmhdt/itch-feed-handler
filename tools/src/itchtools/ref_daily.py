"""Official daily close and volume for the watchlist (P-11, DQ-10).

Sharadar SEP/SFP is the primary source, with the key in the `x-api-key` header so that it stays out
of URLs and logs. The owner's local Stooq snapshot is the fallback. Stooq rows are split-adjusted
and, for dividend payers, dividend-adjusted as well, so only the split part can be undone from
`configs/splits.csv`; rows from such symbols are tagged so the close check can say so.

Licensed rows stay under `data/`, which git ignores. Reports built from them are aggregate-only.
"""

from __future__ import annotations

import csv
import io
from dataclasses import dataclass
from datetime import date as Date
from pathlib import Path

import httpx
import polars as pl

SHARADAR_BASE_URL = "https://api.sharadar.com/v1.0"
ETF_SYMBOLS = frozenset({"QQQ"})
SNAPSHOT_DATE = "2026-10-30"
COLUMNS = ("symbol", "close", "volume", "source", "dividend_adjusted")


class ReferenceError(RuntimeError):
    """A reference source could not supply what was asked of it."""


@dataclass(frozen=True)
class DailyRow:
    symbol: str
    close: float
    volume: float
    source: str
    dividend_adjusted: bool


def read_watchlist(path: Path) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    return [line.strip() for line in lines if line.strip() and not line.startswith("#")]


def read_symbol_map(path: Path) -> dict[str, str]:
    """Ticker in the sample file to current ticker, for renames such as FB to META (DQ-10)."""
    with path.open(encoding="utf-8", newline="") as handle:
        return {row["symbol"]: row["current"] for row in csv.DictReader(handle)}


def read_splits(path: Path) -> dict[str, list[tuple[str, float]]]:
    """Symbol to (effective date, factor) pairs."""
    splits: dict[str, list[tuple[str, float]]] = {}
    with path.open(encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            splits.setdefault(row["symbol"], []).append((row["date"], float(row["factor"])))
    return splits


def split_factor(splits: list[tuple[str, float]], date: str) -> float:
    """Combined factor of the splits after `date`: unadjusted = adjusted * factor."""
    factor = 1.0
    for effective, ratio in splits:
        if effective > date:
            factor *= ratio
    return factor


def fetch_sharadar(
    symbols: list[str],
    date: str,
    api_key: str,
    symbol_map: dict[str, str],
    transport: httpx.BaseTransport | None = None,
) -> list[DailyRow]:
    """One row per symbol from SEP, or SFP for funds. Sharadar's `volume` is split-adjusted, so it
    is scaled back with the ratio of `closeunadj` to `close`."""
    rows: list[DailyRow] = []
    with httpx.Client(
        base_url=SHARADAR_BASE_URL,
        headers={"x-api-key": api_key},
        transport=transport,
        timeout=httpx.Timeout(60.0),
    ) as client:
        for symbol in symbols:
            ticker = symbol_map.get(symbol, symbol)
            table = "funds" if symbol in ETF_SYMBOLS else "stocks"
            response = client.get(f"/data/{table}", params={"ticker": ticker, "date": date})
            response.raise_for_status()
            found = list(csv.DictReader(io.StringIO(response.text)))
            if not found:
                continue
            record = found[0]
            close = float(record["close"])
            unadjusted = float(record["closeunadj"])
            factor = close / unadjusted if unadjusted else 1.0
            rows.append(
                DailyRow(symbol, unadjusted, float(record["volume"]) * factor, "sharadar", False)
            )
    return rows


def stooq_index(stooq_dir: Path) -> dict[str, Path]:
    """Lower-case ticker to path, by walking the US tree once rather than guessing paths."""
    root = stooq_dir / "d_us_txt"
    return {path.name.removesuffix(".us.txt"): path for path in root.rglob("*.us.txt")}


def load_stooq(stooq_dir: Path, symbols: list[str], symbol_map: dict[str, str]) -> pl.DataFrame:
    """Reads each symbol's whole history once into one table of raw rows."""
    index = stooq_index(stooq_dir)
    frames = []
    for symbol in symbols:
        path = index.get(symbol_map.get(symbol, symbol).lower())
        if path is None:
            continue
        frame = pl.read_csv(path, schema_overrides={"<DATE>": pl.String})
        frames.append(
            frame.select(
                pl.lit(symbol).alias("symbol"),
                pl.col("<DATE>").alias("yyyymmdd"),
                pl.col("<CLOSE>").cast(pl.Float64).alias("close"),
                pl.col("<VOL>").cast(pl.Float64).alias("volume"),
            )
        )
    return pl.concat(frames) if frames else pl.DataFrame()


def stooq_rows(
    raw: pl.DataFrame, date: str, splits: dict[str, list[tuple[str, float]]]
) -> list[DailyRow]:
    """Rows for one date, with later splits undone (DQ-10).

    Dividend adjustment cannot be undone from the snapshot, so every Stooq row is tagged as
    possibly dividend-adjusted.
    """
    compact = Date.fromisoformat(date).strftime("%Y%m%d")
    rows = []
    for record in raw.filter(pl.col("yyyymmdd") == compact).iter_rows(named=True):
        factor = split_factor(splits.get(record["symbol"], []), date)
        rows.append(
            DailyRow(
                record["symbol"],
                record["close"] * factor,
                record["volume"] / factor,
                f"stooq:{SNAPSHOT_DATE}",
                True,
            )
        )
    return rows


def to_frame(rows: list[DailyRow]) -> pl.DataFrame:
    return pl.DataFrame(
        {name: [getattr(row, name) for row in rows] for name in COLUMNS},
        schema={
            "symbol": pl.String,
            "close": pl.Float64,
            "volume": pl.Float64,
            "source": pl.String,
            "dividend_adjusted": pl.Boolean,
        },
    )


def daily_path(reference_dir: Path, date: str) -> Path:
    return reference_dir / f"daily_{date}.parquet"


def write_daily(rows: list[DailyRow], reference_dir: Path, date: str) -> Path:
    reference_dir.mkdir(parents=True, exist_ok=True)
    path = daily_path(reference_dir, date)
    to_frame(rows).write_parquet(path)
    return path
