"""Reading the handler's Parquet datasets and its build report."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import polars as pl

DATASETS = (
    "bars",
    "bbo",
    "book_l3",
    "book_snapshots",
    "instruments",
    "noii",
    "trades",
    "trading_state",
)
ROW_HASH_SEED = 20190130
HASH_MODULUS = 1 << 64


def partition_glob(root: Path, dataset: str, symbol: str, date: str) -> str:
    return str(root / dataset / "venue=XNAS" / f"symbol={symbol}" / f"date={date}" / "*.parquet")


def read_partition(root: Path, dataset: str, symbol: str, date: str) -> pl.DataFrame:
    return pl.read_parquet(partition_glob(root, dataset, symbol, date), hive_partitioning=False)


def build_report_path(root: Path, date: str) -> Path:
    """Per-symbol figures stay under the data root, out of git."""
    return root / "reports" / f"build_{date}.json"


def read_build_report(root: Path, date: str) -> dict[str, Any]:
    loaded: dict[str, Any] = json.loads(build_report_path(root, date).read_text(encoding="utf-8"))
    return loaded


def content_hash(root: Path, date: str) -> str:
    """Order-independent hash of every row of a day, across all datasets (NFR-04).

    Each row hashes separately and the hashes are added, so the result does not depend on row or
    file order and can be compared across runs and thread counts.
    """
    total = 0
    for dataset in DATASETS:
        for path in sorted((root / dataset).glob(f"venue=XNAS/symbol=*/date={date}/*.parquet")):
            frame = pl.read_parquet(path, hive_partitioning=False)
            hashes = frame.hash_rows(seed=ROW_HASH_SEED).to_list()
            total = (total + sum(hashes)) % HASH_MODULUS
    return f"{total:016x}"
