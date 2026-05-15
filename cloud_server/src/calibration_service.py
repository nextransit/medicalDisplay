from __future__ import annotations

import base64
import hashlib
import json
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


class CalibrationService:
    def __init__(
        self,
        db_path: str | Path,
        uploads_dir: str | Path,
        event_publisher: EventPublisher | None = None,
    ) -> None:
        self.db_path = Path(db_path)
        self.uploads_dir = Path(uploads_dir)
        self.event_publisher = event_publisher
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.uploads_dir.mkdir(parents=True, exist_ok=True)
        self._init_db()

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.db_path)
        connection.row_factory = sqlite3.Row
        return connection

    def _init_db(self) -> None:
        with self._connect() as connection:
            connection.executescript(
                """
                CREATE TABLE IF NOT EXISTS calibration_reports (
                    calibration_id TEXT PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    timestamp TEXT NOT NULL,
                    delta_e REAL NOT NULL DEFAULT 0,
                    luminance REAL NOT NULL DEFAULT 0,
                    uniformity REAL NOT NULL DEFAULT 0,
                    gamma REAL NOT NULL DEFAULT 0,
                    report_json TEXT,
                    raw_json TEXT,
                    created_at TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS calibration_sync_payloads (
                    sync_id TEXT PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    file_path TEXT NOT NULL,
                    checksum TEXT NOT NULL,
                    size_bytes INTEGER NOT NULL,
                    created_at TEXT NOT NULL
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

    def _row_to_report(self, row: sqlite3.Row) -> dict[str, Any]:
        report_json = row["report_json"]
        try:
            structured_report = json.loads(report_json) if report_json else {}
        except json.JSONDecodeError:
            structured_report = {"raw": report_json}
        return {
            "calibration_id": row["calibration_id"],
            "device_id": row["device_id"],
            "timestamp": row["timestamp"],
            "delta_e": row["delta_e"],
            "luminance": row["luminance"],
            "uniformity": row["uniformity"],
            "gamma": row["gamma"],
            "report_json": structured_report,
            "created_at": row["created_at"],
        }

    def save_report(self, payload: dict[str, Any]) -> dict[str, Any]:
        calibration_id = payload.get("calibration_id") or str(uuid.uuid4())
        timestamp = payload.get("timestamp") or utc_now()
        report_json = payload.get("report_json", {})
        if isinstance(report_json, str):
            serialized_report = report_json
        else:
            serialized_report = to_json(report_json)
        created_at = utc_now()
        with self._connect() as connection:
            connection.execute(
                """
                INSERT INTO calibration_reports (
                    calibration_id, device_id, timestamp, delta_e, luminance,
                    uniformity, gamma, report_json, raw_json, created_at
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    calibration_id,
                    payload["device_id"],
                    timestamp,
                    payload.get("delta_e", 0.0),
                    payload.get("luminance", 0.0),
                    payload.get("uniformity", 0.0),
                    payload.get("gamma", 0.0),
                    serialized_report,
                    to_json(payload),
                    created_at,
                ),
            )
        saved = self.get_report(calibration_id)
        self._publish("calibration.reported", payload["device_id"], saved)
        return saved

    def get_report(self, calibration_id: str) -> dict[str, Any]:
        with self._connect() as connection:
            row = connection.execute(
                """
                SELECT * FROM calibration_reports
                WHERE calibration_id = ?
                """,
                (calibration_id,),
            ).fetchone()
        if row is None:
            raise KeyError(f"calibration report not found: {calibration_id}")
        return self._row_to_report(row)

    def latest_report(self, device_id: str) -> dict[str, Any] | None:
        with self._connect() as connection:
            row = connection.execute(
                """
                SELECT * FROM calibration_reports
                WHERE device_id = ?
                ORDER BY timestamp DESC, created_at DESC
                LIMIT 1
                """,
                (device_id,),
            ).fetchone()
        return self._row_to_report(row) if row else None

    def save_sync_payload(self, device_id: str, payload_base64: str) -> dict[str, Any]:
        binary = base64.b64decode(payload_base64.encode("utf-8"), validate=True)
        sync_id = str(uuid.uuid4())
        checksum = hashlib.sha256(binary).hexdigest()
        created_at = utc_now()
        target_dir = self.uploads_dir / "calibration"
        target_dir.mkdir(parents=True, exist_ok=True)
        file_path = target_dir / f"{sync_id}.bin"
        file_path.write_bytes(binary)
        with self._connect() as connection:
            connection.execute(
                """
                INSERT INTO calibration_sync_payloads (
                    sync_id, device_id, file_path, checksum, size_bytes, created_at
                )
                VALUES (?, ?, ?, ?, ?, ?)
                """,
                (sync_id, device_id, str(file_path), checksum, len(binary), created_at),
            )
        saved = {
            "sync_id": sync_id,
            "device_id": device_id,
            "checksum": checksum,
            "size_bytes": len(binary),
            "created_at": created_at,
        }
        self._publish("calibration.synced", device_id, saved)
        return saved

    def build_guidance(self, device_id: str) -> dict[str, Any]:
        latest = self.latest_report(device_id)
        guidance: list[str] = []
        severity = "ok"
        if latest is None:
            return {
                "device_id": device_id,
                "severity": "warning",
                "guidance": [
                    "尚未收到校准报告，请先上报首份基线数据。",
                    "建议在安装后 24 小时内执行首次 DICOM GSDF 校准。"
                ],
                "generated_at": utc_now(),
            }
        if latest["delta_e"] > 2.0:
            severity = "warning"
            guidance.append("色差 Delta E 偏高，建议重新进行色彩校准。")
        if latest["luminance"] < 350:
            severity = "warning"
            guidance.append("亮度低于建议值，建议检查背光衰减和电源状态。")
        if latest["uniformity"] < 0.9:
            severity = "warning"
            guidance.append("面板均匀性不足，建议执行均匀性检测。")
        if latest["gamma"] < 2.1 or latest["gamma"] > 2.5:
            severity = "warning"
            guidance.append("Gamma 参数超出预期范围，建议重新应用显示配置。")
        if not guidance:
            guidance.append("最近一次校准结果正常，继续按计划周期校准即可。")
        return {
            "device_id": device_id,
            "severity": severity,
            "latest_report": latest,
            "guidance": guidance,
            "generated_at": utc_now(),
        }
