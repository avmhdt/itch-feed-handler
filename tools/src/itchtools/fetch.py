"""Resumable sample-file download with md5 checks and a manifest (FR-28, DQ-14).

Files are taken one at a time and the transfer rate is capped, because other jobs on this
machine share the connection.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import time
from collections.abc import Callable, Iterable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import httpx

CHUNK_BYTES = 1 << 20
DOWNLOAD_ATTEMPTS = 50
RETRY_BASE_SECONDS = 5.0
RETRY_MAX_SECONDS = 60.0
FILE_SUFFIX = ".NASDAQ_ITCH50"
MANIFEST_NAME = "manifest.json"


class FetchError(RuntimeError):
    """A download or checksum step failed."""


@dataclass
class RateLimiter:
    """Sleeps just enough to hold the average rate at or under `bytes_per_s`."""

    bytes_per_s: int
    clock: Callable[[], float] = time.monotonic
    sleep: Callable[[float], None] = time.sleep
    _start: float | None = None
    _sent: int = 0

    def account(self, n: int) -> None:
        if self.bytes_per_s <= 0:
            return
        if self._start is None:
            self._start = self.clock()
        self._sent += n
        due = self._start + self._sent / self.bytes_per_s
        delay = due - self.clock()
        if delay > 0:
            self.sleep(delay)


def file_digest(path: Path, algorithm: str) -> str:
    hasher = hashlib.new(algorithm)
    with path.open("rb") as handle:
        while chunk := handle.read(CHUNK_BYTES):
            hasher.update(chunk)
    return hasher.hexdigest()


def parse_md5sum(text: str) -> str:
    """Return the digest from an md5sum line, `<hex>  <name>`; the first token is the digest."""
    tokens = text.split()
    if not tokens or len(tokens[0]) != 32:
        raise FetchError(f"unreadable md5sum content: {text!r}")
    return tokens[0].lower()


def published_md5(client: httpx.Client, url: str, md5_path: Path) -> str | None:
    """Return the published digest, or None when the server does not serve one.

    The sample site lists `.md5sum` companions but answers 404 for them, so a missing file is an
    expected state (DQ-14: "where available"); any other failure is an error.
    """
    if not md5_path.exists():
        response = client.get(url)
        if response.status_code == httpx.codes.NOT_FOUND:
            return None
        response.raise_for_status()
        md5_path.write_bytes(response.content)
    return parse_md5sum(md5_path.read_text(encoding="utf-8"))


def load_manifest(raw_dir: Path) -> dict[str, Any]:
    path = raw_dir / MANIFEST_NAME
    if not path.exists():
        return {"files": {}}
    loaded: dict[str, Any] = json.loads(path.read_text(encoding="utf-8"))
    return loaded


def save_manifest(raw_dir: Path, manifest: dict[str, Any]) -> None:
    path = raw_dir / MANIFEST_NAME
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    tmp.replace(path)


def download(
    client: httpx.Client,
    url: str,
    dest: Path,
    limiter: RateLimiter,
    *,
    attempts: int = DOWNLOAD_ATTEMPTS,
    sleep: Callable[[float], None] = time.sleep,
) -> None:
    """Stream `url` to `dest`, resuming after a dropped connection.

    The sample server stalls now and then on multi-hour transfers, so a transport error is retried
    from the bytes already on disk.
    """
    for attempt in range(1, attempts + 1):
        try:
            download_once(client, url, dest, limiter)
            return
        except httpx.TransportError:
            if attempt == attempts:
                raise
            sleep(min(RETRY_BASE_SECONDS * attempt, RETRY_MAX_SECONDS))


def download_once(client: httpx.Client, url: str, dest: Path, limiter: RateLimiter) -> None:
    """One attempt at `url`, continuing any `.part` file with a range request."""
    part = dest.with_name(dest.name + ".part")
    head = client.head(url)
    head.raise_for_status()
    total = int(head.headers["content-length"])
    have = part.stat().st_size if part.exists() else 0
    if have > total:
        part.unlink()
        have = 0
    if have < total:
        headers = {"Range": f"bytes={have}-"} if have else {}
        with client.stream("GET", url, headers=headers) as response:
            response.raise_for_status()
            if have and response.status_code != httpx.codes.PARTIAL_CONTENT:
                raise FetchError(f"server ignored the range request for {url}")
            with part.open("ab") as handle:
                for chunk in response.iter_bytes(CHUNK_BYTES):
                    handle.write(chunk)
                    limiter.account(len(chunk))
    if part.stat().st_size != total:
        raise FetchError(f"{part.name}: expected {total} bytes, got {part.stat().st_size}")
    part.replace(dest)


def decompress(gz_path: Path, out_path: Path, runner: Any = subprocess) -> None:
    """Decompress with pigz at low priority, leaving no partial output on failure."""
    tmp = out_path.with_name(out_path.name + ".part")
    tool = shutil.which("pigz") or "gzip"
    with tmp.open("wb") as handle:
        runner.run(
            ["nice", "-n", "10", "ionice", "-c", "3", tool, "-dc", str(gz_path)],
            stdout=handle,
            check=True,
        )
    tmp.replace(out_path)


def fetch_day(
    client: httpx.Client,
    day: str,
    base_url: str,
    raw_dir: Path,
    interim_dir: Path,
    limiter: RateLimiter,
    *,
    want_decompress: bool,
    keep_gz: bool,
    log: Callable[[str], None] = print,
) -> dict[str, Any]:
    """Fetch and verify one MMDDYYYY day, returning its manifest entry. Safe to re-run."""
    base = day + FILE_SUFFIX
    gz_name = base + ".gz"
    gz_path = raw_dir / gz_name
    md5_path = raw_dir / (gz_name + ".md5sum")
    raw_path = interim_dir / base
    url = base_url.rstrip("/") + "/" + gz_name

    manifest = load_manifest(raw_dir)
    entry: dict[str, Any] = dict(manifest["files"].get(day, {}))

    expected_md5 = published_md5(client, url + ".md5sum", md5_path)

    if not gz_path.exists():
        log(f"{gz_name}: downloading")
        download(client, url, gz_path, limiter)
    if "gz_sha256" not in entry:
        log(f"{gz_name}: hashing")
        actual = file_digest(gz_path, "md5")
        if expected_md5 is not None and actual != expected_md5:
            raise FetchError(f"{gz_name}: md5 {actual} does not match {expected_md5}")
        if expected_md5 is None:
            log(f"{gz_name}: no published md5; relying on length and the gzip CRC")
        entry.update(
            gz_md5=actual,
            gz_md5_source="published" if expected_md5 else "computed-unverified",
            gz_sha256=file_digest(gz_path, "sha256"),
            gz_bytes=gz_path.stat().st_size,
        )
        manifest["files"][day] = entry
        save_manifest(raw_dir, manifest)

    if want_decompress and not raw_path.exists():
        log(f"{base}: decompressing")
        decompress(gz_path, raw_path)
        entry["gz_crc_verified"] = True
    if raw_path.exists() and "raw_sha256" not in entry:
        log(f"{base}: hashing decompressed file")
        entry.update(raw_sha256=file_digest(raw_path, "sha256"), raw_bytes=raw_path.stat().st_size)
        manifest["files"][day] = entry
        save_manifest(raw_dir, manifest)

    if raw_path.exists() and not keep_gz:
        gz_path.unlink()
    return entry


def fetch_days(
    days: Iterable[str],
    base_url: str,
    raw_dir: Path,
    interim_dir: Path,
    *,
    max_bytes_per_s: int,
    want_decompress: bool,
    keep_gz: bool,
    client: httpx.Client | None = None,
) -> dict[str, dict[str, Any]]:
    """Fetch each day in turn, never concurrently."""
    raw_dir.mkdir(parents=True, exist_ok=True)
    interim_dir.mkdir(parents=True, exist_ok=True)
    owned = client is None
    active = client or httpx.Client(follow_redirects=True, timeout=httpx.Timeout(60.0))
    limiter = RateLimiter(max_bytes_per_s)
    try:
        return {
            day: fetch_day(
                active,
                day,
                base_url,
                raw_dir,
                interim_dir,
                limiter,
                want_decompress=want_decompress,
                keep_gz=keep_gz,
            )
            for day in days
        }
    finally:
        if owned:
            active.close()
