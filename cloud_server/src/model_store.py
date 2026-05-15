from __future__ import annotations

import base64
import json
import logging
import re
import sqlite3
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable

LOGGER = logging.getLogger(__name__)
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


class ModelStore:
    def __init__(
        self,
        db_path: str | Path,
        models_dir: str | Path,
        uploads_dir: str | Path,
        public_download_prefix: str = "/api/v1/models",
        event_publisher: EventPublisher | None = None,
    ) -> None:
        self.db_path = Path(db_path)
        self.models_dir = Path(models_dir)
        self.uploads_dir = Path(uploads_dir)
        self.public_download_prefix = public_download_prefix.rstrip("/")
        self.event_publisher = event_publisher
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.models_dir.mkdir(parents=True, exist_ok=True)
        self.uploads_dir.mkdir(parents=True, exist_ok=True)
        self._init_db()
        self.rescan_models()

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.db_path)
        connection.row_factory = sqlite3.Row
        return connection

    def _init_db(self) -> None:
        with self._connect() as connection:
            connection.executescript(
                """
                CREATE TABLE IF NOT EXISTS model_manifests (
                    model_id TEXT NOT NULL,
                    version TEXT NOT NULL,
                    modality TEXT,
                    base_version TEXT,
                    checksum TEXT,
                    file_name TEXT NOT NULL,
                    file_size INTEGER NOT NULL DEFAULT 0,
                    min_firmware_version TEXT,
                    release_notes TEXT,
                    tags_json TEXT,
                    target_devices_json TEXT,
                    is_mandatory INTEGER NOT NULL DEFAULT 0,
                    published_at TEXT NOT NULL,
                    manifest_name TEXT NOT NULL,
                    raw_json TEXT,
                    PRIMARY KEY (model_id, version)
                );

                CREATE TABLE IF NOT EXISTS gradient_uploads (
                    gradient_id TEXT PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    model_id TEXT NOT NULL,
                    file_path TEXT NOT NULL,
                    size_bytes INTEGER NOT NULL,
                    metadata_json TEXT,
                    uploaded_at TEXT NOT NULL
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

    def _row_to_manifest(self, row: sqlite3.Row) -> dict[str, Any]:
        return {
            "model_id": row["model_id"],
            "version": row["version"],
            "modality": row["modality"],
            "base_version": row["base_version"],
            "checksum": row["checksum"],
            "file_name": row["file_name"],
            "file_size": row["file_size"],
            "min_firmware_version": row["min_firmware_version"],
            "release_notes": row["release_notes"],
            "tags": from_json(row["tags_json"], []),
            "target_device_models": from_json(row["target_devices_json"], []),
            "is_mandatory": bool(row["is_mandatory"]),
            "published_at": row["published_at"],
            "manifest_name": row["manifest_name"],
            "download_url": f"{self.public_download_prefix}/{row['model_id']}/{row['version']}/download",
        }

    def rescan_models(self) -> list[dict[str, Any]]:
        with self._connect() as connection:
            for manifest_path in sorted(self.models_dir.glob("*.manifest.json")):
                try:
                    payload = json.loads(manifest_path.read_text(encoding="utf-8"))
                except Exception as exc:
                    LOGGER.warning("跳过无效模型清单 %s: %s", manifest_path.name, exc)
                    continue
                file_name = payload["file_name"]
                artifact_path = self.models_dir / file_name
                file_size = artifact_path.stat().st_size if artifact_path.exists() else 0
                connection.execute(
                    """
                    INSERT INTO model_manifests (
                        model_id, version, modality, base_version, checksum,
                        file_name, file_size, min_firmware_version, release_notes,
                        tags_json, target_devices_json, is_mandatory, published_at,
                        manifest_name, raw_json
                    )
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    ON CONFLICT(model_id, version) DO UPDATE SET
                        modality = excluded.modality,
                        base_version = excluded.base_version,
                        checksum = excluded.checksum,
                        file_name = excluded.file_name,
                        file_size = excluded.file_size,
                        min_firmware_version = excluded.min_firmware_version,
                        release_notes = excluded.release_notes,
                        tags_json = excluded.tags_json,
                        target_devices_json = excluded.target_devices_json,
                        is_mandatory = excluded.is_mandatory,
                        published_at = excluded.published_at,
                        manifest_name = excluded.manifest_name,
                        raw_json = excluded.raw_json
                    """,
                    (
                        payload["model_id"],
                        payload["version"],
                        payload.get("modality", ""),
                        payload.get("base_version", ""),
                        payload.get("checksum", ""),
                        file_name,
                        file_size,
                        payload.get("min_firmware_version", ""),
                        payload.get("release_notes", ""),
                        to_json(payload.get("tags", [])),
                        to_json(payload.get("target_device_models", [])),
                        int(payload.get("is_mandatory", False)),
                        payload.get("published_at", utc_now()),
                        manifest_path.name,
                        to_json(payload),
                    ),
                )
        models = self.list_models()
        self._publish("model.rescanned", "catalog", {"count": len(models)})
        return models

    def list_models(self, modality: str | None = None) -> list[dict[str, Any]]:
        query = "SELECT * FROM model_manifests"
        parameters: tuple[Any, ...] = ()
        if modality:
            query += " WHERE modality = ?"
            parameters = (modality,)
        query += " ORDER BY published_at DESC"
        with self._connect() as connection:
            rows = connection.execute(query, parameters).fetchall()
        return [self._row_to_manifest(row) for row in rows]

    def check_update(
        self,
        current_version: str,
        model_id: str | None = None,
        modality: str | None = None,
        firmware_version: str | None = None,
        device_model: str | None = None,
    ) -> dict[str, Any] | None:
        candidates = self.list_models(modality=modality)
        if model_id:
            candidates = [item for item in candidates if item["model_id"] == model_id]
        compatible: list[dict[str, Any]] = []
        for candidate in candidates:
            if version_key(candidate["version"]) <= version_key(current_version):
                continue
            min_fw = candidate.get("min_firmware_version") or ""
            if firmware_version and min_fw and version_key(firmware_version) < version_key(min_fw):
                continue
            targets = candidate.get("target_device_models", [])
            if device_model and targets and device_model not in targets:
                continue
            compatible.append(candidate)
        compatible.sort(key=lambda item: version_key(item["version"]), reverse=True)
        return compatible[0] if compatible else None

    def get_download_path(self, model_id: str, version: str) -> Path:
        with self._connect() as connection:
            row = connection.execute(
                """
                SELECT * FROM model_manifests
                WHERE model_id = ? AND version = ?
                """,
                (model_id, version),
            ).fetchone()
        if row is None:
            raise KeyError(f"model not found: {model_id}:{version}")
        artifact_path = self.models_dir / row["file_name"]
        if not artifact_path.exists():
            raise FileNotFoundError(artifact_path)
        return artifact_path

    def latest_federated_model(self, model_id: str) -> dict[str, Any] | None:
        candidates = [item for item in self.list_models() if item["model_id"] == model_id]
        candidates.sort(key=lambda item: version_key(item["version"]), reverse=True)
        return candidates[0] if candidates else None

    def record_gradient_upload(
        self,
        device_id: str,
        model_id: str,
        payload_base64: str,
        metadata: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        binary = base64.b64decode(payload_base64.encode("utf-8"), validate=True)
        gradient_id = str(uuid.uuid4())
        target_dir = self.uploads_dir / "gradients"
        target_dir.mkdir(parents=True, exist_ok=True)
        file_path = target_dir / f"{gradient_id}.bin"
        file_path.write_bytes(binary)
        uploaded_at = utc_now()
        with self._connect() as connection:
            connection.execute(
                """
                INSERT INTO gradient_uploads (
                    gradient_id, device_id, model_id, file_path,
                    size_bytes, metadata_json, uploaded_at
                )
                VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    gradient_id,
                    device_id,
                    model_id,
                    str(file_path),
                    len(binary),
                    to_json(metadata or {}),
                    uploaded_at,
                ),
            )
        saved = {
            "gradient_id": gradient_id,
            "device_id": device_id,
            "model_id": model_id,
            "size_bytes": len(binary),
            "uploaded_at": uploaded_at,
            "metadata": metadata or {},
        }
        self._publish("model.gradient_uploaded", model_id, saved)
        return saved
