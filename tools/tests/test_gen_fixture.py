"""Properties of the synthetic day: deterministic, ordered, complete and within budget."""

from __future__ import annotations

import json
import struct
from pathlib import Path

from hypothesis import given, settings
from hypothesis import strategies as st

from itchtools import gen_fixture
from itchtools.settings import REPO_ROOT

ALL_TYPES = set("SRHYLVWKJhAFECXDUPQBINO")
LENGTHS = {
    "S": 12, "R": 39, "H": 25, "Y": 20, "L": 26, "V": 35, "W": 12, "K": 28, "J": 35, "h": 21,
    "A": 36, "F": 40, "E": 31, "C": 36, "X": 23, "D": 19, "U": 35, "P": 44, "Q": 40, "B": 19,
    "I": 50, "N": 20, "O": 48,
}  # fmt: skip
FIXTURES = REPO_ROOT / "tests" / "fixtures"


def frames(data: bytes) -> list[bytes]:
    out = []
    offset = 0
    while offset < len(data):
        (length,) = struct.unpack_from(">H", data, offset)
        out.append(data[offset + 2 : offset + 2 + length])
        offset += 2 + length
    assert offset == len(data)
    return out


def test_every_message_has_its_spec_length(tmp_path: Path) -> None:
    gen_fixture.generate(tmp_path / "day.itchbin")
    messages = frames((tmp_path / "day.itchbin").read_bytes())
    for message in messages:
        assert len(message) == LENGTHS[chr(message[0])]


def test_every_type_appears_and_timestamps_never_fall(tmp_path: Path) -> None:
    stats = gen_fixture.generate(tmp_path / "day.itchbin")
    assert set(stats["by_type"]) == ALL_TYPES
    messages = frames((tmp_path / "day.itchbin").read_bytes())
    stamps = [int.from_bytes(m[5:11], "big") for m in messages]
    assert stamps == sorted(stamps)
    assert stats["total"] == len(messages)


def test_committed_fixture_is_current(tmp_path: Path) -> None:
    gen_fixture.generate(tmp_path / "mini_day.itchbin")
    for name in (
        "mini_day.itchbin",
        "golden_stats.json",
        "golden_symbols.json",
        "golden_outcome.json",
    ):
        assert (tmp_path / name).read_bytes() == (FIXTURES / name).read_bytes(), name


def test_fixture_stays_under_the_size_limit() -> None:
    assert (FIXTURES / "mini_day.itchbin").stat().st_size < 1_000_000


def test_accounting_identity_holds_for_every_symbol() -> None:
    outcome = json.loads((FIXTURES / "golden_outcome.json").read_text())
    assert len(outcome["symbols"]) == gen_fixture.SYMBOL_COUNT
    for symbol in outcome["symbols"].values():
        assert symbol["added"] == (
            symbol["executed"]
            + symbol["cancelled"]
            + symbol["deleted"]
            + symbol["replaced_out"]
            + symbol["resting_shares"]
        )
        assert symbol["closing_cross_shares"] > 0


@settings(max_examples=8, deadline=None)
@given(seed=st.integers(0, 2**32), messages=st.integers(800, 2500))
def test_any_seed_gives_a_consistent_day(tmp_path_factory, seed: int, messages: int) -> None:  # type: ignore[no-untyped-def]
    out = tmp_path_factory.mktemp("day") / "day.itchbin"
    stats = gen_fixture.generate(out, seed=seed, messages=messages)
    assert set(stats["by_type"]) >= ALL_TYPES - {"E", "C", "P", "X", "U", "F"}
    twin = out.parent / "twin.itchbin"
    gen_fixture.generate(twin, seed=seed, messages=messages)
    assert out.read_bytes() == twin.read_bytes()
