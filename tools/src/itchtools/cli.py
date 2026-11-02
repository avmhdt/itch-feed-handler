"""Command line entry point: `uv run --project tools itchtools ...`."""

from __future__ import annotations

from typing import Annotated

import typer

from itchtools import fetch as fetch_module
from itchtools.settings import Settings

app = typer.Typer(no_args_is_help=True, add_completion=False)


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


@app.command()
def version() -> None:
    """Print the tool version."""
    typer.echo("itchtools 0.1.0")


if __name__ == "__main__":
    app()
