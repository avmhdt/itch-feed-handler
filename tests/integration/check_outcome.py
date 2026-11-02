#!/usr/bin/env python3
"""Compares an `itch check --per-symbol` report with the generator's known outcome."""

import json
import sys


def main() -> None:
    report = json.load(open(sys.argv[1], encoding="utf-8"))
    golden = json.load(open(sys.argv[2], encoding="utf-8"))["symbols"]

    for key in (
        "accounting_failures",
        "crossed_outside_auction",
        "duplicate_adds",
        "orphans",
        "over_executions",
        "timestamp_regressions",
        "undefined_locates",
    ):
        assert report[key] == 0, f"{key} = {report[key]}"
    assert report["crossed_in_auction"] > 0, "the fixture should cross inside its auctions"
    assert report["broken_trades"] == 1

    assert set(report["symbols"]) == set(golden)
    for symbol, expected in golden.items():
        actual = report["symbols"][symbol]
        for key in (
            "added",
            "cancelled",
            "deleted",
            "executed",
            "replaced_out",
            "resting_orders",
            "resting_shares",
        ):
            assert actual[key] == expected[key], f"{symbol} {key}: {actual[key]} {expected[key]}"
        assert actual["cross_volume"] >= expected["closing_cross_shares"]


if __name__ == "__main__":
    main()
