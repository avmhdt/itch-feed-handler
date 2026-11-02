"""AC-W-10 on real files: market open lands on 14:30 UTC in winter and 13:30 UTC in summer."""

from __future__ import annotations

import json
from datetime import UTC, datetime

import pytest

from itchtools.settings import REPO_ROOT

EXPECTED = {
    "2019-01-30": datetime(2019, 1, 30, 14, 30, tzinfo=UTC),
    "2019-03-27": datetime(2019, 3, 27, 13, 30, tzinfo=UTC),
    "2020-01-30": datetime(2020, 1, 30, 14, 30, tzinfo=UTC),
}


@pytest.mark.data
@pytest.mark.parametrize("date", sorted(EXPECTED))
def test_market_open_is_at_the_expected_utc_time(date: str) -> None:
    path = REPO_ROOT / "results" / "validation" / f"counts_{date}.json"
    report = json.loads(path.read_text(encoding="utf-8"))
    event = next(e for e in report["system_events"] if e["code"] == "Q")
    opened = datetime.fromtimestamp(event["utc_ns"] // 1_000_000_000, tz=UTC)
    assert opened == EXPECTED[date]
    assert event["et"].startswith("09:30:00")
