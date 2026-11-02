"""Tests for the counts comparison, the CLI around it and report wording."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest
from typer.testing import CliRunner

from itchtools import cli, timeutil, validate

OURS: dict[str, Any] = {
    "by_type": {"A": 3, "h": 2},
    "date": "2019-01-30",
    "malformed": 0,
    "total": 5,
}
REFERENCE: dict[str, Any] = {"by_type": {"A": 3, "h": 2}, "errors": 0, "total": 5}


def write_pair(folder: Path, ours: dict[str, Any], reference: dict[str, Any]) -> None:
    (folder / "counts_2019-01-30.json").write_text(json.dumps(ours))
    (folder / "ref_counts_2019-01-30.json").write_text(json.dumps(reference))


def test_equal_counts_pass() -> None:
    assert validate.compare_counts(OURS, REFERENCE).matches


@pytest.mark.parametrize(
    "ours,reference",
    [
        ({**OURS, "by_type": {"A": 2, "h": 2}, "total": 4}, REFERENCE),
        ({**OURS, "by_type": {"A": 3, "h": 2, "D": 1}, "total": 6}, REFERENCE),
        ({**OURS, "malformed": 1}, REFERENCE),
        (OURS, {**REFERENCE, "errors": 1}),
        (OURS, {**REFERENCE, "total": 6}),
    ],
)
def test_any_difference_fails(ours: dict[str, Any], reference: dict[str, Any]) -> None:
    assert not validate.compare_counts(ours, reference).matches


def test_report_names_the_verdict_and_framed_only_types(tmp_path: Path) -> None:
    write_pair(tmp_path, OURS, {**REFERENCE, "framed_only": {"h": 2}})
    assert validate.validate_counts("2019-01-30", tmp_path).matches
    report = (tmp_path / "counts_compare_2019-01-30.md").read_text()
    assert "PASS" in report
    assert "no parser for type(s) h" in report


def test_cli_validate_counts_fails_on_a_difference(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    write_pair(tmp_path, OURS, {**REFERENCE, "total": 6})
    monkeypatch.setattr(cli, "VALIDATION_DIR", tmp_path)
    result = CliRunner().invoke(cli.app, ["validate-counts", "--date", "2019-01-30"])
    assert result.exit_code == 1
    assert "FAIL" in result.output


def test_cli_ref_counts_runs_the_binary(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    calls: list[list[str]] = []

    def fake_run(argv: list[str], check: bool) -> None:
        assert check
        calls.append(argv)

    monkeypatch.setattr(cli, "VALIDATION_DIR", tmp_path)
    monkeypatch.setattr(cli.subprocess, "run", fake_run)
    monkeypatch.setenv("ITCH_REF_PARSER_BIN", "/opt/ref_counts")
    result = CliRunner().invoke(cli.app, ["ref-counts", "--file", "/x/03272019.NASDAQ_ITCH50"])
    assert result.exit_code == 0, result.output
    assert calls[0][0] == "/opt/ref_counts"
    assert calls[0][-1].endswith("ref_counts_2019-03-27.json")


def test_date_from_file_rejects_other_names() -> None:
    assert cli.date_from_file(Path("01302020.NASDAQ_ITCH50")) == "2020-01-30"
    with pytest.raises(Exception, match="no MMDDYYYY"):
        cli.date_from_file(Path("mini_day.itchbin"))


def test_cli_gen_fixture(tmp_path: Path) -> None:
    out = tmp_path / "day.itchbin"
    result = CliRunner().invoke(cli.app, ["gen-fixture", "--out", str(out), "--messages", "900"])
    assert result.exit_code == 0, result.output
    assert out.exists()


def test_timeutil_matches_the_spec_examples() -> None:
    open_ns = (9 * 3600 + 30 * 60) * 1_000_000_000
    assert timeutil.to_utc_ns("2019-01-30", open_ns) == 1_548_858_600 * 1_000_000_000
    assert timeutil.to_utc_ns("2019-03-27", open_ns) == 1_553_693_400 * 1_000_000_000
    assert timeutil.format_et(open_ns + 5) == "09:30:00.000000005"
