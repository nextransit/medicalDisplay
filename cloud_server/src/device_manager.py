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


class DeviceManager:
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
                CREATE TABLE IF NOT EXISTS devices (
                    device_id TEXT PRIMARY KEY,
                    model TEXT NOT NULL,
                    firmware_version TEXT,
                    hardware_version TEXT,
                    hospital_id TEXT,
                    department TEXT,
                    latitude REAL,
                    longitude REAL,
                    metadata_json TEXT,
                    status TEXT NOT NULL DEFAULT 'offline',
                    registered_at TEXT NOT NULL,
                    last_seen_at TEXT NOT NULL,
                    updated_at TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS device_events (
                    event_id TEXT PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    event_type TEXT NOT NULL,
                    payload_json TEXT,
                    created_at TEXT NOT NULL
                );

                CREATE TABLE IF NOT EXISTS commands (
                    command_id TEXT PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    command_type TEXT NOT NULL,
                    correlation_id TEXT,
                    payload_json TEXT,
                    requires_response INTEGER NOT NULL DEFAULT 1,
                    status TEXT NOT NULL DEFAULT 'pending',
                    created_at TEXT NOT NULL,
                    delivered_at TEXT,
                    responded_at TEXT
                );

                CREATE TABLE IF NOT EXISTS command_responses (
                    response_id INTEGER PRIMARY KEY AUTOINCREMENT,
                    command_id TEXT NOT NULL,
                    status_code INTEGER NOT NULL,
                    message TEXT,
                    payload_json TEXT,
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

    def _record_event(
        self,
        connection: sqlite3.Connection,
        device_id: str,
        event_type: str,
        payload: dict[str, Any],
    ) -> None:
        connection.execute(
            """
            INSERT INTO device_events (event_id, device_id, event_type, payload_json, created_at)
            VALUES (?, ?, ?, ?, ?)
            """,
            (str(uuid.uuid4()), device_id, event_type, to_json(payload), utc_now()),
        )

    def _row_to_device(self, row: sqlite3.Row) -> dict[str, Any]:
        return {
            "device_id": row["device_id"],
            "model": row["model"],
            "firmware_version": row["firmware_version"],
            "hardware_version": row["hardware_version"],
            "hospital_id": row["hospital_id"],
            "department": row["department"],
            "latitude": row["latitude"],
            "longitude": row["longitude"],
            "metadata": from_json(row["metadata_json"], {}),
            "status": row["status"],
            "registered_at": row["registered_at"],
            "last_seen_at": row["last_seen_at"],
            "updated_at": row["updated_at"],
        }

    def _row_to_command(self, row: sqlite3.Row) -> dict[str, Any]:
        return {
            "command_id": row["command_id"],
            "device_id": row["device_id"],
            "command_type": row["command_type"],
            "correlation_id": row["correlation_id"],
            "payload": from_json(row["payload_json"], {}),
            "requires_response": bool(row["requires_response"]),
            "status": row["status"],
            "created_at": row["created_at"],
            "delivered_at": row["delivered_at"],
            "responded_at": row["responded_at"],
        }

    def register_device(self, device: dict[str, Any]) -> dict[str, Any]:
        now = utc_now()
        device_id = device["device_id"]
        with self._connect() as connection:
            connection.execute(
                """
                INSERT INTO devices (
                    device_id, model, firmware_version, hardware_version,
                    hospital_id, department, latitude, longitude, metadata_json,
                    status, registered_at, last_seen_at, updated_at
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(device_id) DO UPDATE SET
                    model = excluded.model,
                    firmware_version = excluded.firmware_version,
                    hardware_version = excluded.hardware_version,
                    hospital_id = excluded.hospital_id,
                    department = excluded.department,
                    latitude = excluded.latitude,
                    longitude = excluded.longitude,
                    metadata_json = excluded.metadata_json,
                    status = excluded.status,
                    last_seen_at = excluded.last_seen_at,
                    updated_at = excluded.updated_at
                """,
                (
                    device_id,
                    device["model"],
                    device.get("firmware_version", ""),
                    device.get("hardware_version", ""),
                    device.get("hospital_id", ""),
                    device.get("department", ""),
                    device.get("latitude"),
                    device.get("longitude"),
                    to_json(device.get("metadata", {})),
                    "online",
                    now,
                    now,
                    now,
                ),
            )
            self._record_event(connection, device_id, "register", device)
        saved = self.get_device(device_id)
        self._publish("device.registered", device_id, saved)
        return saved

    def update_heartbeat(self, device_id: str, payload: dict[str, Any]) -> dict[str, Any]:
        now = utc_now()
        with self._connect() as connection:
            cursor = connection.execute(
                """
                UPDATE devices
                SET status = ?, metadata_json = ?, last_seen_at = ?, updated_at = ?
                WHERE device_id = ?
                """,
                (
                    payload.get("status", "online"),
                    to_json(payload.get("metadata", {})),
                    now,
                    now,
                    device_id,
                ),
            )
            if cursor.rowcount == 0:
                raise KeyError(f"device not found: {device_id}")
            self._record_event(connection, device_id, "heartbeat", payload)
        saved = self.get_device(device_id)
        self._publish("device.heartbeat", device_id, saved)
        return saved

    def list_devices(self, status: str | None = None) -> list[dict[str, Any]]:
        query = "SELECT * FROM devices"
        parameters: tuple[Any, ...] = ()
        if status:
            query += " WHERE status = ?"
            parameters = (status,)
        query += " ORDER BY updated_at DESC"
        with self._connect() as connection:
            rows = connection.execute(query, parameters).fetchall()
        return [self._row_to_device(row) for row in rows]

    def get_device(self, device_id: str) -> dict[str, Any]:
        with self._connect() as connection:
            row = connection.execute(
                "SELECT * FROM devices WHERE device_id = ?",
                (device_id,),
            ).fetchone()
        if row is None:
            raise KeyError(f"device not found: {device_id}")
        return self._row_to_device(row)

    def queue_command(
        self,
        device_id: str,
        command_type: str,
        payload: dict[str, Any],
        correlation_id: str | None = None,
        requires_response: bool = True,
    ) -> dict[str, Any]:
        self.get_device(device_id)
        command_id = str(uuid.uuid4())
        now = utc_now()
        with self._connect() as connection:
            connection.execute(
                """
                INSERT INTO commands (
                    command_id, device_id, command_type, correlation_id,
                    payload_json, requires_response, status, created_at
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    command_id,
                    device_id,
                    command_type,
                    correlation_id or command_id,
                    to_json(payload),
                    int(requires_response),
                    "pending",
                    now,
                ),
            )
            self._record_event(
                connection,
                device_id,
                "command.queued",
                {
                    "command_id": command_id,
                    "command_type": command_type,
                    "correlation_id": correlation_id or command_id,
                },
            )
        saved = self.get_command(command_id)
        self._publish("command.queued", device_id, saved)
        return saved

    def get_command(self, command_id: str) -> dict[str, Any]:
        with self._connect() as connection:
            row = connection.execute(
                "SELECT * FROM commands WHERE command_id = ?",
                (command_id,),
            ).fetchone()
        if row is None:
            raise KeyError(f"command not found: {command_id}")
        return self._row_to_command(row)

    def pull_next_command(self, device_id: str) -> dict[str, Any] | None:
        with self._connect() as connection:
            row = connection.execute(
                """
                SELECT * FROM commands
                WHERE device_id = ? AND status = 'pending'
                ORDER BY created_at ASC
                LIMIT 1
                """,
                (device_id,),
            ).fetchone()
            if row is None:
                return None
            delivered_at = utc_now()
            connection.execute(
                """
                UPDATE commands
                SET status = 'delivered', delivered_at = ?
                WHERE command_id = ?
                """,
                (delivered_at, row["command_id"]),
            )
        saved = self.get_command(row["command_id"])
        self._publish("command.delivered", device_id, saved)
        return saved

    def save_command_response(
        self,
        command_id: str,
        status_code: int,
        message: str,
        payload: dict[str, Any],
    ) -> dict[str, Any]:
        command = self.get_command(command_id)
        responded_at = utc_now()
        with self._connect() as connection:
            connection.execute(
                """
                INSERT INTO command_responses (command_id, status_code, message, payload_json, created_at)
                VALUES (?, ?, ?, ?, ?)
                """,
                (command_id, status_code, message, to_json(payload), responded_at),
            )
            connection.execute(
                """
                UPDATE commands
                SET status = 'responded', responded_at = ?
                WHERE command_id = ?
                """,
                (responded_at, command_id),
            )
            self._record_event(
                connection,
                command["device_id"],
                "command.responded",
                {"command_id": command_id, "status_code": status_code, "message": message},
            )
        response = {
            "command_id": command_id,
            "status_code": status_code,
            "message": message,
            "payload": payload,
            "responded_at": responded_at,
        }
        self._publish("command.responded", command["device_id"], response)
        return response
