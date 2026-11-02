"""Tests for official-close extraction and the close and volume validators."""

from __future__ import annotations

import json
from pathlib import Path

import httpx
import polars as pl
import pytest
from typer.testing import CliRunner

from itchtools import cli, ref_daily, validate
from itchtools.ref_daily import DailyRow
from itchtools.settings import REPO_ROOT

DATE = "2019-01-30"


def test_config_files_parse() -> None:
    symbols = ref_daily.read_watchlist(REPO_ROOT / "configs" / "watchlist.txt")
    assert len(symbols) == 50
    assert len(set(symbols)) == 50
    assert ref_daily.read_symbol_map(REPO_ROOT / "configs" / "symbol_map.csv")["FB"] == "META"
    splits = ref_daily.read_splits(REPO_ROOT / "configs" / "splits.csv")
    assert ref_daily.split_factor(splits["TSLA"], DATE) == 15
    assert ref_daily.split_factor(splits["TSLA"], "2021-01-01") == 3
    assert ref_daily.split_factor([], DATE) == 1


def test_sharadar_uses_the_header_for_the_key_and_unadjusts_volume() -> None:
    seen: list[httpx.Request] = []

    def handle(request: httpx.Request) -> httpx.Response:
        seen.append(request)
        if request.url.params["ticker"] == "MISSING":
            return httpx.Response(200, text="ticker,date,close,closeunadj,volume\n")
        return httpx.Response(
            200, text="ticker,date,close,closeunadj,volume\nAAPL,2019-01-30,41.3,165.2,244000000\n"
        )

    rows = ref_daily.fetch_sharadar(
        ["AAPL", "QQQ", "MISSING"], DATE, "secret", {}, transport=httpx.MockTransport(handle)
    )
    assert rows[0].close == 165.2
    assert rows[0].volume == pytest.approx(61_000_000)
    assert not rows[0].dividend_adjusted
    assert len(rows) == 2
    assert all(r.headers["x-api-key"] == "secret" for r in seen)
    assert all("secret" not in str(r.url) for r in seen)
    assert seen[0].url.path == "/v1.0/data/stocks"
    assert seen[1].url.path == "/v1.0/data/funds"


def stooq_tree(root: Path) -> Path:
    folder = root / "d_us_txt" / "data" / "daily" / "us" / "nasdaq stocks" / "1"
    folder.mkdir(parents=True)
    header = "<TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>\n"
    (folder / "aapl.us.txt").write_text(header + "AAPL.US,D,20190130,000000,1,2,1,41.0,1000.5,0\n")
    (folder / "meta.us.txt").write_text(header + "META.US,D,20190130,000000,1,2,1,150.4,500,0\n")
    return root


def test_stooq_finds_renames_and_undoes_splits(tmp_path: Path) -> None:
    raw = ref_daily.load_stooq(stooq_tree(tmp_path), ["AAPL", "FB", "ATVI"], {"FB": "META"})
    rows = ref_daily.stooq_rows(raw, DATE, {"AAPL": [("2020-08-31", 4.0)]})
    assert {row.symbol for row in rows} == {"AAPL", "FB"}
    apple = next(row for row in rows if row.symbol == "AAPL")
    assert apple.close == 164.0
    assert apple.volume == pytest.approx(250.125)
    assert apple.dividend_adjusted


def test_stooq_with_no_files_is_empty(tmp_path: Path) -> None:
    assert ref_daily.load_stooq(tmp_path, ["AAPL"], {}).is_empty()


def test_write_daily_round_trips(tmp_path: Path) -> None:
    row = DailyRow("AAPL", 165.25, 1e6, "sharadar", False)
    path = ref_daily.write_daily([row], tmp_path, DATE)
    frame = pl.read_parquet(path)
    assert frame["symbol"].to_list() == ["AAPL"]


@pytest.mark.parametrize(
    "cross,official,adjusted,matched,reason",
    [
        (165.25, 165.25, False, True, ""),
        (165.25, 165.26, False, True, ""),
        (165.25, 165.27, False, False, "unexplained"),
        (165.25, None, False, False, "no official close"),
        (None, 165.25, False, False, "no closing cross"),
        (0.0, 165.25, False, False, "no closing cross"),
        (165.25, 41.31, False, False, "split adjustment"),
        (165.25, 156.97, True, False, "dividend-adjusted"),
    ],
)
def test_close_classification(
    cross: float | None, official: float | None, adjusted: bool, matched: bool, reason: str
) -> None:
    result = validate.classify_close("AAPL", cross, official, adjusted)
    assert result.matched is matched
    assert reason in result.reason


def test_adjustment_ignores_bad_prices() -> None:
    assert not validate.adjustment_suspected(0, 10)
    assert not validate.adjustment_suspected(10, 10.5)


def partition(root: Path, dataset: str, symbol: str) -> Path:
    path = root / dataset / "venue=XNAS" / f"symbol={symbol}" / f"date={DATE}"
    path.mkdir(parents=True)
    return path / "part-0000.parquet"


def fake_day(root: Path, symbols: list[str], *, counter_shift: int = 0) -> None:
    report: dict[str, dict[str, dict[str, int]]] = {"symbols": {}}
    for symbol in symbols:
        pl.DataFrame(
            {
                "qty": [100.0, 400.0, 50.0],
                "price": [10.0, 165.25, 11.0],
                "printable": [True, True, True],
                "broken": [False, False, True],
                "cross_type": [None, "C", None],
            },
            schema_overrides={"cross_type": pl.String},
        ).write_parquet(partition(root, "trades", symbol))
        pl.DataFrame({"interval": ["1s", "1m"], "volume": [500, 500]}).write_parquet(
            partition(root, "bars", symbol)
        )
        report["symbols"][symbol] = {
            "counter_volume": 500 + counter_shift,
            "closing_cross_shares": 400,
        }
    (root / "reports").mkdir()
    (root / "reports" / f"build_{DATE}.json").write_text(json.dumps(report))


def reference(root: Path, rows: list[DailyRow]) -> Path:
    return ref_daily.write_daily(rows, root, DATE)


def test_validators_pass_on_a_consistent_day(tmp_path: Path) -> None:
    symbols = [f"S{i}" for i in range(50)]
    fake_day(tmp_path, symbols)
    rows = [DailyRow(s, 165.25, 2000.0, "sharadar", False) for s in symbols]
    path = reference(tmp_path, rows)

    close = validate.validate_close(DATE, symbols, tmp_path, path, tmp_path)
    assert close.passed
    assert "No mismatches" in (tmp_path / f"closing_cross_{DATE}.md").read_text()
    assert "165.25" not in (tmp_path / f"closing_cross_{DATE}.md").read_text()

    volume = validate.validate_volume(DATE, symbols, tmp_path, path, tmp_path)
    assert volume.passed
    assert volume.median_ratio == 0.25
    assert "PASS" in (tmp_path / f"volume_{DATE}.md").read_text()


def test_validators_report_each_failure(tmp_path: Path) -> None:
    symbols = ["AAA", "BBB", "CCC"]
    fake_day(tmp_path, symbols, counter_shift=1)
    rows = [
        DailyRow("AAA", 165.25, 1_000_000.0, "sharadar", False),
        DailyRow("BBB", 1.0, 2000.0, "sharadar", False),
    ]
    path = reference(tmp_path, rows)

    close = validate.validate_close(DATE, symbols, tmp_path, path, tmp_path)
    assert close.matches == 1
    assert not close.passed
    text = (tmp_path / f"closing_cross_{DATE}.md").read_text()
    assert "FAIL" in text
    assert "| CCC | no official close" in text

    volume = validate.validate_volume(DATE, symbols, tmp_path, path, tmp_path)
    assert not volume.passed
    assert set(volume.internal_mismatches) == set(symbols)
    assert set(volume.outliers) == {"AAA", "CCC"}
    assert "Outside the band: AAA, CCC." in (tmp_path / f"volume_{DATE}.md").read_text()


def test_cli_ref_daily_prefers_stooq_without_a_key(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    stooq_tree(tmp_path / "stooq")
    watchlist = tmp_path / "w.txt"
    watchlist.write_text("AAPL\n")
    monkeypatch.setenv("ITCH_DATA_DIR", str(tmp_path))
    monkeypatch.setenv("STOOQ_DIR", str(tmp_path / "stooq"))
    monkeypatch.setenv("SHARADAR_API_KEY", "")
    args = ["ref-daily", "--date", DATE, "--watchlist", str(watchlist)]
    result = CliRunner().invoke(cli.app, args)
    assert result.exit_code == 0, result.output
    assert "1 of 1 symbols from stooq" in result.output

    missing = CliRunner().invoke(cli.app, [*args, "--source", "sharadar"])
    assert missing.exit_code == 1
    unknown = CliRunner().invoke(cli.app, [*args, "--source", "tiingo"])
    assert unknown.exit_code != 0


def test_cli_ref_daily_uses_sharadar_with_a_key(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    watchlist = tmp_path / "w.txt"
    watchlist.write_text("AAPL\n")
    monkeypatch.setenv("ITCH_DATA_DIR", str(tmp_path))
    monkeypatch.setenv("SHARADAR_API_KEY", "key")
    row = DailyRow("AAPL", 165.25, 1e6, "sharadar", False)
    monkeypatch.setattr(ref_daily, "fetch_sharadar", lambda *_: [row])
    result = CliRunner().invoke(
        cli.app, ["ref-daily", "--date", DATE, "--watchlist", str(watchlist)]
    )
    assert result.exit_code == 0, result.output
    assert "from sharadar" in result.output


def test_cli_validators_set_the_exit_code(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    symbols = ["AAA"]
    fake_day(tmp_path, symbols)
    reference_dir = tmp_path / "reference"
    reference(reference_dir, [DailyRow("AAA", 1.0, 2000.0, "sharadar", False)])
    watchlist = tmp_path / "w.txt"
    watchlist.write_text("AAA\n")
    monkeypatch.setenv("ITCH_DATA_DIR", str(tmp_path))
    monkeypatch.setattr(cli, "VALIDATION_DIR", tmp_path)
    args = ["--date", DATE, "--watchlist", str(watchlist)]
    assert CliRunner().invoke(cli.app, ["validate-close", *args]).exit_code == 1
    assert CliRunner().invoke(cli.app, ["validate-volume", *args]).exit_code == 0
