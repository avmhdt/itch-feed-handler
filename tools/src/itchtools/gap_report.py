"""The evidence file for the gap-recovery demo (AC-H-03)."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

EXIT_OK = 0
EXIT_UNRECOVERABLE = 5


@dataclass(frozen=True)
class GapRun:
    file_digest: str
    listen_digest: str
    file_counts: dict[str, Any]
    listen_stats: dict[str, Any]
    listen_exit: int
    short_exit: int
    short_log: str


def build_report(run: GapRun) -> dict[str, Any]:
    stats = run.listen_stats
    counts = stats["counts"]
    checks = {
        "digest_equals_file_mode": run.file_digest == run.listen_digest,
        "counts_equal_file_mode": counts["by_type"] == run.file_counts["by_type"]
        and counts["total"] == run.file_counts["total"],
        "no_unrecovered_gaps": run.listen_exit == EXIT_OK and stats["unrecovered"] == 0,
        "short_window_exits_5": run.short_exit == EXIT_UNRECOVERABLE,
        "short_window_message_is_clear": "unrecoverable gap" in run.short_log,
    }
    return {
        "checks": checks,
        "passed": all(checks.values()),
        "digest": run.listen_digest,
        "messages": counts["total"],
        "packets": stats["packets"],
        "gaps": stats["gaps"],
        "gap_messages": stats["gap_messages"],
        "largest_gap": stats["largest_gap"],
        "rerequests": stats["rerequests"],
        "timeouts": stats["timeouts"],
        "duplicate_packets": stats["duplicate_packets"],
        "overflow_packets": stats["overflow_packets"],
        "wall_s": stats["wall_s"],
    }


def write_report(report: dict[str, Any], validation_dir: Path, date: str) -> Path:
    path = validation_dir / f"gap_demo_{date}.json"
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path
