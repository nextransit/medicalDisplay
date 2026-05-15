#!/bin/bash
# ================================================================
# Medical Display Cloud API - curl Examples
# ================================================================

BASE_URL="${API_URL:-http://localhost:8000}"
AUTH_HEADER="X-API-Key: your-api-key"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
echo_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
echo_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# ================================================================
# Health Check
# ================================================================
test_health() {
    echo_info "Testing health endpoint..."
    response=$(curl -s -w "\n%{http_code}" "$BASE_URL/health")
    code=$(echo "$response" | tail -1)
    body=$(echo "$response" | head -1)
    
    if [ "$code" = "200" ]; then
        echo_info "Health check: OK"
        echo "  Response: $body"
    else
        echo_error "Health check failed (HTTP $code)"
    fi
    echo
}

# ================================================================
# Device Registration
# ================================================================
register_device() {
    echo_info "Registering device..."
    
    curl -s -X POST "$BASE_URL/api/v1/devices/register" \
        -H "Content-Type: application/json" \
        -H "$AUTH_HEADER" \
        -d '{
            "device_id": "DEMO-001",
            "device_name": "Test Display Station",
            "model": "MedicalDisplay-X1",
            "firmware_version": "1.0.0",
            "hardware_version": "rev-a",
            "capabilities": {
                "max_display_count": 4,
                "max_resolution_width": 4096,
                "max_resolution_height": 4096,
                "max_bit_depth": 12,
                "supports_hdr": true,
                "supports_npu": true
            }
        }' | jq '.'
    echo
}

# ================================================================
# Get Device Info
# ================================================================
get_device() {
    local device_id="${1:-DEMO-001}"
    echo_info "Getting device info: $device_id"
    
    curl -s "$BASE_URL/api/v1/devices/$device_id" \
        -H "$AUTH_HEADER" | jq '.'
    echo
}

# ================================================================
# Update Device Status
# ================================================================
update_status() {
    local device_id="${1:-DEMO-001}"
    local status="${2:-online}"
    
    echo_info "Updating device status: $device_id -> $status"
    
    curl -s -X POST "$BASE_URL/api/v1/devices/status" \
        -H "Content-Type: application/json" \
        -H "$AUTH_HEADER" \
        -d "{
            \"device_id\": \"$device_id\",
            \"status\": \"$status\"
        }" | jq '.'
    echo
}

# ================================================================
# Send Telemetry
# ================================================================
send_telemetry() {
    local device_id="${1:-DEMO-001}"
    echo_info "Sending telemetry: $device_id"
    
    curl -s -X POST "$BASE_URL/api/v1/telemetry" \
        -H "Content-Type: application/json" \
        -H "$AUTH_HEADER" \
        -d "{
            \"device_id\": \"$device_id\",
            \"timestamp\": $(date +%s),
            \"display_count\": 2,
            \"hours_used\": 42,
            \"error_count\": 0,
            \"avg_fps\": 59.8,
            \"ai_inference_latency_ms\": 18.5,
            \"backlight_hours\": 1200.5,
            \"color_accuracy_delta_e\": 1.2,
            \"temperature\": 45.0
        }" | jq '.'
    echo
}

# ================================================================
# Check Model Update
# ================================================================
check_model_update() {
    local device_id="${1:-DEMO-001}"
    local current_version="${2:-1.0.0}"
    
    echo_info "Checking model update for: $device_id (current: $current_version)"
    
    curl -s -X GET "$BASE_URL/api/v1/models/check?device_id=$device_id&current_model_version=$current_version" \
        -H "$AUTH_HEADER" | jq '.'
    echo
}

# ================================================================
# Check OTA Update
# ================================================================
check_ota_update() {
    local device_id="${1:-DEMO-001}"
    local current_version="${2:-1.0.0}"
    
    echo_info "Checking OTA update for: $device_id (current: $current_version)"
    
    curl -s -X GET "$BASE_URL/api/v1/ota/check?device_id=$device_id&current_version=$current_version" \
        -H "$AUTH_HEADER" | jq '.'
    echo
}

# ================================================================
# List Models
# ================================================================
list_models() {
    echo_info "Listing available models..."
    
    curl -s "$BASE_URL/api/v1/models?skip=0&limit=10" \
        -H "$AUTH_HEADER" | jq '.'
    echo
}

# ================================================================
# Upload Calibration Report
# ================================================================
upload_calibration() {
    local device_id="${1:-DEMO-001}"
    echo_info "Uploading calibration report: $device_id"
    
    curl -s -X POST "$BASE_URL/api/v1/calibration/upload" \
        -H "Content-Type: application/json" \
        -H "$AUTH_HEADER" \
        -d "{
            \"device_id\": \"$device_id\",
            \"display_id\": 0,
            \"report\": {
                \"timestamp\": $(date +%s),
                \"ambient_luminance\": 10.5,
                \"max_luminance\": 505.0,
                \"min_luminance\": 0.8,
                \"contrast_ratio\": 1500.0,
                \"delta_e_avg\": 1.2,
                \"delta_e_max\": 2.1,
                \"gsdf_error_max\": 1.5,
                \"gsdf_error_avg\": 0.8,
                \"uniformity_9point\": [0.98, 0.97, 0.98, 0.97, 0.99, 0.97, 0.98, 0.96, 0.97],
                \"next_calibration_due\": $(($(date +%s) + 15552000))
            }
        }" | jq '.'
    echo
}

# ================================================================
# Get Calibration Guidance
# ================================================================
get_calibration_guidance() {
    local device_id="${1:-DEMO-001}"
    echo_info "Getting calibration guidance: $device_id"
    
    curl -s "$BASE_URL/api/v1/calibration/guidance/$device_id?display_id=0" \
        -H "$AUTH_HEADER" | jq '.'
    echo
}

# ================================================================
# Analytics Overview
# ================================================================
get_analytics() {
    echo_info "Getting analytics overview..."
    
    curl -s "$BASE_URL/api/v1/analytics/overview" \
        -H "$AUTH_HEADER" | jq '.'
    echo
}

# ================================================================
# Get Telemetry History
# ================================================================
get_telemetry_history() {
    local device_id="${1:-DEMO-001}"
    echo_info "Getting telemetry history: $device_id"
    
    curl -s "$BASE_URL/api/v1/telemetry/$device_id?limit=10" \
        -H "$AUTH_HEADER" | jq '.'
    echo
}

# ================================================================
# Run All Tests
# ================================================================
run_all_tests() {
    echo "================================================"
    echo "Medical Display Cloud API - Test Suite"
    echo "================================================"
    echo
    
    test_health
    register_device
    get_device "DEMO-001"
    update_status "DEMO-001" "online"
    send_telemetry "DEMO-001"
    list_models
    check_model_update "DEMO-001" "1.0.0"
    check_ota_update "DEMO-001" "1.0.0"
    upload_calibration "DEMO-001"
    get_calibration_guidance "DEMO-001"
    get_telemetry_history "DEMO-001"
    get_analytics
    
    echo_info "All tests completed!"
}

# ================================================================
# Main
# ================================================================
case "${1:-all}" in
    health) test_health ;;
    register) register_device ;;
    device) get_device "${2:-DEMO-001}" ;;
    status) update_status "${2:-DEMO-001}" "${3:-online}" ;;
    telemetry) send_telemetry "${2:-DEMO-001}" ;;
    models) list_models ;;
    model-update) check_model_update "${2:-DEMO-001}" "${3:-1.0.0}" ;;
    ota-update) check_ota_update "${2:-DEMO-001}" "${3:-1.0.0}" ;;
    calibration) upload_calibration "${2:-DEMO-001}" ;;
    guidance) get_calibration_guidance "${2:-DEMO-001}" ;;
    analytics) get_analytics ;;
    telemetry-history) get_telemetry_history "${2:-DEMO-001}" ;;
    all) run_all_tests ;;
    *) 
        echo "Usage: $0 {health|register|device|status|telemetry|models|model-update|ota-update|calibration|guidance|analytics|telemetry-history|all}"
        echo ""
        echo "Examples:"
        echo "  $0 health                    # Test health endpoint"
        echo "  $0 register                 # Register a device"
        echo "  $0 device DEMO-001           # Get device info"
        echo "  $0 status DEMO-001 online   # Update device status"
        echo "  $0 telemetry DEMO-001       # Send telemetry"
        echo "  $0 all                      # Run all tests"
        ;;
esac
