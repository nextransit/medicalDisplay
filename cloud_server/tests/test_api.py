"""
Medical Display Cloud API - pytest Tests
"""

import pytest
import httpx
from datetime import datetime
from httpx import AsyncClient
import sys
import os

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from main import app

# Test base URL
BASE_URL = "http://testserver"
TIMEOUT = 30.0


@pytest.fixture
async def client():
    """Create async HTTP client for testing."""
    async with AsyncClient(app=app, base_url=BASE_URL, timeout=TIMEOUT) as ac:
        yield ac


@pytest.fixture
def sample_device():
    """Sample device registration data."""
    return {
        "device_id": f"TEST-{datetime.now().strftime('%Y%m%d%H%M%S')}",
        "device_name": "Test Display Station",
        "model": "MedicalDisplay-X1",
        "firmware_version": "1.0.0",
        "hardware_version": "rev-a",
        "capabilities": {
            "max_display_count": 4,
            "max_resolution_width": 4096,
            "max_resolution_height": 4096,
            "max_bit_depth": 12,
            "supports_hdr": True,
            "supports_npu": True
        }
    }


@pytest.fixture
def sample_telemetry():
    """Sample telemetry data."""
    return {
        "device_id": "TEST-001",
        "timestamp": int(datetime.now().timestamp()),
        "display_count": 2,
        "hours_used": 42,
        "error_count": 0,
        "avg_fps": 59.8,
        "ai_inference_latency_ms": 18.5,
        "backlight_hours": 1200.5,
        "color_accuracy_delta_e": 1.2,
        "temperature": 45.0
    }


# ================================================================
# Health Tests
# ================================================================

@pytest.mark.asyncio
async def test_health_check(client: AsyncClient):
    """Test health endpoint returns OK."""
    response = await client.get("/health")
    assert response.status_code == 200
    
    data = response.json()
    assert data["status"] == "healthy"
    assert "timestamp" in data


# ================================================================
# Device Management Tests
# ================================================================

@pytest.mark.asyncio
async def test_register_device(client: AsyncClient, sample_device):
    """Test device registration."""
    response = await client.post("/api/v1/devices/register", json=sample_device)
    assert response.status_code == 200
    
    data = response.json()
    assert data["success"] is True
    assert data["device"]["device_id"] == sample_device["device_id"]


@pytest.mark.asyncio
async def test_get_device(client: AsyncClient, sample_device):
    """Test getting device info."""
    # First register
    await client.post("/api/v1/devices/register", json=sample_device)
    
    # Then get
    response = await client.get(f"/api/v1/devices/{sample_device['device_id']}")
    assert response.status_code == 200
    
    data = response.json()
    assert data["device_id"] == sample_device["device_id"]
    assert data["device_name"] == sample_device["device_name"]


@pytest.mark.asyncio
async def test_get_device_not_found(client: AsyncClient):
    """Test getting non-existent device."""
    response = await client.get("/api/v1/devices/NONEXISTENT")
    assert response.status_code == 404


@pytest.mark.asyncio
async def test_update_device_status(client: AsyncClient, sample_device):
    """Test updating device status."""
    # Register first
    await client.post("/api/v1/devices/register", json=sample_device)
    
    # Update status
    response = await client.post("/api/v1/devices/status", json={
        "device_id": sample_device["device_id"],
        "status": "online"
    })
    assert response.status_code == 200
    assert response.json()["success"] is True


@pytest.mark.asyncio
async def test_list_devices(client: AsyncClient, sample_device):
    """Test listing devices."""
    # Register device first
    await client.post("/api/v1/devices/register", json=sample_device)
    
    # List
    response = await client.get("/api/v1/devices?skip=0&limit=10")
    assert response.status_code == 200
    
    data = response.json()
    assert "devices" in data
    assert "total" in data
    assert isinstance(data["devices"], list)


# ================================================================
# Telemetry Tests
# ================================================================

@pytest.mark.asyncio
async def test_send_telemetry(client: AsyncClient, sample_telemetry):
    """Test sending telemetry data."""
    response = await client.post("/api/v1/telemetry", json=sample_telemetry)
    assert response.status_code == 200
    assert response.json()["success"] is True


@pytest.mark.asyncio
async def test_get_telemetry_history(client: AsyncClient, sample_telemetry):
    """Test getting telemetry history."""
    # Send some telemetry first
    for _ in range(3):
        await client.post("/api/v1/telemetry", json=sample_telemetry)
    
    # Get history
    response = await client.get(f"/api/v1/telemetry/{sample_telemetry['device_id']}")
    assert response.status_code == 200
    
    data = response.json()
    assert "history" in data


# ================================================================
# Model Tests
# ================================================================

@pytest.mark.asyncio
async def test_list_models(client: AsyncClient):
    """Test listing models."""
    response = await client.get("/api/v1/models?skip=0&limit=10")
    assert response.status_code == 200
    
    data = response.json()
    assert "models" in data


@pytest.mark.asyncio
async def test_check_model_update(client: AsyncClient):
    """Test checking for model updates."""
    response = await client.get(
        "/api/v1/models/check",
        params={
            "device_id": "TEST-001",
            "current_model_version": "0.0.1"
        }
    )
    assert response.status_code == 200
    
    data = response.json()
    assert "update_available" in data


@pytest.mark.asyncio
async def test_get_model_info(client: AsyncClient):
    """Test getting model info."""
    # First get the model ID from list
    list_response = await client.get("/api/v1/models?skip=0&limit=1")
    models = list_response.json()["models"]
    
    if models:
        model_id = models[0]["model_id"]
        response = await client.get(f"/api/v1/models/{model_id}")
        assert response.status_code == 200
        
        data = response.json()
        assert "model_id" in data


# ================================================================
# OTA Tests
# ================================================================

@pytest.mark.asyncio
async def test_check_ota_update(client: AsyncClient):
    """Test checking for OTA updates."""
    response = await client.get(
        "/api/v1/ota/check",
        params={
            "device_id": "TEST-001",
            "current_version": "1.0.0"
        }
    )
    assert response.status_code == 200
    
    data = response.json()
    assert "update_available" in data


# ================================================================
# Calibration Tests
# ================================================================

@pytest.mark.asyncio
async def test_upload_calibration(client: AsyncClient):
    """Test uploading calibration report."""
    response = await client.post("/api/v1/calibration/upload", json={
        "device_id": "TEST-001",
        "display_id": 0,
        "report": {
            "timestamp": int(datetime.now().timestamp()),
            "ambient_luminance": 10.5,
            "max_luminance": 505.0,
            "delta_e_avg": 1.2,
            "delta_e_max": 2.1,
            "gsdf_error_max": 1.5,
            "next_calibration_due": int(datetime.now().timestamp()) + 15552000
        }
    })
    assert response.status_code == 200
    
    data = response.json()
    assert data["success"] is True
    assert "report_id" in data


@pytest.mark.asyncio
async def test_get_calibration_guidance(client: AsyncClient):
    """Test getting calibration guidance."""
    response = await client.get("/api/v1/calibration/guidance/TEST-001?display_id=0")
    assert response.status_code == 200
    
    data = response.json()
    assert "guidance" in data


@pytest.mark.asyncio
async def test_get_calibration_history(client: AsyncClient):
    """Test getting calibration history."""
    response = await client.get("/api/v1/calibration/history/TEST-001?limit=10")
    assert response.status_code == 200
    
    data = response.json()
    assert "history" in data


# ================================================================
# Analytics Tests
# ================================================================

@pytest.mark.asyncio
async def test_analytics_overview(client: AsyncClient):
    """Test getting analytics overview."""
    response = await client.get("/api/v1/analytics/overview")
    assert response.status_code == 200
    
    data = response.json()
    assert "total_devices" in data
    assert "active_devices" in data
    assert "models_deployed" in data


# ================================================================
# Error Handling Tests
# ================================================================

@pytest.mark.asyncio
async def test_invalid_json(client: AsyncClient):
    """Test handling of invalid JSON."""
    response = await client.post(
        "/api/v1/devices/register",
        content=b"not valid json",
        headers={"Content-Type": "application/json"}
    )
    assert response.status_code in [400, 422]


@pytest.mark.asyncio
async def test_missing_required_fields(client: AsyncClient):
    """Test handling of missing required fields."""
    response = await client.post(
        "/api/v1/devices/register",
        json={"device_id": "TEST"}  # Missing required fields
    )
    assert response.status_code in [400, 422]


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
