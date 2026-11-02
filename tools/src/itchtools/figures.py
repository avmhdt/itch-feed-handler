"""Static figures for the README. Each takes aggregates and writes one PNG."""

from __future__ import annotations

from pathlib import Path
from typing import Any

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt


def gap_timeline(timeline: list[dict[str, Any]], out: Path) -> None:
    """Messages delivered and gaps opened per second of the demo (AC-H-03)."""
    seconds = list(range(len(timeline)))
    figure, (rate, gaps) = plt.subplots(2, 1, figsize=(9, 5), sharex=True)
    rate.plot(seconds, [b["messages"] / 1e6 for b in timeline], color="#1f6fb2")
    rate.set_ylabel("Messages delivered\n(millions per second)")
    gaps.plot(seconds, [b["gaps"] for b in timeline], color="#c8553d", label="Gaps opened")
    gaps.plot(seconds, [b["rerequests"] for b in timeline], color="#6a6a6a", label="Re-requests")
    gaps.set_ylabel("Count per second")
    gaps.set_xlabel("Seconds since first packet")
    gaps.legend(frameon=False)
    for axis in (rate, gaps):
        axis.spines["top"].set_visible(False)
        axis.spines["right"].set_visible(False)
    rate.set_title("Gap recovery during a full-day replay")
    figure.tight_layout()
    out.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(out, dpi=150)
    plt.close(figure)
