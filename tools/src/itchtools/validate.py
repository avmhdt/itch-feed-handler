"""Comparison of handler output with external references (FR-21)."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class CountsComparison:
    date: str
    rows: list[tuple[str, int, int]]  # type, handler count, reference count
    handler_total: int
    reference_total: int
    malformed: int
    reference_errors: int

    @property
    def matches(self) -> bool:
        return (
            all(ours == theirs for _, ours, theirs in self.rows)
            and self.handler_total == self.reference_total
            and self.malformed == 0
            and self.reference_errors == 0
        )


def compare_counts(ours: dict[str, Any], reference: dict[str, Any]) -> CountsComparison:
    """AC-W-01: every type count and the total must be equal, with nothing malformed."""
    ours_by_type: dict[str, int] = ours["by_type"]
    theirs_by_type: dict[str, int] = reference["by_type"]
    types = sorted(set(ours_by_type) | set(theirs_by_type))
    rows = [(t, ours_by_type.get(t, 0), theirs_by_type.get(t, 0)) for t in types]
    return CountsComparison(
        date=str(ours.get("date")),
        rows=rows,
        handler_total=int(ours["total"]),
        reference_total=int(reference["total"]),
        malformed=int(ours["malformed"]),
        reference_errors=int(reference.get("errors", 0)),
    )


def counts_report(comparison: CountsComparison, reference: dict[str, Any]) -> str:
    verdict = "PASS" if comparison.matches else "FAIL"
    lines = [
        f"# Message counts, {comparison.date}: {verdict}",
        "",
        "AC-W-01: `itch stats` against the `itchy` reference parser, exact equality by type.",
        "",
        "| Type | itch | reference | Difference |",
        "|---|---:|---:|---:|",
    ]
    lines += [f"| {t} | {a:,} | {b:,} | {a - b:,} |" for t, a, b in comparison.rows]
    lines += [
        f"| Total | {comparison.handler_total:,} | {comparison.reference_total:,} | "
        f"{comparison.handler_total - comparison.reference_total:,} |",
        "",
        f"Malformed frames: {comparison.malformed}. Reference parse errors: "
        f"{comparison.reference_errors}.",
    ]
    framed_only = reference.get("framed_only", {})
    if framed_only:
        kinds = ", ".join(sorted(framed_only))
        lines += [
            "",
            f"`itchy` has no parser for type(s) {kinds}, so `ref_counts` counts those from the",
            "frame header and passes everything else through `itchy`.",
        ]
    return "\n".join(lines) + "\n"


def validate_counts(date: str, validation_dir: Path) -> CountsComparison:
    """Compares the two count files for a date and writes the report next to them."""
    ours = json.loads((validation_dir / f"counts_{date}.json").read_text(encoding="utf-8"))
    reference_path = validation_dir / f"ref_counts_{date}.json"
    reference = json.loads(reference_path.read_text(encoding="utf-8"))
    comparison = compare_counts(ours, reference)
    report = counts_report(comparison, reference)
    (validation_dir / f"counts_compare_{date}.md").write_text(report, encoding="utf-8")
    return comparison
