"""AC-H-08: the sibling repos can read these outputs by partition, using DuckDB."""

from __future__ import annotations

from pathlib import Path

import duckdb
import pytest

from itchtools.settings import Settings

DATE = "2019-01-30"
QUERY = """
SELECT t.symbol,
       SUM(t.qty) FILTER (WHERE t.printable AND NOT t.broken) AS volume,
       (SELECT COUNT(*) FROM read_parquet('{book_l3}', hive_partitioning = true)) AS l3_rows
FROM read_parquet('{trades}', hive_partitioning = true) AS t
GROUP BY t.symbol
"""


def run_query(root: Path, symbol: str) -> tuple[str, float, int]:
    partition = f"venue=XNAS/symbol={symbol}/date={DATE}/*.parquet"
    query = QUERY.format(
        trades=root / "trades" / partition,
        book_l3=root / "book_l3" / partition,
    )
    row = duckdb.sql(query).fetchone()
    assert row is not None
    return row[0], row[1], row[2]


@pytest.mark.data
def test_duckdb_reads_trades_and_l3_by_partition() -> None:
    symbol, volume, l3_rows = run_query(Settings().itch_data_dir.expanduser(), "AAPL")
    assert symbol == "AAPL"
    assert volume > 0
    assert l3_rows > 0
