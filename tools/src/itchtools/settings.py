"""Environment-backed settings shared by every itchtools command."""

from __future__ import annotations

from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_DATA_ROOT = Path.home() / "data" / "itch-feed-handler"


class Settings(BaseSettings):
    """Paths and options read from the environment or the repo's `.env`."""

    model_config = SettingsConfigDict(
        env_file=REPO_ROOT / ".env", env_file_encoding="utf-8", extra="ignore"
    )

    itch_data_dir: Path = DEFAULT_DATA_ROOT
    itch_base_url: str = "https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/"
    itch_days: str = "01302019,03272019,01302020"
    itch_keep_gz: bool = True
    itch_max_bytes_per_s: int = 10_000_000
    itch_ref_parser_bin: Path = (
        Path.home() / "build" / "itch-feed-handler" / "cargo" / "release" / "ref_counts"
    )

    @property
    def raw_dir(self) -> Path:
        return self.itch_data_dir.expanduser() / "raw" / "itch"

    @property
    def interim_dir(self) -> Path:
        return self.itch_data_dir.expanduser() / "interim" / "itch"
