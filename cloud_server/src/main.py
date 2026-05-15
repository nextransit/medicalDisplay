from __future__ import annotations

import json
import logging
import os
from contextlib import asynccontextmanager
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import paho.mqtt.client as mqtt
from fastapi import Depends, FastAPI, HTTPException, Query, Request, status
from fastapi.responses import FileResponse
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from pydantic import BaseModel, Field

from .calibration_service import CalibrationService
from .device_manager import DeviceManager
from .model_store import ModelStore
from .ota_service import OTAService
from .telemetry_collector import TelemetryCollector

ROOT_DIR = Path(__file__).resolve().parents[1]
SETTINGS_PATH = Path(os.environ.get("CLOUD_SERVER_SETTINGS", ROOT_DIR / "config" / "settings.json"))


def load_settings(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


SETTINGS = load_settings(SETTINGS_PATH)

logging.basicConfig(
    level=getattr(logging, SETTINGS.get("app", {}).get("log_level", "INFO").upper(), logging.INFO),
    format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
)
LOGGER = logging.getLogger("medical_display_cloud_server")


def resolve_path(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else (ROOT_DIR / path).resolve()


class MqttEventBus:
    def __init__(self, config: dict[str, Any]) -> None:
        self.config = config
        self.enabled = bool(config.get("enabled", False))
        self.connected = False
        self.topic_prefix = str(config.get("topic_prefix", "medical-display/cloud")).strip("/")
        self.client: mqtt.Client | None = None
        if not self.enabled:
            return
        client_id = config.get("client_id") or "medical-display-cloud-server"
        if hasattr(mqtt, "CallbackAPIVersion"):
            self.client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION1, client_id=client_id)
        else:
            self.client = mqtt.Client(client_id=client_id)
        if config.get("username"):
            self.client.username_pw_set(config["username"], config.get("password") or None)
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect

    def _on_connect(self, client: mqtt.Client, userdata: Any, flags: dict[str, Any], rc: int) -> None:
        self.connected = rc == 0
        if self.connected:
            LOGGER.info("MQTT broker connected")
        else:
            LOGGER.warning("MQTT broker connect failed: rc=%s", rc)

    def _on_disconnect(self, client: mqtt.Client, userdata: Any, rc: int) -> None:
        self.connected = False
        LOGGER.warning("MQTT broker disconnected: rc=%s", rc)

    def start(self) -> None:
        if not self.enabled or not self.client:
            return
        try:
            self.client.connect(
                str(self.config.get("host", "localhost")),
                int(self.config.get("port", 1883)),
                int(self.config.get("keepalive", 30)),
            )
            self.client.loop_start()
        except Exception as exc:
            LOGGER.warning("MQTT broker unavailable: %s", exc)
            self.connected = False

    def close(self) -> None:
        if not self.client:
            return
        try:
            self.client.loop_stop()
            self.client.disconnect()
        except Exception:
            return

    def publish(self, event_name: str, key: str, payload: dict[str, Any]) -> None:
        if not self.enabled or not self.client:
            return
        topic = "/".join(part for part in [self.topic_prefix, event_name.replace(".", "/"), key] if part)
        self.client.publish(topic, json.dumps(payload, ensure_ascii=False), qos=1, retain=False)

    def status(self) -> dict[str, Any]:
        return {
            "enabled": self.enabled,
            "connected": self.connected,
            "topic_prefix": self.topic_prefix,
            "host": self.config.get("host", "localhost"),
            "port": self.config.get("port", 1883),
        }


@dataclass
class CloudServerContext:
    settings: dict[str, Any]
    mqtt_bus: MqttEventBus
    device_manager: DeviceManager
    telemetry_collector: TelemetryCollector
    model_store: ModelStore
    ota_service: OTAService
    calibration_service: CalibrationService
    database_path: Path
    models_dir: Path
    uploads_dir: Path


def build_context(settings: dict[str, Any]) -> CloudServerContext:
    database_path = resolve_path(settings["app"]["database_path"])
    models_dir = resolve_path(settings["storage"]["models_dir"])
    uploads_dir = resolve_path(settings["storage"]["uploads_dir"])
    artifacts_dir = resolve_path(settings["storage"].get("artifacts_dir", settings["storage"]["models_dir"]))
    mqtt_bus = MqttEventBus(settings.get("mqtt", {}))
    publisher = mqtt_bus.publish
    return CloudServerContext(
        settings=settings,
        mqtt_bus=mqtt_bus,
        device_manager=DeviceManager(database_path, event_publisher=publisher),
        telemetry_collector=TelemetryCollector(database_path, event_publisher=publisher),
        model_store=ModelStore(
            database_path,
            models_dir=models_dir,
            uploads_dir=uploads_dir,
            public_download_prefix="/api/v1/models",
            event_publisher=publisher,
        ),
        ota_service=OTAService(
            database_path,
            artifacts_dir=artifacts_dir,
            seed_releases=settings.get("ota", {}).get("seed_releases", []),
            public_download_prefix="/api/v1/ota/releases",
            event_publisher=publisher,
        ),
        calibration_service=CalibrationService(database_path, uploads_dir=uploads_dir, event_publisher=publisher),
        database_path=database_path,
        models_dir=models_dir,
        uploads_dir=uploads_dir,
    )


@asynccontextmanager
async def lifespan(app: FastAPI):
    context = build_context(SETTINGS)
    context.mqtt_bus.start()
    app.state.context = context
    try:
        yield
    finally:
        context.mqtt_bus.close()


app = FastAPI(
    title="Medical Display Cloud Server",
    version="1.0.0",
    description="AI 自适应医疗显示系统的云端服务端参考实现",
    lifespan=lifespan,
)

bearer_scheme = HTTPBearer(auto_error=False)


def get_context(request: Request) -> CloudServerContext:
    return request.app.state.context


def require_api_token(
    request: Request,
    credentials: HTTPAuthorizationCredentials | None = Depends(bearer_scheme),
) -> str:
    context = get_context(request)
    security = context.settings.get("security", {})
    if security.get("allow_anonymous", True):
        return "anonymous"
    expected = security.get("api_token")
    if not credentials or credentials.credentials != expected:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid api token")
    return credentials.credentials


class DeviceRegistrationRequest(BaseModel):
    device_id: str
    model: str
    firmware_version: str = ""
    hardware_version: str = ""
    hospital_id: str = ""
    department: str = ""
    latitude: float | None = None
    longitude: float | None = None
    metadata: dict[str, Any] = Field(default_factory=dict)


class HeartbeatRequest(BaseModel):
    status: str = "online"
    metadata: dict[str, Any] = Field(default_factory=dict)


class TelemetrySampleRequest(BaseModel):
    timestamp: str | None = None
    hours_used: float = 0.0
    ai_inference_latency_ms: float = 0.0
    avg_fps: float = 0.0
    cpu_usage_percent: float = 0.0
    memory_usage_mb: float = 0.0
    gpu_temperature_c: float = 0.0
    network_tx_bytes: int = 0
    network_rx_bytes: int = 0
    error_count: int = 0
    metadata: dict[str, Any] = Field(default_factory=dict)


class TelemetryBatchRequest(BaseModel):
    device_id: str
    samples: list[TelemetrySampleRequest] = Field(default_factory=list)


class InferenceStatsRequest(BaseModel):
    timestamp: str | None = None
    total_inferences: int = 0
    successful_inferences: int = 0
    avg_latency_ms: float = 0.0
    p50_latency_ms: float = 0.0
    p95_latency_ms: float = 0.0
    p99_latency_ms: float = 0.0
    modality_counts: list[int] = Field(default_factory=list)


class QCReportRequest(BaseModel):
    report_id: str | None = None
    device_id: str
    timestamp: str | None = None
    delta_e: float = 0.0
    luminance: float = 0.0
    backlight_hours: float = 0.0
    temperature: float = 0.0
    health_score: float = 0.0
    recommendations: str = ""


class ModelUpdateCheckRequest(BaseModel):
    current_version: str
    model_id: str | None = None
    modality: str | None = None
    firmware_version: str | None = None
    device_model: str | None = None


class GradientUploadRequest(BaseModel):
    device_id: str
    model_id: str
    payload_base64: str
    metadata: dict[str, Any] = Field(default_factory=dict)


class OtaCheckRequest(BaseModel):
    device_id: str | None = None
    current_version: str
    channel: str = "stable"


class OtaReleaseCreateRequest(BaseModel):
    release_id: str | None = None
    version: str
    base_version: str = ""
    channel: str = "stable"
    file_name: str
    delta_file_name: str | None = None
    checksum: str = ""
    signature: str = ""
    install_path: str = ""
    changelog: str = ""
    is_mandatory: bool = False
    is_security_update: bool = False
    published_at: str | None = None


class RolloutStatusRequest(BaseModel):
    device_id: str
    release_id: str
    status: str
    details: dict[str, Any] = Field(default_factory=dict)


class CalibrationReportRequest(BaseModel):
    calibration_id: str | None = None
    device_id: str
    timestamp: str | None = None
    delta_e: float = 0.0
    luminance: float = 0.0
    uniformity: float = 1.0
    gamma: float = 2.2
    report_json: dict[str, Any] | str | None = None


class CalibrationSyncRequest(BaseModel):
    device_id: str
    payload_base64: str


class CommandCreateRequest(BaseModel):
    device_id: str
    command_type: str
    correlation_id: str | None = None
    requires_response: bool = True
    payload: dict[str, Any] = Field(default_factory=dict)


class CommandResponseRequest(BaseModel):
    status_code: int
    message: str
    payload: dict[str, Any] = Field(default_factory=dict)


def raise_not_found(error: KeyError) -> None:
    raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=str(error)) from error


@app.get("/health")
def health(request: Request) -> dict[str, Any]:
    context = get_context(request)
    return {
        "status": "ok",
        "service": context.settings["app"]["name"],
        "database": str(context.database_path),
        "models_dir": str(context.models_dir),
        "mqtt": context.mqtt_bus.status(),
    }


@app.post("/api/v1/devices/register")
def register_device(
    payload: DeviceRegistrationRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    return context.device_manager.register_device(payload.model_dump())


@app.get("/api/v1/devices")
def list_devices(
    request: Request,
    status_value: str | None = Query(default=None, alias="status"),
    token: str = Depends(require_api_token),
) -> list[dict[str, Any]]:
    context = get_context(request)
    return context.device_manager.list_devices(status=status_value)


@app.get("/api/v1/devices/{device_id}")
def get_device(device_id: str, request: Request, token: str = Depends(require_api_token)) -> dict[str, Any]:
    context = get_context(request)
    try:
        return context.device_manager.get_device(device_id)
    except KeyError as error:
        raise_not_found(error)


@app.post("/api/v1/devices/{device_id}/heartbeat")
def device_heartbeat(
    device_id: str,
    payload: HeartbeatRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    try:
        return context.device_manager.update_heartbeat(device_id, payload.model_dump())
    except KeyError as error:
        raise_not_found(error)


@app.post("/api/v1/devices/{device_id}/telemetry")
def ingest_telemetry(
    device_id: str,
    payload: TelemetrySampleRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    return context.telemetry_collector.ingest(device_id, payload.model_dump())


@app.post("/api/v1/telemetry/batch")
def ingest_telemetry_batch(
    payload: TelemetryBatchRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    samples = [item.model_dump() for item in payload.samples]
    return context.telemetry_collector.ingest_batch(payload.device_id, samples)


@app.get("/api/v1/devices/{device_id}/telemetry")
def get_device_telemetry(
    device_id: str,
    request: Request,
    limit: int = Query(default=20, ge=1, le=200),
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    return {
        "device_id": device_id,
        "samples": context.telemetry_collector.latest_for_device(device_id, limit=limit),
        "summary": context.telemetry_collector.summary_for_device(device_id),
    }


@app.post("/api/v1/devices/{device_id}/inference-stats")
def report_inference_stats(
    device_id: str,
    payload: InferenceStatsRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    return context.telemetry_collector.report_inference_stats(device_id, payload.model_dump())


@app.post("/api/v1/qc/reports")
def report_qc(
    payload: QCReportRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    return context.telemetry_collector.report_qc(payload.model_dump())


@app.get("/api/v1/devices/{device_id}/recommendations")
def get_recommendations(
    device_id: str,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    try:
        device = context.device_manager.get_device(device_id)
    except KeyError as error:
        raise_not_found(error)
    telemetry_summary = context.telemetry_collector.summary_for_device(device_id)
    calibration_guidance = context.calibration_service.build_guidance(device_id)
    ota_update = context.ota_service.check_update(
        current_version=device.get("firmware_version") or "0.0.0",
        channel=context.settings.get("ota", {}).get("default_channel", "stable"),
        device_id=device_id,
    )
    recommendations: list[str] = []
    latest = telemetry_summary.get("latest") or {}
    if latest.get("gpu_temperature_c", 0) > 80:
        recommendations.append("GPU 温度偏高，建议检查散热与风扇运行状态。")
    if latest.get("error_count", 0) > 0:
        recommendations.append("最近有错误计数，建议检查网络连接和日志。")
    recommendations.extend(calibration_guidance["guidance"])
    if ota_update:
        recommendations.append(f"发现可用固件 {ota_update['version']}，建议安排维护窗口升级。")
    if not recommendations:
        recommendations.append("设备状态平稳，保持现有遥测和校准节奏即可。")
    return {
        "device": device,
        "telemetry_summary": telemetry_summary,
        "calibration_guidance": calibration_guidance,
        "ota_update": ota_update,
        "recommendations": recommendations,
    }


@app.get("/api/v1/models")
def list_models(
    request: Request,
    modality: str | None = Query(default=None),
    token: str = Depends(require_api_token),
) -> list[dict[str, Any]]:
    context = get_context(request)
    return context.model_store.list_models(modality=modality)


@app.post("/api/v1/models/rescan")
def rescan_models(request: Request, token: str = Depends(require_api_token)) -> dict[str, Any]:
    context = get_context(request)
    models = context.model_store.rescan_models()
    return {"count": len(models), "models": models}


@app.post("/api/v1/models/check-update")
def check_model_update(
    payload: ModelUpdateCheckRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    if not payload.model_id and not payload.modality:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail="model_id or modality is required")
    update = context.model_store.check_update(
        current_version=payload.current_version,
        model_id=payload.model_id,
        modality=payload.modality,
        firmware_version=payload.firmware_version,
        device_model=payload.device_model,
    )
    return {"has_update": update is not None, "model": update}


@app.get("/api/v1/models/{model_id}/{version}/download")
def download_model(model_id: str, version: str, request: Request, token: str = Depends(require_api_token)) -> FileResponse:
    context = get_context(request)
    try:
        path = context.model_store.get_download_path(model_id, version)
    except KeyError as error:
        raise_not_found(error)
    except FileNotFoundError as error:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=str(error)) from error
    return FileResponse(path, media_type="application/octet-stream", filename=path.name)


@app.get("/api/v1/models/federated/{model_id}")
def get_federated_model(model_id: str, request: Request, token: str = Depends(require_api_token)) -> dict[str, Any]:
    context = get_context(request)
    model = context.model_store.latest_federated_model(model_id)
    return {"model": model, "available": model is not None}


@app.post("/api/v1/models/gradients")
def upload_gradients(
    payload: GradientUploadRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    try:
        return context.model_store.record_gradient_upload(
            device_id=payload.device_id,
            model_id=payload.model_id,
            payload_base64=payload.payload_base64,
            metadata=payload.metadata,
        )
    except Exception as error:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=f"invalid gradient payload: {error}") from error


@app.get("/api/v1/ota/releases")
def list_ota_releases(
    request: Request,
    channel: str | None = Query(default=None),
    token: str = Depends(require_api_token),
) -> list[dict[str, Any]]:
    context = get_context(request)
    return context.ota_service.list_releases(channel=channel)


@app.post("/api/v1/ota/check")
def check_ota(
    payload: OtaCheckRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    update = context.ota_service.check_update(
        current_version=payload.current_version,
        channel=payload.channel,
        device_id=payload.device_id,
    )
    return {"has_update": update is not None, "update": update}


@app.post("/api/v1/ota/releases")
def create_ota_release(
    payload: OtaReleaseCreateRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    return context.ota_service.create_release(payload.model_dump(exclude_none=True))


@app.get("/api/v1/ota/releases/{release_id}/download")
def download_ota_release(
    release_id: str,
    request: Request,
    delta: bool = Query(default=False),
    token: str = Depends(require_api_token),
) -> FileResponse:
    context = get_context(request)
    try:
        path = context.ota_service.get_download_path(release_id, delta=delta)
    except KeyError as error:
        raise_not_found(error)
    except FileNotFoundError as error:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=str(error)) from error
    return FileResponse(path, media_type="application/octet-stream", filename=path.name)


@app.post("/api/v1/ota/rollouts")
def report_rollout_status(
    payload: RolloutStatusRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    return context.ota_service.mark_rollout(
        device_id=payload.device_id,
        release_id=payload.release_id,
        status=payload.status,
        details=payload.details,
    )


@app.post("/api/v1/calibration/reports")
def report_calibration(
    payload: CalibrationReportRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    return context.calibration_service.save_report(payload.model_dump(exclude_none=True))


@app.get("/api/v1/calibration/{device_id}/guidance")
def calibration_guidance(device_id: str, request: Request, token: str = Depends(require_api_token)) -> dict[str, Any]:
    context = get_context(request)
    return context.calibration_service.build_guidance(device_id)


@app.post("/api/v1/calibration/sync")
def sync_calibration(
    payload: CalibrationSyncRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    try:
        return context.calibration_service.save_sync_payload(payload.device_id, payload.payload_base64)
    except Exception as error:
        raise HTTPException(status_code=status.HTTP_400_BAD_REQUEST, detail=f"invalid calibration payload: {error}") from error


@app.post("/api/v1/commands")
def queue_command(
    payload: CommandCreateRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    try:
        return context.device_manager.queue_command(
            device_id=payload.device_id,
            command_type=payload.command_type,
            payload=payload.payload,
            correlation_id=payload.correlation_id,
            requires_response=payload.requires_response,
        )
    except KeyError as error:
        raise_not_found(error)


@app.get("/api/v1/devices/{device_id}/commands/next")
def pull_next_command(device_id: str, request: Request, token: str = Depends(require_api_token)) -> dict[str, Any]:
    context = get_context(request)
    return {"command": context.device_manager.pull_next_command(device_id)}


@app.post("/api/v1/commands/{command_id}/response")
def command_response(
    command_id: str,
    payload: CommandResponseRequest,
    request: Request,
    token: str = Depends(require_api_token),
) -> dict[str, Any]:
    context = get_context(request)
    try:
        return context.device_manager.save_command_response(
            command_id=command_id,
            status_code=payload.status_code,
            message=payload.message,
            payload=payload.payload,
        )
    except KeyError as error:
        raise_not_found(error)


@app.get("/api/v1/time/sync")
def sync_time(request: Request, token: str = Depends(require_api_token)) -> dict[str, Any]:
    return {"server_time": datetime.now(timezone.utc).isoformat(), "offset_seconds": 0.0}
