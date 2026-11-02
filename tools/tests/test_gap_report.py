"""Tests for the gap demo evidence file and its figure."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest
from typer.testing import CliRunner

from itchtools import cli, figures, gap_report

COUNTS = {"by_type": {"A": 3}, "total": 3}
STATS: dict[str, Any] = {
    "counts": COUNTS,
    "packets": 2,
    "gaps": 1,
    "gap_messages": 1,
    "largest_gap": 1,
    "rerequests": 1,
    "timeouts": 0,
    "duplicate_packets": 0,
    "overflow_packets": 0,
    "unrecovered": 0,
    "wall_s": 1.5,
    "timeline": [{"messages": 3, "gaps": 1, "rerequests": 1}],
}


def run(**changes: Any) -> gap_report.GapRun:
    fields: dict[str, Any] = {
        "file_digest": "abc",
        "listen_digest": "abc",
        "file_counts": COUNTS,
        "listen_stats": STATS,
        "listen_exit": 0,
        "short_exit": 5,
        "short_log": "unrecoverable gap at sequence 9",
    }
    fields.update(changes)
    return gap_report.GapRun(**fields)


def test_a_clean_run_passes() -> None:
    assert gap_report.build_report(run())["passed"]


@pytest.mark.parametrize(
    "changes",
    [
        {"listen_digest": "def"},
        {"file_counts": {"by_type": {"A": 2}, "total": 2}},
        {"listen_exit": 5},
        {"listen_stats": {**STATS, "unrecovered": 1}},
        {"short_exit": 0},
        {"short_log": "something else"},
    ],
)
def test_any_failed_check_fails_the_report(changes: dict[str, Any]) -> None:
    assert not gap_report.build_report(run(**changes))["passed"]


def test_figure_is_written(tmp_path: Path) -> None:
    out = tmp_path / "figures" / "gap_timeline.png"
    figures.gap_timeline(STATS["timeline"], out)
    assert out.stat().st_size > 1000


def test_cli_gap_report(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(cli, "VALIDATION_DIR", tmp_path)
    monkeypatch.setattr(cli, "FIGURES_DIR", tmp_path)
    for name, text in (
        ("file_digest", "abc\n"),
        ("listen_digest", "abc\n"),
        ("file_counts", json.dumps(COUNTS)),
        ("listen_stats", json.dumps(STATS)),
        ("short_log", "unrecoverable gap"),
    ):
        (tmp_path / name).write_text(text)
    args = [
        "gap-report",
        "--date",
        "2019-01-30",
        "--file-digest",
        str(tmp_path / "file_digest"),
        "--listen-digest",
        str(tmp_path / "listen_digest"),
        "--file-counts",
        str(tmp_path / "file_counts"),
        "--listen-stats",
        str(tmp_path / "listen_stats"),
        "--listen-exit",
        "0",
        "--short-exit",
        "5",
        "--short-log",
        str(tmp_path / "short_log"),
    ]
    result = CliRunner().invoke(cli.app, args)
    assert result.exit_code == 0, result.output
    assert json.loads((tmp_path / "gap_demo_2019-01-30.json").read_text())["passed"]
    assert (tmp_path / "gap_timeline.png").exists()

    args[args.index("--short-exit") + 1] = "0"
    assert CliRunner().invoke(cli.app, args).exit_code == 1
