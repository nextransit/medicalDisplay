from __future__ import annotations

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


class TelemetryCollector:
    def __init__(self, db_path: str | Path, event_publisher: EventPublisher | None = None) -> None:
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.event_publisher = event_publisher
        self._init_db()

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.db_path)
        connection.row_factory = sqlite3.Row
        return connection

    def _init_db(self) -> None:
        with self._connect() as connection:
            connection.executescript(
                """
                CREATE TABLE IF NOT EXISTS telemetry (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    device_id TEXT NOT NULL,
                    timestamp TEXT NOT NULL,
                    hours_used REAL NOT NULL DEFAULT 0,
                    ai_inference_latency_ms REAL NOT NULL DEFAULT 0,
                    avg_fps REAL NOT NULL DEFAULT 0,
                    cpu_usage_percent REAL NOT NULL DEFAULT 0,
                    memory_usage_mb REAL NOT NULL DEFAULT 0,
                    gpu_temperature_c REAL NOT NULL DEFAULT 0,
                    network_tx_bytes INTEGER NOT NULL DEFAULT 0,
                    network_rx_bytes INTEGER NOT NULL DEFAULT 0,
                    error_count INTEGER NOT NULL DEFAULT 0,
                    raw_json TEXT,
                    created_at TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS inference_stats (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    device_id TEXT NOT NULL,
                    timestamp TEXT NOT NULL,
                    total_inferences INTEGER NOT NULL DEFAULT 0,
                    successful_inferences INTEGER NOT NULL DEFAULT 0,
                    avg_latency_ms REAL NOT NULL DEFAULT 0,
                    p50_latency_ms REAL NOT NULL DEFAULT 0,
                    p95_latency_ms REAL NOT NULL DEFAULT 0,
                    p99_latency_ms REAL NOT NULL DEFAULT 0,
                    modality_counts_json TEXT,
                    raw_json TEXT,
                    created_at TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS qc_reports (
                    report_id TEXT PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    timestamp TEXT NOT NULL,
                    delta_e REAL NOT NULL DEFAULT 0,
                    luminance REAL NOT NULL DEFAULT 0,
                    backlight_hours REAL NOT NULL DEFAULT 0,
                    temperature REAL NOT NULL DEFAULT 0,
                    health_score REAL NOT NULL DEFAULT 0,
                    recommendations TEXT,
                    raw_json TEXT,
                    created_at TEXT NOT NULL
                );
                """
            )

    def _publish(self, event_name: str, device_id: str, payload: dict[str, Any]) -> None:
        if not self.event_publisher:
            return
        try:
            self.event_publisher(event_name, device_id, payload)
        except Exception:
            return

    def _row_to_telemetry(self, row: sqlite3.Row) -> dict[str, Any]:
        raw = from_json(row["raw_json"], {})
        return {
            "device_id": row["device_id"],
            "timestamp": row["timestamp"],
            "hours_used": row["hours_used"],
            "ai_inference_latency_ms": row["ai_inference_latency_ms"],
            "avg_fps": row["avg_fps"],
            "cpu_usage_percent": row["cpu_usage_percent"],
            "memory_usage_mb": row["memory_usage_mb"],
            "gpu_temperature_c": row["gpu_temperature_c"],
            "network_tx_bytes": row["network_tx_bytes"],
            "network_rx_bytes": row["network_rx_bytes"],
            "error_count": row["error_count"],
            "metadata": raw.get("metadata", {}),
            "created_at": row["created_at"],
        }

    def _row_to_inference_stats(self, row: sqlite3.Row) -> dict[str, Any]:
        return {
            "device_id": row["device_id"],
            "timestamp": row["timestamp"],
            "total_inferences": row["total_inferences"],
            "successful_inferences": row["successful_inferences"],
            "avg_latency_ms": row["avg_latency_ms"],
            "p50_latency_ms": row["p50_latency_ms"],
            "p95_latency_ms": row["p95_latency_ms"],
            "p99_latency_ms": row["p99_latency_ms"],
            "modality_counts": from_json(row["modality_counts_json"], []),
            "created_at": row["created_at"],
        }

    def _row_to_qc_report(self, row: sqlite3.Row) -> dict[str, Any]:
        return {
            "report_id": row["report_id"],
            "device_id": row["device_id"],
            "timestamp": row["timestamp"],
            "delta_e": row["delta_e"],
            "luminance": row["luminance"],
            "backlight_hours": row["backlight_hours"],
            "temperature": row["temperature"],
            "health_score": row["health_score"],
            "recommendations": row["recommendations"],
            "created_at": row["created_at"],
        }

    def ingest(self, device_id: str, payload: dict[str, Any]) -> dict[str, Any]:
        now = utc_now()
        timestamp = payload.get("timestamp") or now
        with self._connect() as connection:
            connection.execute(
                """
                INSERT INTO telemetry (
                    device_id, timestamp, hours_used, ai_inference_latency_ms,
                    avg_fps, cpu_usage_percent, memory_usage_mb, gpu_temperature_c,
                    network_tx_bytes, network_rx_bytes, error_count, raw_json, created_at
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    device_id,
                    timestamp,
                    payload.get("hours_used", 0.0),
                    payload.get("ai_inference_latency_ms", 0.0),
                    payload.get("avg_fps", 0.0),
                    payload.get("cpu_usage_percent", 0.0),
                    payload.get("memory_usage_mb", 0.0),
                    payload.get("gpu_temperature_c", 0.0),
                    payload.get("network_tx_bytes", 0),
                    payload.get("network_rx_bytes", 0),
                    payload.get("error_count", 0),
                    to_json(payload),
                    now,
                ),
            )
        saved = {
            "device_id": device_id,
            "timestamp": timestamp,
            **payload,
            "created_at": now,
        }
        self._publish("telemetry.ingested", device_id, saved)
        return saved

    def ingest_batch(self, device_id: str, samples: list[dict[str, Any]]) -> dict[str, Any]:
        inserted = []
        with self._connect() as connection:
            for sample in samples:
                now = utc_now()
                timestamp = sample.get("timestamp") or now
                connection.execute(
                    """
                    INSERT INTO telemetry (
                        device_id, timestamp, hours_used, ai_inference_latency_ms,
                        avg_fps, cpu_usage_percent, memory_usage_mb, gpu_temperature_c,
                        network_tx_bytes, network_rx_bytes, error_count, raw_json, created_at
                    )
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        device_id,
                        timestamp,
                        sample.get("hours_used", 0.0),
                        sample.get("ai_inference_latency_ms", 0.0),
                        sample.get("avg_fps", 0.0),
                        sample.get("cpu_usage_percent", 0.0),
                        sample.get("memory_usage_mb", 0.0),
                        sample.get("gpu_temperature_c", 0.0),
                        sample.get("network_tx_bytes", 0),
                        sample.get("network_rx_bytes", 0),
                        sample.get("error_count", 0),
                        to_json(sample),
                        now,
                    ),
                )
                inserted.append({"device_id": device_id, "timestamp": timestamp, **sample, "created_at": now})
        batch_result = {"device_id": device_id, "count": len(inserted), "samples": inserted}
        self._publish("telemetry.batch_ingested", device_id, batch_result)
        return batch_result

    def latest_for_device(self, device_id: str, limit: int = 20) -> list[dict[str, Any]]:
        with self._connect() as connection:
            rows = connection.execute(
                """
                SELECT * FROM telemetry
                WHERE device_id = ?
                ORDER BY timestamp DESC, id DESC
                LIMIT ?
                """,
                (device_id, limit),
            ).fetchall()
        return [self._row_to_telemetry(row) for row in rows]

    def summary_for_device(self, device_id: str, sample_window: int = 50) -> dict[str, Any]:
        latest = self.latest_for_device(device_id, limit=1)
        with self._connect() as connection:
            aggregate = connection.execute(
                """
                SELECT
                    COUNT(*) AS sample_count,
                    AVG(cpu_usage_percent) AS avg_cpu_usage_percent,
                    AVG(memory_usage_mb) AS avg_memory_usage_mb,
                    AVG(gpu_temperature_c) AS avg_gpu_temperature_c,
                    MAX(gpu_temperature_c) AS max_gpu_temperature_c,
                    MAX(error_count) AS max_error_count
                FROM (
                    SELECT * FROM telemetry
                    WHERE device_id = ?
                    ORDER BY id DESC
                    LIMIT ?
                )
                """,
                (device_id, sample_window),
            ).fetchone()
        return {
            "device_id": device_id,
            "sample_window": sample_window,
            "samples": int(aggregate["sample_count"] or 0),
            "latest": latest[0] if latest else None,
            "averages": {
                "cpu_usage_percent": aggregate["avg_cpu_usage_percent"] or 0.0,
                "memory_usage_mb": aggregate["avg_memory_usage_mb"] or 0.0,
                "gpu_temperature_c": aggregate["avg_gpu_temperature_c"] or 0.0,
            },
            "maxima": {
                "gpu_temperature_c": aggregate["max_gpu_temperature_c"] or 0.0,
                "error_count": int(aggregate["max_error_count"] or 0),
            },
        }

    def report_inference_stats(self, device_id: str, payload: dict[str, Any]) -> dict[str, Any]:
        now = utc_now()
        timestamp = payload.get("timestamp") or now
        with self._connect() as connection:
            connection.execute(
                """
                INSERT INTO inference_stats (
                    device_id, timestamp, total_inferences, successful_inferences,
                    avg_latency_ms, p50_latency_ms, p95_latency_ms, p99_latency_ms,
                    modality_counts_json, raw_json, created_at
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    device_id,
                    timestamp,
                    payload.get("total_inferences", 0),
                    payload.get("successful_inferences", 0),
                    payload.get("avg_latency_ms", 0.0),
                    payload.get("p50_latency_ms", 0.0),
                    payload.get("p95_latency_ms", 0.0),
                    payload.get("p99_latency_ms", 0.0),
                    to_json(payload.get("modality_counts", [])),
                    to_json(payload),
                    now,
                ),
            )
        saved = {
            "device_id": device_id,
            "timestamp": timestamp,
            **payload,
            "created_at": now,
        }
        self._publish("inference.reported", device_id, saved)
        return saved

    def latest_inference_stats(self, device_id: str) -> dict[str, Any] | None:
        with self._connect() as connection:
            row = connection.execute(
                """
                SELECT * FROM inference_stats
                WHERE device_id = ?
                ORDER BY timestamp DESC, id DESC
                LIMIT 1
                """,
                (device_id,),
            ).fetchone()
        return self._row_to_inference_stats(row) if row else None

    def report_qc(self, payload: dict[str, Any]) -> dict[str, Any]:
        report_id = payload.get("report_id") or str(uuid.uuid4())
        now = utc_now()
        timestamp = payload.get("timestamp") or now
        with self._connect() as connection:
            connection.execute(
                """
                INSERT INTO qc_reports (
                    report_id, device_id, timestamp, delta_e, luminance,
                    backlight_hours, temperature, health_score, recommendations,
                    raw_json, created_at
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    report_id,
                    payload["device_id"],
                    timestamp,
                    payload.get("delta_e", 0.0),
                    payload.get("luminance", 0.0),
                    payload.get("backlight_hours", 0.0),
                    payload.get("temperature", 0.0),
                    payload.get("health_score", 0.0),
                    payload.get("recommendations", ""),
                    to_json(payload),
                    now,
                ),
            )
        saved = {"report_id": report_id, "timestamp": timestamp, **payload, "created_at": now}
        self._publish("qc.reported", payload["device_id"], saved)
        return saved

    def latest_qc_report(self, device_id: str) -> dict[str, Any] | None:
        with self._connect() as connection:
            row = connection.execute(
                """
                SELECT * FROM qc_reports
                WHERE device_id = ?
                ORDER BY timestamp DESC, created_at DESC
                LIMIT 1
                """,
                (device_id,),
            ).fetchone()
        return self._row_to_qc_report(row) if row else None
