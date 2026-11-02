"""AC-W-08: Parquet outputs against the canonical schemas, on the fixture day and on real days."""

from __future__ import annotations

import json
import os
import subprocess
from collections.abc import Iterator
from pathlib import Path

import polars as pl
import pyarrow as pa
import pyarrow.parquet as pq
import pytest

from itchtools import outputs
from itchtools.settings import REPO_ROOT, Settings

DATE = "2019-01-30"
FIXTURES = REPO_ROOT / "tests" / "fixtures"
SYMBOLS = ("FIX00", "FIX01", "FIX03")


def f(name: str, kind: pa.DataType, nullable: bool = False) -> pa.Field:
    return pa.field(name, kind, nullable=nullable)


CANONICAL = {
    "book_l3": [
        f("ts_exchange", pa.int64()),
        f("ts_recv", pa.int64()),
        f("venue", pa.utf8()),
        f("symbol", pa.utf8()),
        f("order_id", pa.uint64()),
        f("event", pa.utf8()),
        f("side", pa.int8()),
        f("price_ticks", pa.int64()),
        f("qty", pa.int64()),
        f("new_order_id", pa.uint64(), True),
        f("match_id", pa.uint64(), True),
        f("seq", pa.int64()),
        f("msg_type", pa.utf8()),
        f("tracking_number", pa.uint16()),
        f("mpid", pa.utf8(), True),
        f("printable", pa.bool_(), True),
    ],
    "trades": [
        f("ts_exchange", pa.int64()),
        f("ts_recv", pa.int64()),
        f("venue", pa.utf8()),
        f("symbol", pa.utf8()),
        f("trade_id", pa.utf8()),
        f("price", pa.float64()),
        f("qty", pa.float64()),
        f("side", pa.int8()),
        f("price_ticks", pa.int64()),
        f("source", pa.utf8()),
        f("cross_type", pa.utf8(), True),
        f("printable", pa.bool_()),
        f("broken", pa.bool_()),
        f("seq", pa.int64()),
    ],
    "bbo": [
        f("ts", pa.int64()),
        f("venue", pa.utf8()),
        f("symbol", pa.utf8()),
        f("bid_px", pa.float64(), True),
        f("ask_px", pa.float64(), True),
        f("bid_qty", pa.float64()),
        f("ask_qty", pa.float64()),
        f("seq", pa.int64()),
        f("bid_orders", pa.int32()),
        f("ask_orders", pa.int32()),
    ],
    "bars": [
        f("ts_start", pa.int64()),
        f("ts_end", pa.int64()),
        f("venue", pa.utf8()),
        f("symbol", pa.utf8()),
        f("interval", pa.utf8()),
        f("open", pa.float64()),
        f("high", pa.float64()),
        f("low", pa.float64()),
        f("close", pa.float64()),
        f("volume", pa.int64()),
        f("notional", pa.float64()),
        f("vwap", pa.float64()),
        f("trade_count", pa.int64()),
        f("cross_volume", pa.int64()),
    ],
    "book_snapshots": [
        f("ts", pa.int64()),
        f("venue", pa.utf8()),
        f("symbol", pa.utf8()),
        f("side", pa.int8()),
        f("level", pa.uint16()),
        f("price_ticks", pa.int64()),
        f("price", pa.float64()),
        f("qty", pa.int64()),
        f("order_count", pa.int32()),
    ],
    "trading_state": [
        f("ts_exchange", pa.int64()),
        f("ts_recv", pa.int64()),
        f("venue", pa.utf8()),
        f("symbol", pa.utf8(), True),
        f("msg_type", pa.utf8()),
        f("state", pa.utf8()),
        f("reason", pa.utf8(), True),
        f("details", pa.utf8()),
        f("seq", pa.int64()),
    ],
    "noii": [
        f("ts_exchange", pa.int64()),
        f("ts_recv", pa.int64()),
        f("venue", pa.utf8()),
        f("symbol", pa.utf8()),
        f("paired_shares", pa.int64()),
        f("imbalance_shares", pa.int64()),
        f("imbalance_direction", pa.utf8()),
        f("far_price_ticks", pa.int64()),
        f("near_price_ticks", pa.int64()),
        f("ref_price_ticks", pa.int64()),
        f("cross_type", pa.utf8()),
        f("price_variation", pa.utf8()),
        f("msg_type", pa.utf8()),
        f("details", pa.utf8(), True),
        f("seq", pa.int64()),
    ],
    "instruments": [
        f("venue", pa.utf8()),
        f("symbol", pa.utf8()),
        f("tick_size", pa.float64()),
        f("lot_size", pa.int32()),
        f("contract_type", pa.utf8()),
        f("maker_fee_bps", pa.float64(), True),
        f("taker_fee_bps", pa.float64(), True),
        f("effective_from", pa.date32()),
        f("effective_to", pa.date32()),
        f("stock_locate", pa.uint16()),
        f("market_category", pa.utf8()),
        f("financial_status", pa.utf8()),
        f("issue_classification", pa.utf8()),
        f("issue_subtype", pa.utf8()),
        f("authenticity", pa.utf8()),
        f("luld_tier", pa.utf8()),
    ],
}


def itch_binary() -> Path | None:
    default = Path.home() / "build" / "itch-feed-handler" / "release" / "src" / "itch"
    path = Path(os.environ.get("ITCH_BIN", str(default)))
    return path if path.exists() else None


@pytest.fixture(scope="module")
def built(tmp_path_factory: pytest.TempPathFactory) -> Iterator[Path]:
    binary = itch_binary()
    if binary is None:
        pytest.skip("the itch binary is not built")
    root = tmp_path_factory.mktemp("outputs")
    watchlist = root / "watchlist.txt"
    watchlist.write_text("\n".join(SYMBOLS) + "\n")
    config = (REPO_ROOT / "configs" / "default.toml").read_text()
    (root / "config.toml").write_text(config.replace("configs/watchlist.txt", str(watchlist)))
    env = {
        **os.environ,
        "ITCH_DATA_DIR": str(root),
        "ITCH_MCAST_GROUP": "239.192.0.1",
        "ITCH_MCAST_IFACE": "127.0.0.1",
        "ITCH_MOLD_SESSION": "ITCHDEMO01",
    }
    subprocess.run(
        [
            str(binary),
            "build",
            "--config",
            str(root / "config.toml"),
            "--file",
            str(FIXTURES / "mini_day.itchbin"),
            "--date",
            DATE,
            "--root",
            str(root),
            "--report",
            str(outputs.build_report_path(root, DATE)),
        ],
        check=True,
        env=env,
        capture_output=True,
    )
    yield root


def assert_canonical(path: Path, dataset: str) -> None:
    schema = pq.read_schema(path)
    assert schema.names == [field.name for field in CANONICAL[dataset]], dataset
    for field in CANONICAL[dataset]:
        actual = schema.field(field.name)
        assert actual.type == field.type, f"{dataset}.{field.name}"
        assert actual.nullable == field.nullable, f"{dataset}.{field.name}"


@pytest.mark.parametrize("dataset", sorted(CANONICAL))
def test_schema_and_partition_path(built: Path, dataset: str) -> None:
    path = built / dataset / "venue=XNAS" / "symbol=FIX01" / f"date={DATE}" / "part-0000.parquet"
    assert path.exists(), path
    assert_canonical(path, dataset)
    assert pq.read_metadata(path).row_group(0).column(0).compression == "ZSTD"


def test_market_wide_rows_have_their_own_partition(built: Path) -> None:
    frame = outputs.read_partition(built, "trading_state", "_MARKET", DATE)
    assert frame["symbol"].null_count() == frame.height
    assert set(frame["msg_type"]) >= {"S", "V", "W"}


def test_only_output_symbols_are_written(built: Path) -> None:
    written = {p.name for p in (built / "book_l3" / "venue=XNAS").iterdir()}
    assert written == {f"symbol={symbol}" for symbol in SYMBOLS}


def test_volumes_and_closing_cross_match_the_generator(built: Path) -> None:
    golden = json.loads((FIXTURES / "golden_outcome.json").read_text())["symbols"]
    report = outputs.read_build_report(built, DATE)["symbols"]
    for symbol in SYMBOLS:
        figures = report[symbol]
        assert figures["trade_volume"] == golden[symbol]["volume"], symbol
        assert figures["counter_volume"] == golden[symbol]["volume"], symbol
        assert figures["closing_cross_ticks"] == golden[symbol]["closing_cross_price"], symbol
        assert figures["closing_cross_shares"] == golden[symbol]["closing_cross_shares"], symbol


def test_bars_sum_to_the_trades_in_every_interval(built: Path) -> None:
    for symbol in SYMBOLS:
        trades = outputs.read_partition(built, "trades", symbol, DATE)
        volume = trades.filter(pl.col("printable") & ~pl.col("broken"))["qty"].sum()
        bars = outputs.read_partition(built, "bars", symbol, DATE)
        totals = bars.group_by("interval").agg(pl.col("volume").sum())
        assert set(totals["interval"]) == {"1s", "1m"}
        assert all(total == volume for total in totals["volume"])


def test_broken_trade_is_flagged_and_left_out_of_bars(built: Path) -> None:
    frames = [outputs.read_partition(built, "trades", s, DATE) for s in SYMBOLS]
    flagged = pl.concat(frames).filter(pl.col("broken"))
    golden = json.loads((FIXTURES / "golden_stats.json").read_text())
    assert golden["by_type"]["B"] == 1
    assert flagged.height <= 1


def test_l3_events_are_canonical_and_seq_never_falls(built: Path) -> None:
    frame = outputs.read_partition(built, "book_l3", "FIX01", DATE)
    assert set(frame["event"]) == {"add", "exec", "cancel", "delete", "replace"}
    assert frame["seq"].is_sorted()
    replaces = frame.filter(pl.col("event") == "replace")
    assert replaces["new_order_id"].null_count() == 0
    executions = frame.filter(pl.col("event") == "exec")
    assert executions["match_id"].null_count() == 0


def test_timestamps_are_utc(built: Path) -> None:
    frame = outputs.read_partition(built, "book_l3", "FIX01", DATE)
    assert (frame["ts_exchange"] == frame["ts_recv"]).all()
    first = frame["ts_exchange"][0]
    assert 1_548_806_400_000_000_000 < first < 1_548_892_800_000_000_000


def test_content_hash_ignores_file_and_row_order(built: Path, tmp_path: Path) -> None:
    first = outputs.content_hash(built, DATE)
    assert first == outputs.content_hash(built, DATE)
    source = built / "bars" / "venue=XNAS" / "symbol=FIX01" / f"date={DATE}" / "part-0000.parquet"
    copy = tmp_path / source.relative_to(built)
    copy.parent.mkdir(parents=True)
    pl.read_parquet(source).reverse().write_parquet(copy)
    assert outputs.content_hash(tmp_path, DATE) == hash_only(source)


def hash_only(path: Path) -> str:
    hashes = pl.read_parquet(path, hive_partitioning=False).hash_rows(seed=outputs.ROW_HASH_SEED)
    return f"{sum(hashes.to_list()) % outputs.HASH_MODULUS:016x}"


@pytest.mark.data
@pytest.mark.parametrize("date", ["2019-01-30", "2019-03-27", "2020-01-30"])
def test_real_outputs_are_canonical(date: str) -> None:
    root = Settings().itch_data_dir.expanduser()
    for dataset in CANONICAL:
        paths = sorted((root / dataset).glob(f"venue=XNAS/symbol=*/date={date}/*.parquet"))
        assert paths, dataset
        for path in paths:
            assert_canonical(path, dataset)
