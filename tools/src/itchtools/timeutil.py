"""Eastern-to-UTC conversion for reports, using the tz database as a check on the C++ rule."""

from __future__ import annotations

from datetime import date as Date
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

NEW_YORK = ZoneInfo("America/New_York")


def to_utc_ns(date: str, ns_since_midnight: int) -> int:
    """Nanoseconds since the Unix epoch for a time on an Eastern trading date (DQ-08)."""
    day = Date.fromisoformat(date)
    noon = datetime(day.year, day.month, day.day, 12, tzinfo=NEW_YORK)
    offset = noon.utcoffset()
    assert offset is not None
    midnight_utc = datetime(day.year, day.month, day.day) - offset
    epoch = datetime(1970, 1, 1)
    return (midnight_utc - epoch) // timedelta(microseconds=1) * 1000 + ns_since_midnight


def format_et(ns_since_midnight: int) -> str:
    seconds, nanos = divmod(ns_since_midnight, 1_000_000_000)
    minutes, secs = divmod(seconds, 60)
    hours, mins = divmod(minutes, 60)
    return f"{hours:02d}:{mins:02d}:{secs:02d}.{nanos:09d}"
