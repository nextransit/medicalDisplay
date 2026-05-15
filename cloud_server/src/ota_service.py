from __future__ import annotations

import json
import re
import sqlite3
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

EventPublisher = Callable[[str, str, dict[str, Any]], None]


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def to_json(value: Any) -> str:
    return json.dumps(value if value is not None else {}, ensure_ascii=False)


def from_json(value: str | None, default: Any) -> Any:
    if not value:
        return default
    try:
        return json.loads(value)
    except json.JSONDecodeError:
        return default


def version_key(version: str | None) -> tuple[int, ...]:
    numbers = [int(item) for item in re.findall(r"\d+", version or "0")]
    return tuple(numbers or [0])


class OTAService:
    def __init__(
        self,
        db_path: str | Path,
        artifacts_dir: str | Path,
        seed_releases: list[dict[str, Any]] | None = None,
        public_download_prefix: str = "/api/v1/ota/releases",
        event_publisher: EventPublisher | None = None,
    ) -> None:
        self.db_path = Path(db_path)
        self.artifacts_dir = Path(artifacts_dir)
        self.public_download_prefix = public_download_prefix.rstrip("/")
        self.event_publisher = event_publisher
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.artifacts_dir.mkdir(parents=True, exist_ok=True)
        self._init_db()
        self.seed_releases(seed_releases or [])

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.db_path)
        connection.row_factory = sqlite3.Row
        return connection

    def _init_db(self) -> None:
        with self._connect() as connection:
            connection.executescript(
                """
                CREATE TABLE IF NOT EXISTS ota_releases (
                    release_id TEXT PRIMARY KEY,
                    version TEXT NOT NULL,
                    base_version TEXT,
                    channel TEXT NOT NULL,
                    file_name TEXT NOT NULL,
                    delta_file_name TEXT,
                    file_size INTEGER NOT NULL DEFAULT 0,
                    delta_size INTEGER NOT NULL DEFAULT 0,
                    checksum TEXT,
                    signature TEXT,
                    install_path TEXT,
                    changelog TEXT,
                    is_mandatory INTEGER NOT NULL DEFAULT 0,
                    is_security_update INTEGER NOT NULL DEFAULT 0,
                    published_at TEXT NOT NULL,
                    raw_json TEXT
                );

                CREATE TABLE IF NOT EXISTS ota_rollouts (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    device_id TEXT NOT NULL,
                    release_id TEXT NOT NULL,
                    status TEXT NOT NULL,
                    details_json TEXT,
                    noted_at TEXT NOT NULL
                );
                """
            )

    def _publish(self, event_name: str, key: str, payload: dict[str, Any]) -> None:
        if not self.event_publisher:
            return
        try:
            self.event_publisher(event_name, key, payload)
        except Exception:
            return

    def _row_to_release(self, row: sqlite3.Row) -> dict[str, Any]:
        return {
            "release_id": row["release_id"],
            "version": row["version"],
            "base_version": row["base_version"],
            "channel": row["channel"],
            "file_name": row["file_name"],
            "delta_file_name": row["delta_file_name"],
            "full_size": row["file_size"],
            "delta_size": row["delta_size"],
            "checksum": row["checksum"],
            "signature": row["signature"],
            "install_path": row["install_path"],
            "changelog": row["changelog"],
            "is_mandatory": bool(row["is_mandatory"]),
            "is_security_update": bool(row["is_security_update"]),
            "published_at": row["published_at"],
            "download_url": f"{self.public_download_prefix}/{row['release_id']}/download",
            "delta_url": (
                f"{self.public_download_prefix}/{row['release_id']}/download?delta=true"
                if row["delta_file_name"]
                else None
            ),
        }

    def seed_releases(self, releases: list[dict[str, Any]]) -> None:
        for payload in releases:
            self.create_release(payload, publish_event=False)

    def list_releases(self, channel: str | None = None) -> list[dict[str, Any]]:
        query = "SELECT * FROM ota_releases"
        parameters: tuple[Any, ...] = ()
        if channel:
            query += " WHERE channel = ?"
            parameters = (channel,)
        query += " ORDER BY published_at DESC"
        with self._connect() as connection:
            rows = connection.execute(query, parameters).fetchall()
        return [self._row_to_release(row) for row in rows]

    def create_release(self, payload: dict[str, Any], publish_event: bool = True) -> dict[str, Any]:
        release_id = payload.get("release_id") or str(uuid.uuid4())
        file_name = payload["file_name"]
        delta_file_name = payload.get("delta_file_name", "")
        artifact_path = self.artifacts_dir / file_name
        delta_path = self.artifacts_dir / delta_file_name if delta_file_name else None
        file_size = artifact_path.stat().st_size if artifact_path.exists() else 0
        delta_size = delta_path.stat().st_size if delta_path and delta_path.exists() else 0
        published_at = payload.get("published_at", utc_now())
        with self._connect() as connection:
            connection.execute(
                """
                INSERT INTO ota_releases (
                    release_id, version, base_version, channel, file_name,
                    delta_file_name, file_size, delta_size, checksum, signature,
                    install_path, changelog, is_mandatory, is_security_update,
                    published_at, raw_json
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(release_id) DO UPDATE SET
                    version = excluded.version,
                    base_version = excluded.base_version,
                    channel = excluded.channel,
                    file_name = excluded.file_name,
                    delta_file_name = excluded.delta_file_name,
                    file_size = excluded.file_size,
                    delta_size = excluded.delta_size,
                    checksum = excluded.checksum,
                    signature = excluded.signature,
                    install_path = excluded.install_path,
                    changelog = excluded.changelog,
                    is_mandatory = excluded.is_mandatory,
                    is_security_update = excluded.is_security_update,
                    published_at = excluded.published_at,
                    raw_json = excluded.raw_json
                """,
                (
                    release_id,
                    payload["version"],
                    payload.get("base_version", ""),
                    payload.get("channel", "stable"),
                    file_name,
                    delta_file_name,
                    file_size,
                    delta_size,
                    payload.get("checksum", ""),
                    payload.get("signature", ""),
                    payload.get("install_path", ""),
                    payload.get("changelog", ""),
                    int(payload.get("is_mandatory", False)),
                    int(payload.get("is_security_update", False)),
                    published_at,
                    to_json(payload),
                ),
            )
        saved = self.get_release(release_id)
        if publish_event:
            self._publish("ota.release_created", release_id, saved)
        return saved

    def get_release(self, release_id: str) -> dict[str, Any]:
        with self._connect() as connection:
            row = connection.execute(
                "SELECT * FROM ota_releases WHERE release_id = ?",
                (release_id,),
            ).fetchone()
        if row is None:
            raise KeyError(f"release not found: {release_id}")
        return self._row_to_release(row)

    def check_update(self, current_version: str, channel: str = "stable", device_id: str | None = None) -> dict[str, Any] | None:
        releases = self.list_releases(channel=channel)
        candidates = [item for item in releases if version_key(item["version"]) > version_key(current_version)]
        candidates.sort(key=lambda item: version_key(item["version"]), reverse=True)
        update = candidates[0] if candidates else None
        if update and device_id:
            self._publish("ota.update_available", device_id, update)
        return update

    def get_download_path(self, release_id: str, delta: bool = False) -> Path:
        release = self.get_release(release_id)
        file_name = release["delta_file_name"] if delta else release["file_name"]
        if not file_name:
            raise FileNotFoundError(f"release artifact missing for {release_id}")
        artifact_path = self.artifacts_dir / file_name
        if not artifact_path.exists():
            raise FileNotFoundError(artifact_path)
        return artifact_path

    def mark_rollout(self, device_id: str, release_id: str, status: str, details: dict[str, Any] | None = None) -> dict[str, Any]:
        noted_at = utc_now()
        with self._connect() as connection:
            connection.execute(
                """
                INSERT INTO ota_rollouts (device_id, release_id, status, details_json, noted_at)
                VALUES (?, ?, ?, ?, ?)
                """,
                (device_id, release_id, status, to_json(details or {}), noted_at),
            )
        saved = {
            "device_id": device_id,
            "release_id": release_id,
            "status": status,
            "details": details or {},
            "noted_at": noted_at,
        }
        self._publish("ota.rollout_status", device_id, saved)
        return saved
