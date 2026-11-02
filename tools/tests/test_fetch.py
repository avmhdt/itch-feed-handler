"""Tests for the resumable, rate-limited download path."""

from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any

import httpx
import pytest
from typer.testing import CliRunner

from itchtools import cli, fetch
from itchtools.fetch import FetchError, RateLimiter

BODY = b"itch" * 1000
MD5 = hashlib.md5(BODY).hexdigest()
DAY = "01302019"
GZ = f"{DAY}.NASDAQ_ITCH50.gz"


def server(*, md5_body: str | None, seen: list[httpx.Request] | None = None) -> httpx.MockTransport:
    def handle(request: httpx.Request) -> httpx.Response:
        if seen is not None:
            seen.append(request)
        path = request.url.path
        if path.endswith(".md5sum"):
            if md5_body is None:
                return httpx.Response(404)
            return httpx.Response(200, content=md5_body.encode())
        if request.method == "HEAD":
            return httpx.Response(200, headers={"content-length": str(len(BODY))})
        range_header = request.headers.get("range")
        if range_header:
            start = int(range_header.removeprefix("bytes=").removesuffix("-"))
            return httpx.Response(206, content=BODY[start:])
        return httpx.Response(200, content=BODY)

    return httpx.MockTransport(handle)


def run(tmp_path: Path, transport: httpx.MockTransport, **kwargs: Any) -> dict[str, Any]:
    options: dict[str, Any] = {"want_decompress": False, "keep_gz": True, "max_bytes_per_s": 0}
    options.update(kwargs)
    with httpx.Client(transport=transport) as client:
        return fetch.fetch_days(
            [DAY],
            "https://example.test/ITCH/",
            tmp_path / "raw",
            tmp_path / "interim",
            client=client,
            **options,
        )


def test_download_verifies_published_md5_and_writes_manifest(tmp_path: Path) -> None:
    entries = run(tmp_path, server(md5_body=f"{MD5}  {GZ}\n"))
    entry = entries[DAY]
    assert entry["gz_md5"] == MD5
    assert entry["gz_md5_source"] == "published"
    assert entry["gz_bytes"] == len(BODY)
    manifest = json.loads((tmp_path / "raw" / "manifest.json").read_text())
    assert manifest["files"][DAY]["gz_sha256"] == hashlib.sha256(BODY).hexdigest()


def test_missing_md5sum_is_recorded_not_fatal(tmp_path: Path) -> None:
    entry = run(tmp_path, server(md5_body=None))[DAY]
    assert entry["gz_md5_source"] == "computed-unverified"
    assert entry["gz_md5"] == MD5


def test_md5_mismatch_is_an_error(tmp_path: Path) -> None:
    with pytest.raises(FetchError, match="does not match"):
        run(tmp_path, server(md5_body="0" * 32))


def test_unreadable_md5sum_is_an_error(tmp_path: Path) -> None:
    with pytest.raises(FetchError, match="unreadable"):
        run(tmp_path, server(md5_body="not a digest"))


def test_resume_uses_a_range_request(tmp_path: Path) -> None:
    raw = tmp_path / "raw"
    raw.mkdir()
    (raw / f"{GZ}.part").write_bytes(BODY[:100])
    seen: list[httpx.Request] = []
    entry = run(tmp_path, server(md5_body=MD5, seen=seen))[DAY]
    assert entry["gz_md5"] == MD5
    assert any(r.headers.get("range") == "bytes=100-" for r in seen)


def test_oversized_partial_restarts_from_zero(tmp_path: Path) -> None:
    raw = tmp_path / "raw"
    raw.mkdir()
    (raw / f"{GZ}.part").write_bytes(BODY + b"extra")
    assert run(tmp_path, server(md5_body=MD5))[DAY]["gz_md5"] == MD5


def test_server_ignoring_range_is_an_error(tmp_path: Path) -> None:
    def handle(request: httpx.Request) -> httpx.Response:
        if request.method == "HEAD":
            return httpx.Response(200, headers={"content-length": str(len(BODY))})
        return httpx.Response(200, content=BODY)

    raw = tmp_path / "raw"
    raw.mkdir()
    (raw / f"{GZ}.md5sum").write_text(MD5)
    (raw / f"{GZ}.part").write_bytes(BODY[:10])
    with pytest.raises(FetchError, match="ignored the range"):
        run(tmp_path, httpx.MockTransport(handle))


def test_short_download_is_an_error(tmp_path: Path) -> None:
    def handle(request: httpx.Request) -> httpx.Response:
        if request.url.path.endswith(".md5sum"):
            return httpx.Response(404)
        if request.method == "HEAD":
            return httpx.Response(200, headers={"content-length": str(len(BODY) + 1)})
        return httpx.Response(200, content=BODY)

    with pytest.raises(FetchError, match="expected"):
        run(tmp_path, httpx.MockTransport(handle))


def test_transport_errors_are_retried_from_the_bytes_on_disk(tmp_path: Path) -> None:
    calls = {"gets": 0}

    def handle(request: httpx.Request) -> httpx.Response:
        if request.method == "HEAD":
            return httpx.Response(200, headers={"content-length": str(len(BODY))})
        calls["gets"] += 1
        if calls["gets"] == 1:
            raise httpx.ReadTimeout("stalled", request=request)
        return httpx.Response(200, content=BODY)

    dest = tmp_path / GZ
    with httpx.Client(transport=httpx.MockTransport(handle)) as client:
        fetch.download(client, "https://example.test/x", dest, RateLimiter(0), sleep=lambda _: None)
    assert dest.read_bytes() == BODY
    assert calls["gets"] == 2


def test_transport_errors_give_up_after_the_last_attempt(tmp_path: Path) -> None:
    def handle(request: httpx.Request) -> httpx.Response:
        raise httpx.ConnectError("refused", request=request)

    with (
        httpx.Client(transport=httpx.MockTransport(handle)) as client,
        pytest.raises(httpx.ConnectError),
    ):
        fetch.download(
            client,
            "https://example.test/x",
            tmp_path / GZ,
            RateLimiter(0),
            attempts=2,
            sleep=lambda _: None,
        )


def test_second_run_downloads_nothing(tmp_path: Path) -> None:
    run(tmp_path, server(md5_body=MD5))
    seen: list[httpx.Request] = []
    run(tmp_path, server(md5_body=MD5, seen=seen))
    assert seen == []


def test_decompress_records_raw_hash_and_can_drop_gz(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def fake_decompress(gz_path: Path, out_path: Path) -> None:
        out_path.write_bytes(b"raw bytes")

    monkeypatch.setattr(fetch, "decompress", fake_decompress)
    entry = run(tmp_path, server(md5_body=MD5), want_decompress=True, keep_gz=False)[DAY]
    assert entry["raw_bytes"] == len(b"raw bytes")
    assert entry["raw_sha256"] == hashlib.sha256(b"raw bytes").hexdigest()
    assert entry["gz_crc_verified"] is True
    assert not (tmp_path / "raw" / GZ).exists()


def test_decompress_runs_pigz_at_low_priority(tmp_path: Path) -> None:
    calls: list[list[str]] = []

    class Runner:
        def run(self, argv: list[str], stdout: Any, check: bool) -> None:
            assert check
            calls.append(argv)
            stdout.write(b"plain")

    fetch.decompress(tmp_path / "x.gz", tmp_path / "x", runner=Runner())
    assert calls[0][:5] == ["nice", "-n", "10", "ionice", "-c"]
    assert (tmp_path / "x").read_bytes() == b"plain"


def test_decompress_failure_leaves_no_output(tmp_path: Path) -> None:
    gz = tmp_path / "bad.gz"
    gz.write_bytes(b"not gzip")
    with pytest.raises(subprocess.CalledProcessError):
        fetch.decompress(gz, tmp_path / "bad")
    assert not (tmp_path / "bad").exists()


def test_rate_limiter_paces_to_the_cap() -> None:
    now = [0.0]
    slept: list[float] = []

    def sleep(seconds: float) -> None:
        slept.append(seconds)
        now[0] += seconds

    limiter = RateLimiter(1000, clock=lambda: now[0], sleep=sleep)
    limiter.account(500)
    limiter.account(500)
    assert sum(slept) == pytest.approx(1.0)


def test_rate_limiter_disabled_at_zero() -> None:
    limiter = RateLimiter(0, sleep=lambda _: pytest.fail("should not sleep"))
    limiter.account(10**9)


def test_cli_fetch_uses_settings(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    captured: dict[str, Any] = {}

    def fake(days: list[str], *_: Any, **kwargs: Any) -> dict[str, dict[str, Any]]:
        captured.update(days=days, **kwargs)
        return {days[0]: {"gz_bytes": 1, "raw_bytes": 2}}

    monkeypatch.setenv("ITCH_DATA_DIR", str(tmp_path))
    monkeypatch.setattr(fetch, "fetch_days", fake)
    result = CliRunner().invoke(cli.app, ["fetch", "--days", "01302019", "--max-bytes-per-s", "5"])
    assert result.exit_code == 0, result.output
    assert captured["days"] == ["01302019"]
    assert captured["max_bytes_per_s"] == 5
    assert "gz 1 bytes" in result.output
