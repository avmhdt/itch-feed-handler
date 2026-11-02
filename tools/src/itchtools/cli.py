"""Command line entry point: `uv run --project tools itchtools ...`."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path
from typing import Annotated

import typer

from itchtools import fetch as fetch_module
from itchtools import gen_fixture, py_baseline, validate
from itchtools.settings import REPO_ROOT, Settings

app = typer.Typer(no_args_is_help=True, add_completion=False)

VALIDATION_DIR = REPO_ROOT / "results" / "validation"


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


if __name__ == "__main__":
    app()
