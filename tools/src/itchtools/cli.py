"""Command line entry point: `uv run --project tools itchtools ...`."""

from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path
from typing import Annotated

import typer

from itchtools import fetch as fetch_module
from itchtools import figures, gap_report, gen_fixture, outputs, py_baseline, ref_daily, validate
from itchtools.settings import REPO_ROOT, Settings

app = typer.Typer(no_args_is_help=True, add_completion=False)

VALIDATION_DIR = REPO_ROOT / "results" / "validation"
CONFIGS_DIR = REPO_ROOT / "configs"
FIGURES_DIR = REPO_ROOT / "reports" / "figures"
WATCHLIST = CONFIGS_DIR / "watchlist.txt"


def date_from_file(path: Path) -> str:
    """Turns a MMDDYYYY file name prefix into YYYY-MM-DD."""
    match = re.match(r"(\d{2})(\d{2})(\d{4})", path.name)
    if match is None:
        raise typer.BadParameter(f"no MMDDYYYY date in {path.name}")
    month, day, year = match.groups()
    return f"{year}-{month}-{day}"


@app.command()
def fetch(
    days: Annotated[str | None, typer.Option(help="Comma-separated MMDDYYYY days.")] = None,
    decompress: Annotated[bool, typer.Option("--decompress", help="Decompress with pigz.")] = False,
    keep_gz: Annotated[bool, typer.Option("--keep-gz", help="Keep the .gz afterwards.")] = False,
    max_bytes_per_s: Annotated[
        int | None, typer.Option(help="Transfer cap; defaults to ITCH_MAX_BYTES_PER_S.")
    ] = None,
) -> None:
    """Download, verify and optionally decompress sample days, one file at a time."""
    settings = Settings()
    selected = [d.strip() for d in (days or settings.itch_days).split(",") if d.strip()]
    entries = fetch_module.fetch_days(
        selected,
        settings.itch_base_url,
        settings.raw_dir,
        settings.interim_dir,
        max_bytes_per_s=max_bytes_per_s or settings.itch_max_bytes_per_s,
        want_decompress=decompress,
        keep_gz=keep_gz or settings.itch_keep_gz,
    )
    for day, entry in entries.items():
        typer.echo(f"{day}: gz {entry.get('gz_bytes')} bytes, raw {entry.get('raw_bytes')} bytes")


@app.command("gen-fixture")
def gen_fixture_command(
    out: Annotated[Path, typer.Option(help="Fixture path.")] = (
        REPO_ROOT / "tests" / "fixtures" / "mini_day.itchbin"
    ),
    seed: Annotated[int, typer.Option(help="Random seed.")] = 7,
    messages: Annotated[int, typer.Option(help="Approximate message count.")] = (
        gen_fixture.DEFAULT_MESSAGES
    ),
) -> None:
    """Write the deterministic synthetic day and its golden files."""
    stats = gen_fixture.generate(out, seed=seed, messages=messages)
    typer.echo(f"{out}: {stats['total']} messages, {stats['bytes']} bytes")


@app.command("py-baseline")
def py_baseline_command(
    file: Annotated[Path, typer.Option(help="Uncompressed ITCH file.")],
    limit: Annotated[int, typer.Option(help="Messages to read.")] = 20_000_000,
    book: Annotated[bool, typer.Option("--book", help="Also build books.")] = False,
) -> None:
    """Time the pure-Python parser, optionally with the dict-based book."""
    result = py_baseline.run_baseline(file, limit, book)
    rate = result.messages_per_second
    typer.echo(f"{result.messages} messages in {result.seconds:.2f} s, {rate:,.0f} msgs/s")


@app.command("ref-counts")
def ref_counts(
    file: Annotated[Path, typer.Option(help="Uncompressed ITCH file.")],
    date: Annotated[str | None, typer.Option(help="Trading date, YYYY-MM-DD.")] = None,
) -> None:
    """Run the Rust reference counter and store its counts for validate-counts."""
    settings = Settings()
    resolved = date or date_from_file(file)
    out = VALIDATION_DIR / f"ref_counts_{resolved}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [str(settings.itch_ref_parser_bin.expanduser()), str(file), "--json", str(out)],
        check=True,
    )
    typer.echo(f"wrote {out}")


@app.command("validate-counts")
def validate_counts(
    date: Annotated[str, typer.Option(help="Trading date, YYYY-MM-DD.")],
) -> None:
    """AC-W-01: compare `itch stats` with the reference parser."""
    comparison = validate.validate_counts(date, VALIDATION_DIR)
    typer.echo(f"{date}: {'PASS' if comparison.matches else 'FAIL'}")
    if not comparison.matches:
        raise typer.Exit(1)


@app.command("ref-daily")
def ref_daily_command(
    date: Annotated[str, typer.Option(help="Trading date, YYYY-MM-DD.")],
    watchlist: Annotated[Path, typer.Option(help="Watchlist file.")] = WATCHLIST,
    source: Annotated[
        str | None, typer.Option(help="sharadar or stooq; default sharadar when a key is set.")
    ] = None,
) -> None:
    """Extract official close and volume for the watchlist (DQ-10)."""
    settings = Settings()
    symbols = ref_daily.read_watchlist(watchlist)
    symbol_map = ref_daily.read_symbol_map(CONFIGS_DIR / "symbol_map.csv")
    chosen = source or ("sharadar" if settings.sharadar_api_key else "stooq")
    if chosen == "sharadar":
        if not settings.sharadar_api_key:
            typer.echo("SHARADAR_API_KEY is not set", err=True)
            raise typer.Exit(1)
        rows = ref_daily.fetch_sharadar(symbols, date, settings.sharadar_api_key, symbol_map)
    elif chosen == "stooq":
        raw = ref_daily.load_stooq(settings.stooq_dir, symbols, symbol_map)
        splits = ref_daily.read_splits(CONFIGS_DIR / "splits.csv")
        rows = ref_daily.stooq_rows(raw, date, splits)
    else:
        raise typer.BadParameter(f"unknown source {chosen}")
    path = ref_daily.write_daily(rows, settings.reference_dir, date)
    typer.echo(f"{path}: {len(rows)} of {len(symbols)} symbols from {chosen}")


@app.command("validate-close")
def validate_close(
    date: Annotated[str, typer.Option(help="Trading date, YYYY-MM-DD.")],
    watchlist: Annotated[Path, typer.Option(help="Watchlist file.")] = WATCHLIST,
) -> None:
    """AC-W-02: compare closing-cross prices with official closes."""
    settings = Settings()
    comparison = validate.validate_close(
        date,
        ref_daily.read_watchlist(watchlist),
        settings.itch_data_dir.expanduser(),
        ref_daily.daily_path(settings.reference_dir, date),
        VALIDATION_DIR,
    )
    total = len(comparison.results)
    typer.echo(f"{date}: {comparison.matches} of {total} match")
    if not comparison.passed:
        raise typer.Exit(1)


@app.command("validate-volume")
def validate_volume(
    date: Annotated[str, typer.Option(help="Trading date, YYYY-MM-DD.")],
    watchlist: Annotated[Path, typer.Option(help="Watchlist file.")] = WATCHLIST,
) -> None:
    """AC-W-03: internal volume consistency and the venue-share ratio."""
    settings = Settings()
    comparison = validate.validate_volume(
        date,
        ref_daily.read_watchlist(watchlist),
        settings.itch_data_dir.expanduser(),
        ref_daily.daily_path(settings.reference_dir, date),
        VALIDATION_DIR,
    )
    typer.echo(f"{date}: {'PASS' if comparison.passed else 'FAIL'}")
    if not comparison.passed:
        raise typer.Exit(1)


@app.command("gap-report")
def gap_report_command(
    date: Annotated[str, typer.Option(help="Trading date, YYYY-MM-DD.")],
    file_digest: Annotated[Path, typer.Option(help="Digest from file mode.")],
    listen_digest: Annotated[Path, typer.Option(help="Digest from `itch listen`.")],
    file_counts: Annotated[Path, typer.Option(help="`itch stats` JSON.")],
    listen_stats: Annotated[Path, typer.Option(help="`itch listen` stats JSON.")],
    listen_exit: Annotated[int, typer.Option(help="Exit code of the recovery run.")],
    short_exit: Annotated[int, typer.Option(help="Exit code of the short-window run.")],
    short_log: Annotated[Path, typer.Option(help="Log of the short-window run.")],
) -> None:
    """AC-H-03: write the demo's evidence file and gap timeline figure."""
    stats = json.loads(listen_stats.read_text(encoding="utf-8"))
    run = gap_report.GapRun(
        file_digest.read_text(encoding="utf-8").strip(),
        listen_digest.read_text(encoding="utf-8").strip(),
        json.loads(file_counts.read_text(encoding="utf-8")),
        stats,
        listen_exit,
        short_exit,
        short_log.read_text(encoding="utf-8"),
    )
    report = gap_report.build_report(run)
    path = gap_report.write_report(report, VALIDATION_DIR, date)
    figures.gap_timeline(stats["timeline"], FIGURES_DIR / "gap_timeline.png")
    typer.echo(f"{path}: {'PASS' if report['passed'] else 'FAIL'}")
    if not report["passed"]:
        raise typer.Exit(1)


@app.command("content-hash")
def content_hash(
    date: Annotated[str, typer.Option(help="Trading date, YYYY-MM-DD.")],
    root: Annotated[Path | None, typer.Option(help="Data root.")] = None,
) -> None:
    """Print an order-independent hash of a day's Parquet rows (NFR-04)."""
    typer.echo(outputs.content_hash(root or Settings().itch_data_dir.expanduser(), date))


if __name__ == "__main__":
    app()
