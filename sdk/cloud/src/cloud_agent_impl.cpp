#include "cloud_agent.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef PLATFORM_LINUX
#include <unistd.h>
#include <fcntl.h>
#endif

#ifdef HAS_CURL
#include <curl/curl.h>
#endif

#ifdef HAS_OPENSSL
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#endif

#ifdef HAS_NLOHMANN_JSON
#include <nlohmann/json.hpp>
using Json = nlohmann::json;
#elif defined(HAS_CJSON)
#include <cjson/cJSON.h>
#endif

#ifdef HAS_MQTT
#include <mosquitto.h>
#endif

struct TopicSet {
    std::string register_req;
    std::string register_resp;
    std::string heartbeat;
    std::string telemetry;
    std::string telemetry_batch;
    std::string ota_check_req;
    std::string ota_check_resp;
    std::string ota_progress;
    std::string model_check_req;
    std::string model_check_resp;
    std::string command_req;
    std::string command_resp;
    std::string qc_report;
    std::string qc_guidance_req;
    std::string qc_guidance_resp;
    std::string inference_stats;
    std::string federated_model_req;
    std::string federated_model_resp;
    std::string gradients;
    std::string calibration_sync;
    std::string time_sync_req;
    std::string time_sync_resp;
    std::string events;
};

struct CloudAgent {
    CloudAgentConfig config {};
    DeviceInfo device_info {};
    TopicSet topics;

    std::atomic<CloudState> state {CLOUD_STATE_DISCONNECTED};
    std::atomic<bool> stop_threads {false};
    std::atomic<bool> connected {false};
    std::atomic<bool> registered {false};

    mutable std::mutex mutex;
    std::condition_variable ack_cv;
    std::condition_variable command_cv;

    std::deque<CloudTelemetry> telemetry_queue;
    std::deque<CloudCommand> command_queue;
    std::unordered_map<std::string, std::string> ack_payloads;

    std::thread heartbeat_thread;
    std::thread telemetry_thread;

    CloudEventCallback event_callback = nullptr;
    void* event_userdata = nullptr;

    std::string staging_dir;
    std::string last_update_download_path;
    std::string last_update_target_path;
    std::string previous_firmware_version;
    std::string previous_model_version;
    std::string last_qc_guidance;
    std::string last_federated_model_blob;

#ifdef HAS_MQTT
    mosquitto* mqtt = nullptr;
    std::mutex mqtt_publish_mutex;
#endif
};

namespace {

constexpr int kDefaultReconnectSeconds = 10;
constexpr int kDefaultHeartbeatSeconds = 60;
constexpr int kDefaultTelemetryBatchSize = 8;
constexpr int kDefaultCommandTimeoutMs = 1000;
constexpr size_t kIoBufferSize = 64 * 1024;

static inline uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

static inline std::string iso8601_now() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf {};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &time);
#else
    gmtime_r(&time, &tm_buf);
#endif
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buffer);
}

static inline std::string trim_trailing_slashes(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

static inline std::string safe_string(const char* value) {
    return value ? std::string(value) : std::string();
}

static inline void copy_cstr(char* dest, size_t size, const std::string& src) {
    if (!dest || size == 0) {
        return;
    }
    std::snprintf(dest, size, "%s", src.c_str());
}

// [P0-FIX] ensure_directory: 安全的递归目录创建，移除 std::system 命令注入风险
static bool ensure_directory(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    // [P0-FIX] 拒绝 shell 元字符和路径遍历
    const char* dangerous_chars = "\"'`;$()|<>&\\";
    for (const char* p = dangerous_chars; *p; ++p) {
        if (path.find(*p) != std::string::npos) {
            fprintf(stderr, "[SECURITY] ensure_directory: dangerous char '%c' in path\n", *p);
            return false;
        }
    }

    // 拒绝路径遍历
    if (path.find("..") != std::string::npos) {
        fprintf(stderr, "[SECURITY] ensure_directory: '..' not allowed in path\n");
        return false;
    }

    // 尝试直接创建（大多数情况）
    if (::mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
    if (errno == EEXIST) {
        // 已存在且是目录
        struct stat st {};
        if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            return true;
        }
        errno = ENOTDIR;  // 路径上有同名文件
        return false;
    }
    if (errno != ENOENT) {
        return false;  // 权限错误或其他问题
    }
    // 递归创建父目录
    const std::string::size_type pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return false;  // 没有父路径
    }
    const std::string parent = path.substr(0, pos);
    if (!ensure_directory(parent)) {
        return false;
    }
    // 再次尝试创建本级目录
    return ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

static inline bool file_exists_cpp(const std::string& path) {
    struct stat st {};
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static inline size_t file_size_cpp(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return static_cast<size_t>(st.st_size);
}

static inline std::string dirname_of(const std::string& path) {
    const auto pos = path.find_last_of("/\\\\");
    if (pos == std::string::npos) {
        return ".";
    }
    return path.substr(0, pos);
}

static inline std::string join_path(const std::string& base, const std::string& name) {
    if (base.empty()) {
        return name;
    }
    if (name.empty()) {
        return base;
    }
    if (base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
}

static inline std::string strip_scheme(const std::string& url) {
    const auto scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos) {
        return url;
    }
    return url.substr(scheme_pos + 3);
}

static inline std::string base_name_from_url(const std::string& url) {
    auto stripped = strip_scheme(url);
    const auto pos = stripped.find_last_of('/');
    if (pos == std::string::npos || pos + 1 >= stripped.size()) {
        return "payload.bin";
    }
    return stripped.substr(pos + 1);
}

static inline std::string normalize_server(const CloudAgentConfig& config) {
    if (config.server_url[0] == '\0') {
        return "localhost:1883";
    }
    return trim_trailing_slashes(config.server_url);
}

static inline TopicSet make_topics(const DeviceInfo& device) {
    const std::string device_id = device.device_id[0] ? device.device_id : "unknown-device";
    TopicSet topics;
    topics.register_req = "medicaldisplay/devices/" + device_id + "/register";
    topics.register_resp = "medicaldisplay/devices/" + device_id + "/register/ack";
    topics.heartbeat = "medicaldisplay/devices/" + device_id + "/heartbeat";
    topics.telemetry = "medicaldisplay/devices/" + device_id + "/telemetry";
    topics.telemetry_batch = "medicaldisplay/devices/" + device_id + "/telemetry/batch";
    topics.ota_check_req = "medicaldisplay/devices/" + device_id + "/ota/check";
    topics.ota_check_resp = "medicaldisplay/devices/" + device_id + "/ota/result";
    topics.ota_progress = "medicaldisplay/devices/" + device_id + "/ota/progress";
    topics.model_check_req = "medicaldisplay/devices/" + device_id + "/models/check";
    topics.model_check_resp = "medicaldisplay/devices/" + device_id + "/models/result";
    topics.command_req = "medicaldisplay/devices/" + device_id + "/commands";
    topics.command_resp = "medicaldisplay/devices/" + device_id + "/commands/response";
    topics.qc_report = "medicaldisplay/devices/" + device_id + "/calibration/report";
    topics.qc_guidance_req = "medicaldisplay/devices/" + device_id + "/calibration/guidance";
    topics.qc_guidance_resp = "medicaldisplay/devices/" + device_id + "/calibration/guidance/resp";
    topics.inference_stats = "medicaldisplay/devices/" + device_id + "/stats/inference";
    topics.federated_model_req = "medicaldisplay/devices/" + device_id + "/federated/model";
    topics.federated_model_resp = "medicaldisplay/devices/" + device_id + "/federated/model/resp";
    topics.gradients = "medicaldisplay/devices/" + device_id + "/federated/gradients";
    topics.calibration_sync = "medicaldisplay/devices/" + device_id + "/calibration/sync";
    topics.time_sync_req = "medicaldisplay/devices/" + device_id + "/time/sync";
    topics.time_sync_resp = "medicaldisplay/devices/" + device_id + "/time/sync/resp";
    topics.events = "medicaldisplay/devices/" + device_id + "/events";
    return topics;
}

#ifdef HAS_NLOHMANN_JSON
static inline std::string json_dump(const Json& json) {
    return json.dump();
}

static inline Json parse_json(const std::string& payload) {
    if (payload.empty()) {
        return Json::object();
    }
    return Json::parse(payload, nullptr, false);
}
#endif

static std::string escape_json(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (char ch : input) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
                    out += buffer;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

static std::string quote_json(const std::string& value) {
    return "\"" + escape_json(value) + "\"";
}

static std::string build_simple_json(const std::vector<std::pair<std::string, std::string>>& fields,
                                     const std::vector<std::pair<std::string, bool>>& bool_fields = {},
                                     const std::vector<std::pair<std::string, double>>& num_fields = {}) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    auto append_sep = [&]() {
        if (!first) {
            oss << ",";
        }
        first = false;
    };
    for (const auto& [key, value] : fields) {
        append_sep();
        oss << quote_json(key) << ":" << quote_json(value);
    }
    for (const auto& [key, value] : bool_fields) {
        append_sep();
        oss << quote_json(key) << ":" << (value ? "true" : "false");
    }
    for (const auto& [key, value] : num_fields) {
        append_sep();
        oss << quote_json(key) << ":" << value;
    }
    oss << "}";
    return oss.str();
}

static std::string to_hex(const unsigned char* data, size_t size) {
    static const char hex[] = "0123456789abcdef";
    std::string out(size * 2, '0');
    for (size_t i = 0; i < size; ++i) {
        out[i * 2] = hex[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    return out;
}

#ifdef HAS_OPENSSL
static std::string sha256_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};
    std::vector<char> buffer(kIoBufferSize);
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) <= 0) {
        EVP_MD_CTX_free(ctx);
        return {};
    }
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(count));
        }
    }
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);
    return to_hex(digest, digest_len);
}

static std::vector<unsigned char> base64_decode(const std::string& encoded) {
    std::vector<unsigned char> output;
    if (encoded.empty()) {
        return output;
    }
    BIO* bio = BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size()));
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    output.resize(encoded.size());
    const int len = BIO_read(bio, output.data(), static_cast<int>(output.size()));
    if (len > 0) {
        output.resize(static_cast<size_t>(len));
    } else {
        output.clear();
    }
    BIO_free_all(bio);
    return output;
}

static bool verify_signature_file(const std::string& file_path,
                                  const std::string& signature_base64,
                                  const std::string& public_key_path) {
    // [P0-FIX] 空签名或空公钥路径在 enforce_signature=true 时必须拒绝
    if (signature_base64.empty()) {
        fprintf(stderr, "[SECURITY] verify_signature_file: signature is empty, REJECTED\n");
        return false;
    }
    if (public_key_path.empty()) {
        fprintf(stderr, "[SECURITY] verify_signature_file: public_key_path is empty, REJECTED\n");
        return false;
    }
    FILE* key_file = std::fopen(public_key_path.c_str(), "rb");
    if (!key_file) {
        return false;
    }
    EVP_PKEY* pkey = PEM_read_PUBKEY(key_file, nullptr, nullptr, nullptr);
    std::fclose(key_file);
    if (!pkey) {
        return false;
    }

    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
        EVP_PKEY_free(pkey);
        return false;
    }

    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        EVP_PKEY_free(pkey);
        return false;
    }

    bool verified = false;
    if (EVP_DigestVerifyInit(md_ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
        std::vector<char> buffer(kIoBufferSize);
        while (input.good()) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count > 0 && EVP_DigestVerifyUpdate(md_ctx, buffer.data(), static_cast<size_t>(count)) != 1) {
                input.setstate(std::ios::failbit);
                break;
            }
        }
        if (!input.bad()) {
            const auto signature = base64_decode(signature_base64);
            if (!signature.empty()) {
                verified = EVP_DigestVerifyFinal(md_ctx,
                                                 signature.data(),
                                                 signature.size()) == 1;
            }
        }
    }

    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(pkey);
    return verified;
}
#endif

#ifdef HAS_CURL
struct CurlSink {
    FILE* file = nullptr;
    UpdateProgressCallback callback = nullptr;
    void* callback_userdata = nullptr;
    size_t offset = 0;
};

size_t curl_write_to_file(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* sink = static_cast<CurlSink*>(userdata);
    if (!sink || !sink->file) {
        return 0;
    }
    const size_t written = std::fwrite(ptr, size, nmemb, sink->file);
    return written * size;
}

int curl_progress_callback(void* clientp,
                           curl_off_t dltotal,
                           curl_off_t dlnow,
                           curl_off_t,
                           curl_off_t) {
    auto* sink = static_cast<CurlSink*>(clientp);
    if (sink && sink->callback) {
        sink->callback(static_cast<size_t>(sink->offset + dlnow),
                       static_cast<size_t>(sink->offset + dltotal),
                       sink->callback_userdata);
    }
    return 0;
}
#endif

static void emit_event(CloudAgent* agent, CloudEventType type, void* data = nullptr) {
    CloudEventCallback callback = nullptr;
    void* userdata = nullptr;
    {
        std::lock_guard<std::mutex> lock(agent->mutex);
        callback = agent->event_callback;
        userdata = agent->event_userdata;
    }
    if (callback) {
        callback(type, data, userdata);
    }
}

static std::string get_ack_topic_for_request(CloudAgent* agent, const std::string& topic) {
    if (topic == agent->topics.register_req) return agent->topics.register_resp;
    if (topic == agent->topics.ota_check_req) return agent->topics.ota_check_resp;
    if (topic == agent->topics.model_check_req) return agent->topics.model_check_resp;
    if (topic == agent->topics.qc_guidance_req) return agent->topics.qc_guidance_resp;
    if (topic == agent->topics.time_sync_req) return agent->topics.time_sync_resp;
    if (topic == agent->topics.federated_model_req) return agent->topics.federated_model_resp;
    return topic + "/ack";
}

static bool wait_for_ack(CloudAgent* agent,
                         const std::string& ack_topic,
                         std::string* payload,
                         uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(agent->mutex);
    const auto ready = agent->ack_cv.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms),
        [&]() { return agent->ack_payloads.find(ack_topic) != agent->ack_payloads.end() || agent->stop_threads.load(); });
    if (!ready || agent->stop_threads.load()) {
        return false;
    }
    const auto it = agent->ack_payloads.find(ack_topic);
    if (it == agent->ack_payloads.end()) {
        return false;
    }
    if (payload) {
        *payload = it->second;
    }
    agent->ack_payloads.erase(it);
    return true;
}

static bool publish_message(CloudAgent* agent,
                            const std::string& topic,
                            const std::string& payload,
                            int qos = 1,
                            bool retain = false) {
    if (!agent) {
        return false;
    }
#ifdef HAS_MQTT
    std::lock_guard<std::mutex> publish_lock(agent->mqtt_publish_mutex);
    if (!agent->mqtt) {
        return false;
    }
    const int rc = mosquitto_publish(agent->mqtt,
                                     nullptr,
                                     topic.c_str(),
                                     static_cast<int>(payload.size()),
                                     payload.data(),
                                     qos,
                                     retain);
    return rc == MOSQ_ERR_SUCCESS;
#else
    (void)topic;
    (void)payload;
    (void)qos;
    (void)retain;
    return true;
#endif
}

static std::string make_auth_token(const CloudAgentConfig& config) {
    return config.api_key[0] ? config.api_key : "anonymous";
}

static std::string serialize_device_registration(const CloudAgentConfig& config,
                                                 const DeviceInfo& device_info) {
#ifdef HAS_NLOHMANN_JSON
    Json json = {
        {"device_id", device_info.device_id},
        {"model", device_info.model},
        {"firmware_version", device_info.firmware_version},
        {"hardware_version", device_info.hardware_version},
        {"hospital_id", device_info.hospital_id},
        {"department", device_info.department},
        {"latitude", device_info.latitude},
        {"longitude", device_info.longitude},
        {"sdk", "medicaldisplay"},
        {"auth", make_auth_token(config)},
        {"timestamp", iso8601_now()}
    };
    return json_dump(json);
#else
    return build_simple_json(
        {
            {"device_id", device_info.device_id},
            {"model", device_info.model},
            {"firmware_version", device_info.firmware_version},
            {"hardware_version", device_info.hardware_version},
            {"hospital_id", device_info.hospital_id},
            {"department", device_info.department},
            {"auth", make_auth_token(config)},
            {"timestamp", iso8601_now()}
        },
        {},
        {
            {"latitude", device_info.latitude},
            {"longitude", device_info.longitude}
        });
#endif
}

static std::string serialize_heartbeat(const CloudAgent* agent) {
#ifdef HAS_NLOHMANN_JSON
    Json json = {
        {"device_id", agent->device_info.device_id},
        {"registered", agent->registered.load()},
        {"state", static_cast<int>(agent->state.load())},
        {"firmware_version", agent->device_info.firmware_version},
        {"timestamp", iso8601_now()}
    };
    return json_dump(json);
#else
    return build_simple_json(
        {
            {"device_id", agent->device_info.device_id},
            {"firmware_version", agent->device_info.firmware_version},
            {"timestamp", iso8601_now()}
        },
        {
            {"registered", agent->registered.load()}
        },
        {
            {"state", static_cast<double>(agent->state.load())}
        });
#endif
}

static std::string serialize_telemetry(const CloudAgent* agent, const CloudTelemetry& telemetry) {
#ifdef HAS_NLOHMANN_JSON
    Json json = {
        {"device_id", agent->device_info.device_id},
        {"timestamp", telemetry.timestamp[0] ? telemetry.timestamp : iso8601_now()},
        {"hours_used", telemetry.hours_used},
        {"ai_inference_latency_ms", telemetry.ai_inference_latency_ms},
        {"avg_fps", telemetry.avg_fps},
        {"cpu_usage_percent", telemetry.cpu_usage_percent},
        {"memory_usage_mb", telemetry.memory_usage_mb},
        {"gpu_temperature_c", telemetry.gpu_temperature_c},
        {"network_tx_bytes", telemetry.network_tx_bytes},
        {"network_rx_bytes", telemetry.network_rx_bytes},
        {"error_count", telemetry.error_count}
    };
    return json_dump(json);
#else
    return build_simple_json(
        {
            {"device_id", agent->device_info.device_id},
            {"timestamp", telemetry.timestamp[0] ? telemetry.timestamp : iso8601_now()}
        },
        {},
        {
            {"hours_used", telemetry.hours_used},
            {"ai_inference_latency_ms", telemetry.ai_inference_latency_ms},
            {"avg_fps", telemetry.avg_fps},
            {"cpu_usage_percent", telemetry.cpu_usage_percent},
            {"memory_usage_mb", telemetry.memory_usage_mb},
            {"gpu_temperature_c", telemetry.gpu_temperature_c},
            {"network_tx_bytes", static_cast<double>(telemetry.network_tx_bytes)},
            {"network_rx_bytes", static_cast<double>(telemetry.network_rx_bytes)},
            {"error_count", static_cast<double>(telemetry.error_count)}
        });
#endif
}

static std::string serialize_telemetry_batch(CloudAgent* agent,
                                             const std::vector<CloudTelemetry>& batch) {
#ifdef HAS_NLOHMANN_JSON
    Json array = Json::array();
    for (const auto& telemetry : batch) {
        array.push_back(parse_json(serialize_telemetry(agent, telemetry)));
    }
    Json json = {
        {"device_id", agent->device_info.device_id},
        {"batch_size", batch.size()},
        {"records", array}
    };
    return json_dump(json);
#else
    std::ostringstream oss;
    oss << "{\"device_id\":" << quote_json(agent->device_info.device_id)
        << ",\"batch_size\":" << batch.size()
        << ",\"records\":[";
    for (size_t i = 0; i < batch.size(); ++i) {
        if (i != 0) {
            oss << ",";
        }
        oss << serialize_telemetry(agent, batch[i]);
    }
    oss << "]}";
    return oss.str();
#endif
}

static std::string serialize_qc_report(const CloudAgent* agent, const QCReport& report) {
#ifdef HAS_NLOHMANN_JSON
    Json json = {
        {"device_id", report.device_id[0] ? report.device_id : agent->device_info.device_id},
        {"report_id", report.report_id},
        {"timestamp", report.timestamp},
        {"delta_e", report.delta_e},
        {"luminance", report.luminance},
        {"backlight_hours", report.backlight_hours},
        {"temperature", report.temperature},
        {"health_score", report.health_score},
        {"recommendations", report.recommendations}
    };
    return json_dump(json);
#else
    return build_simple_json(
        {
            {"device_id", report.device_id[0] ? report.device_id : agent->device_info.device_id},
            {"report_id", report.report_id},
            {"timestamp", report.timestamp},
            {"recommendations", report.recommendations}
        },
        {},
        {
            {"delta_e", report.delta_e},
            {"luminance", report.luminance},
            {"backlight_hours", report.backlight_hours},
            {"temperature", report.temperature},
            {"health_score", report.health_score}
        });
#endif
}

static std::string serialize_inference_stats(const CloudAgent* agent, const InferenceStats& stats) {
#ifdef HAS_NLOHMANN_JSON
    Json modalities = Json::array();
    for (uint64_t count : stats.modality_counts) {
        modalities.push_back(count);
    }
    Json json = {
        {"device_id", agent->device_info.device_id},
        {"total_inferences", stats.total_inferences},
        {"successful_inferences", stats.successful_inferences},
        {"avg_latency_ms", stats.avg_latency_ms},
        {"p50_latency_ms", stats.p50_latency_ms},
        {"p95_latency_ms", stats.p95_latency_ms},
        {"p99_latency_ms", stats.p99_latency_ms},
        {"modality_counts", modalities},
        {"timestamp", iso8601_now()}
    };
    return json_dump(json);
#else
    std::ostringstream modality_json;
    modality_json << "[";
    for (size_t i = 0; i < sizeof(stats.modality_counts) / sizeof(stats.modality_counts[0]); ++i) {
        if (i != 0) {
            modality_json << ",";
        }
        modality_json << stats.modality_counts[i];
    }
    modality_json << "]";
    std::ostringstream oss;
    oss << "{"
        << "\"device_id\":" << quote_json(agent->device_info.device_id)
        << ",\"total_inferences\":" << stats.total_inferences
        << ",\"successful_inferences\":" << stats.successful_inferences
        << ",\"avg_latency_ms\":" << stats.avg_latency_ms
        << ",\"p50_latency_ms\":" << stats.p50_latency_ms
        << ",\"p95_latency_ms\":" << stats.p95_latency_ms
        << ",\"p99_latency_ms\":" << stats.p99_latency_ms
        << ",\"modality_counts\":" << modality_json.str()
        << ",\"timestamp\":" << quote_json(iso8601_now())
        << "}";
    return oss.str();
#endif
}

static std::string bytes_to_hex(const uint8_t* data, size_t size) {
    return data ? to_hex(data, size) : std::string();
}

static bool copy_file_binary(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) {
        return false;
    }
    ensure_directory(dirname_of(dst));
    std::ofstream out(dst, std::ios::binary);
    if (!out) {
        return false;
    }
    out << in.rdbuf();
    return in.good() || in.eof();
}

static std::string deduce_install_target(const std::string& configured, const std::string& download_url) {
    if (!configured.empty()) {
        return configured;
    }
    return base_name_from_url(download_url);
}

static bool apply_delta_file(const std::string& base_path,
                             const std::string& delta_path,
                             const std::string& output_path) {
    std::ifstream base(base_path, std::ios::binary);
    std::ifstream delta(delta_path, std::ios::binary);
    if (!base || !delta) {
        return false;
    }
    ensure_directory(dirname_of(output_path));
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << base.rdbuf();
    out << delta.rdbuf();
    return out.good();
}

static void record_ack(CloudAgent* agent, const std::string& topic, const std::string& payload) {
    {
        std::lock_guard<std::mutex> lock(agent->mutex);
        agent->ack_payloads[topic] = payload;
    }
    agent->ack_cv.notify_all();
}

static bool string_contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

static void parse_mqtt_endpoint(const CloudAgentConfig& config, std::string* host, int* port) {
    if (!host || !port) {
        return;
    }
    *host = "localhost";
    *port = 1883;

    std::string endpoint = normalize_server(config);
    if (endpoint.empty()) {
        return;
    }
    const auto scheme_pos = endpoint.find("://");
    if (scheme_pos != std::string::npos) {
        endpoint = endpoint.substr(scheme_pos + 3);
    }
    const auto slash = endpoint.find('/');
    if (slash != std::string::npos) {
        endpoint = endpoint.substr(0, slash);
    }
    if (endpoint.empty()) {
        return;
    }

    if (endpoint.front() == '[') {
        const auto close = endpoint.find(']');
        if (close != std::string::npos) {
            *host = endpoint.substr(1, close - 1);
            if (close + 2 < endpoint.size() && endpoint[close + 1] == ':') {
                const int parsed_port = std::atoi(endpoint.substr(close + 2).c_str());
                if (parsed_port > 0) {
                    *port = parsed_port;
                }
            }
            return;
        }
    }

    const auto first_colon = endpoint.find(':');
    const auto last_colon = endpoint.rfind(':');
    if (first_colon != std::string::npos && first_colon == last_colon) {
        const int parsed_port = std::atoi(endpoint.substr(last_colon + 1).c_str());
        *host = endpoint.substr(0, last_colon);
        if (parsed_port > 0) {
            *port = parsed_port;
        }
    } else {
        *host = endpoint;
    }

    if (host->empty()) {
        *host = "localhost";
    }
}

static int parse_update_info(const std::string& payload, UpdateInfo* update_info) {
    if (!update_info) {
        return 0;
    }
    std::memset(update_info, 0, sizeof(*update_info));
#ifdef HAS_NLOHMANN_JSON
    Json json = parse_json(payload);
    if (!json.is_object()) {
        return -1;
    }
    copy_cstr(update_info->version, sizeof(update_info->version), json.value("version", ""));
    copy_cstr(update_info->release_date, sizeof(update_info->release_date), json.value("release_date", ""));
    update_info->full_size = json.value("full_size", static_cast<size_t>(0));
    update_info->delta_size = json.value("delta_size", static_cast<size_t>(0));
    copy_cstr(update_info->checksum, sizeof(update_info->checksum), json.value("checksum", ""));
    copy_cstr(update_info->signature, sizeof(update_info->signature), json.value("signature", ""));
    copy_cstr(update_info->download_url, sizeof(update_info->download_url), json.value("download_url", ""));
    copy_cstr(update_info->delta_url, sizeof(update_info->delta_url), json.value("delta_url", ""));
    copy_cstr(update_info->base_version, sizeof(update_info->base_version), json.value("base_version", ""));
    copy_cstr(update_info->install_path, sizeof(update_info->install_path), json.value("install_path", ""));
    copy_cstr(update_info->changelog, sizeof(update_info->changelog), json.value("changelog", ""));
    update_info->is_mandatory = json.value("is_mandatory", false);
    update_info->is_security_update = json.value("is_security_update", false);
    return json.value("update_available", true) ? 0 : 1;
#else
    if (payload.empty() || string_contains(payload, "\"update_available\":false")) {
        return 1;
    }
    return 0;
#endif
}

static int parse_model_info(const std::string& payload, CloudModelInfo* model_info) {
    if (!model_info) {
        return 0;
    }
    std::memset(model_info, 0, sizeof(*model_info));
#ifdef HAS_NLOHMANN_JSON
    Json json = parse_json(payload);
    if (!json.is_object()) {
        return -1;
    }
    copy_cstr(model_info->model_id, sizeof(model_info->model_id), json.value("model_id", ""));
    copy_cstr(model_info->version, sizeof(model_info->version), json.value("version", ""));
    copy_cstr(model_info->base_version, sizeof(model_info->base_version), json.value("base_version", ""));
    model_info->full_size = json.value("full_size", static_cast<size_t>(0));
    model_info->delta_size = json.value("delta_size", static_cast<size_t>(0));
    copy_cstr(model_info->checksum, sizeof(model_info->checksum), json.value("checksum", ""));
    copy_cstr(model_info->signature, sizeof(model_info->signature), json.value("signature", ""));
    copy_cstr(model_info->download_url, sizeof(model_info->download_url), json.value("download_url", ""));
    copy_cstr(model_info->delta_url, sizeof(model_info->delta_url), json.value("delta_url", ""));
    copy_cstr(model_info->install_path, sizeof(model_info->install_path), json.value("install_path", ""));
    copy_cstr(model_info->release_notes, sizeof(model_info->release_notes), json.value("release_notes", ""));
    model_info->is_mandatory = json.value("is_mandatory", false);
    return json.value("update_available", true) ? 0 : 1;
#else
    if (payload.empty() || string_contains(payload, "\"update_available\":false")) {
        return 1;
    }
    return 0;
#endif
}

static int parse_command_message(const std::string& payload, CloudCommand* command) {
    if (!command) {
        return -1;
    }
    std::memset(command, 0, sizeof(*command));
#ifdef HAS_NLOHMANN_JSON
    Json json = parse_json(payload);
    if (!json.is_object()) {
        return -1;
    }
    copy_cstr(command->command_id, sizeof(command->command_id), json.value("command_id", ""));
    copy_cstr(command->command_type, sizeof(command->command_type), json.value("command_type", ""));
    copy_cstr(command->correlation_id, sizeof(command->correlation_id), json.value("correlation_id", ""));
    if (json.contains("payload")) {
        if (json["payload"].is_string()) {
            copy_cstr(command->payload, sizeof(command->payload), json["payload"].get<std::string>());
        } else {
            copy_cstr(command->payload, sizeof(command->payload), json["payload"].dump());
        }
    }
    copy_cstr(command->received_at, sizeof(command->received_at), json.value("received_at", iso8601_now()));
    command->requires_response = json.value("requires_response", true);
    return 0;
#else
    copy_cstr(command->payload, sizeof(command->payload), payload);
    copy_cstr(command->received_at, sizeof(command->received_at), iso8601_now());
    command->requires_response = true;
    return 0;
#endif
}

#ifdef HAS_MQTT
static void subscribe_topics(CloudAgent* agent) {
    if (!agent || !agent->mqtt) {
        return;
    }
    mosquitto_subscribe(agent->mqtt, nullptr, agent->topics.register_resp.c_str(), 1);
    mosquitto_subscribe(agent->mqtt, nullptr, agent->topics.ota_check_resp.c_str(), 1);
    mosquitto_subscribe(agent->mqtt, nullptr, agent->topics.model_check_resp.c_str(), 1);
    mosquitto_subscribe(agent->mqtt, nullptr, agent->topics.command_req.c_str(), 1);
    mosquitto_subscribe(agent->mqtt, nullptr, agent->topics.qc_guidance_resp.c_str(), 1);
    mosquitto_subscribe(agent->mqtt, nullptr, agent->topics.time_sync_resp.c_str(), 1);
    mosquitto_subscribe(agent->mqtt, nullptr, agent->topics.federated_model_resp.c_str(), 1);
}

static void mqtt_on_connect(struct mosquitto*, void* userdata, int rc) {
    auto* agent = static_cast<CloudAgent*>(userdata);
    if (!agent) {
        return;
    }
    if (rc == 0) {
        agent->connected.store(true);
        agent->state.store(CLOUD_STATE_CONNECTED);
        subscribe_topics(agent);
        emit_event(agent, CLOUD_EVENT_CONNECTED);
    } else {
        agent->connected.store(false);
        agent->state.store(CLOUD_STATE_ERROR);
        emit_event(agent, CLOUD_EVENT_ERROR);
    }
}

static void mqtt_on_disconnect(struct mosquitto*, void* userdata, int) {
    auto* agent = static_cast<CloudAgent*>(userdata);
    if (!agent) {
        return;
    }
    agent->connected.store(false);
    if (!agent->stop_threads.load()) {
        agent->state.store(CLOUD_STATE_CONNECTING);
    } else {
        agent->state.store(CLOUD_STATE_DISCONNECTED);
    }
    emit_event(agent, CLOUD_EVENT_DISCONNECTED);
}

static void mqtt_on_message(struct mosquitto*, void* userdata, const mosquitto_message* message) {
    auto* agent = static_cast<CloudAgent*>(userdata);
    if (!agent || !message || !message->topic) {
        return;
    }
    const std::string topic(message->topic);
    const std::string payload(
        message->payload ? static_cast<const char*>(message->payload) : "",
        message->payloadlen > 0 ? static_cast<size_t>(message->payloadlen) : 0);

    if (topic == agent->topics.command_req) {
        CloudCommand command {};
        if (parse_command_message(payload, &command) == 0) {
            {
                std::lock_guard<std::mutex> lock(agent->mutex);
                agent->command_queue.push_back(command);
            }
            agent->command_cv.notify_one();
        }
        return;
    }

    record_ack(agent, topic, payload);
}
#endif

static void heartbeat_loop(CloudAgent* agent) {
    const int interval = std::max(1, agent->config.heartbeat_interval_sec > 0
                                     ? agent->config.heartbeat_interval_sec
                                     : kDefaultHeartbeatSeconds);
    while (!agent->stop_threads.load()) {
        if (agent->connected.load() && agent->registered.load()) {
            publish_message(agent, agent->topics.heartbeat, serialize_heartbeat(agent), 1, false);
        }
        for (int i = 0; i < interval && !agent->stop_threads.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

static void telemetry_loop(CloudAgent* agent) {
    const int interval = std::max(1, agent->config.stats_report_interval_sec > 0
                                     ? agent->config.stats_report_interval_sec
                                     : kDefaultHeartbeatSeconds);
    const int batch_size = std::max(1, agent->config.telemetry_batch_size > 0
                                       ? agent->config.telemetry_batch_size
                                       : kDefaultTelemetryBatchSize);
    while (!agent->stop_threads.load()) {
        std::vector<CloudTelemetry> batch;
        {
            std::lock_guard<std::mutex> lock(agent->mutex);
            while (!agent->telemetry_queue.empty() && static_cast<int>(batch.size()) < batch_size) {
                batch.push_back(agent->telemetry_queue.front());
                agent->telemetry_queue.pop_front();
            }
        }
        if (!batch.empty() && agent->connected.load() && agent->registered.load()) {
            publish_message(agent, batch.size() == 1 ? agent->topics.telemetry : agent->topics.telemetry_batch,
                            batch.size() == 1 ? serialize_telemetry(agent, batch.front())
                                              : serialize_telemetry_batch(agent, batch),
                            1,
                            false);
        } else if (!batch.empty()) {
            std::lock_guard<std::mutex> lock(agent->mutex);
            for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
                agent->telemetry_queue.push_front(*it);
            }
        }
        for (int i = 0; i < interval && !agent->stop_threads.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

static bool prepare_staging(CloudAgent* agent) {
    if (agent->staging_dir.empty()) {
        if (agent->config.staging_dir[0]) {
            agent->staging_dir = agent->config.staging_dir;
        } else {
            agent->staging_dir = ".cloud_agent";
        }
    }
    return ensure_directory(agent->staging_dir);
}

static std::string rollback_marker_path(CloudAgent* agent, const std::string& name) {
    prepare_staging(agent);
    return join_path(agent->staging_dir, name + ".rollback");
}

// [P0-FIX] verify_download: agent!=nullptr 时强制要求签名，防止旁路攻击
static bool verify_download(CloudAgent* agent,
                            const std::string& file_path,
                            const char* checksum,
                            const char* signature,
                            const std::string& public_key_path) {
#ifdef HAS_OPENSSL
    // Step 1: SHA256 checksum 校验（可选）
    if (checksum && checksum[0]) {
        const std::string actual = sha256_file(file_path);
        if (actual.empty() || actual != checksum) {
            fprintf(stderr, "[SECURITY] verify_download: checksum mismatch for %s\n", file_path.c_str());
            return false;
        }
    }

    // Step 2: 签名校验（enforce_signature=true 时强制）
    const bool enforce = !agent || agent->config.enforce_signature;
    if (enforce) {
        // 强制验签：signature 或 public_key 缺失 → 拒绝
        if (!signature || !signature[0]) {
            fprintf(stderr, "[SECURITY] verify_download: MISSING signature, REJECTED (enforce=true)\n");
            return false;
        }
        if (public_key_path.empty()) {
            fprintf(stderr, "[SECURITY] verify_download: MISSING public_key_path, REJECTED (enforce=true)\n");
            return false;
        }
        if (!verify_signature_file(file_path, signature, public_key_path)) {
            fprintf(stderr, "[SECURITY] verify_download: signature verification FAILED for %s\n", file_path.c_str());
            return false;
        }
    } else {
        // 非强制模式：仅在提供了签名时才验签
        if (signature && signature[0]) {
            if (!verify_signature_file(file_path, signature, public_key_path)) {
                fprintf(stderr, "[SECURITY] verify_download: signature verification FAILED for %s\n", file_path.c_str());
                return false;
            }
        }
    }
    return true;
#else
    // 没有 OpenSSL：enforce=true 时拒绝
    const bool enforce = !agent || agent->config.enforce_signature;
    if (enforce) {
        fprintf(stderr, "[SECURITY] verify_download: OpenSSL not available, REJECTED (enforce=true)\n");
        return false;
    }
    (void)file_path;
    (void)checksum;
    (void)signature;
    (void)public_key_path;
    return true;
#endif
}

static int download_file_with_resume(CloudAgent* agent,
                                     const std::string& url,
                                     const std::string& target_path,
                                     UpdateProgressCallback callback,
                                     void* progress_userdata) {
    if (url.empty() || target_path.empty()) {
        return -1;
    }
    ensure_directory(dirname_of(target_path));
    const size_t resume_offset = file_exists_cpp(target_path) ? file_size_cpp(target_path) : 0;

#ifdef HAS_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        return -1;
    }
    FILE* file = std::fopen(target_path.c_str(), resume_offset > 0 ? "ab" : "wb");
    if (!file) {
        curl_easy_cleanup(curl);
        return -1;
    }

    CurlSink sink;
    sink.file = file;
    sink.callback = callback;
    sink.callback_userdata = progress_userdata;
    sink.offset = resume_offset;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 16L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &sink);
    if (resume_offset > 0) {
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resume_offset));
    }

    struct curl_slist* headers = nullptr;
    const std::string auth = "Authorization: Bearer " + make_auth_token(agent->config);
    headers = curl_slist_append(headers, auth.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    const CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    std::fclose(file);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK ? 0 : -1;
#else
    std::ofstream output(target_path, resume_offset > 0 ? (std::ios::binary | std::ios::app) : std::ios::binary);
    if (!output) {
        return -1;
    }
    output << "stub-download-from:" << url << "\n";
    output << "timestamp:" << iso8601_now() << "\n";
    if (callback) {
        const size_t size = static_cast<size_t>(output.tellp());
        callback(size, size, progress_userdata);
    }
    return output.good() ? 0 : -1;
#endif
}

static std::string default_model_download_path(CloudAgent* agent, const CloudModelInfo& model_info) {
    prepare_staging(agent);
    const std::string filename = !safe_string(model_info.install_path).empty()
                                   ? base_name_from_url(model_info.install_path)
                                   : base_name_from_url(model_info.download_url);
    return join_path(agent->staging_dir, filename.empty() ? "model.bin" : filename);
}

static std::string default_update_download_path(CloudAgent* agent, const UpdateInfo& update_info) {
    prepare_staging(agent);
    const std::string filename = base_name_from_url(update_info.download_url);
    return join_path(agent->staging_dir, filename.empty() ? "update.pkg" : filename);
}

static std::string default_delta_download_path(CloudAgent* agent, const std::string& url, const std::string& fallback) {
    prepare_staging(agent);
    const std::string filename = !url.empty() ? base_name_from_url(url) : fallback;
    return join_path(agent->staging_dir, filename);
}

static int publish_and_wait(CloudAgent* agent,
                            const std::string& request_topic,
                            const std::string& payload,
                            std::string* response,
                            uint32_t timeout_ms) {
    const std::string ack_topic = get_ack_topic_for_request(agent, request_topic);
    if (!publish_message(agent, request_topic, payload, 1, false)) {
        return -1;
    }
#ifndef HAS_MQTT
    (void)timeout_ms;
    if (response) {
        if (request_topic == agent->topics.register_req) {
            *response = "{\"status\":\"ok\"}";
        } else if (request_topic == agent->topics.ota_check_req) {
            *response = "{\"update_available\":false}";
        } else if (request_topic == agent->topics.model_check_req) {
            *response = "{\"update_available\":false}";
        } else if (request_topic == agent->topics.qc_guidance_req) {
            *response = "{\"guidance\":\"MQTT disabled; using local fallback guidance.\"}";
        } else if (request_topic == agent->topics.time_sync_req) {
            *response = "{\"server_ts_ms\":0}";
        } else if (request_topic == agent->topics.federated_model_req) {
            *response = "{\"model_blob\":\"local-fallback-model\"}";
        } else {
            *response = "{\"status\":\"queued\"}";
        }
    }
    return 0;
#else
    return wait_for_ack(agent, ack_topic, response, timeout_ms) ? 0 : -1;
#endif
}

}  // namespace

extern "C" {

CloudAgent* cloud_agent_create(const CloudAgentConfig* config, const DeviceInfo* device_info) {
    if (!config) {
        return nullptr;
    }

    auto* agent = new CloudAgent();
    agent->config = *config;
    // [P0-FIX] 医疗安全强制：enforce_signature 默认为 true（无法关闭）
    // 这是医疗设备安全的必要条件
    agent->config.enforce_signature = true;
    if (device_info) {
        agent->device_info = *device_info;
    }
    agent->topics = make_topics(agent->device_info);
    agent->staging_dir = agent->config.staging_dir[0] ? agent->config.staging_dir : ".cloud_agent";
    prepare_staging(agent);

#ifdef HAS_MQTT
    mosquitto_lib_init();
    const std::string client_id = agent->device_info.device_id[0]
                                    ? std::string("medicaldisplay-") + agent->device_info.device_id
                                    : std::string("medicaldisplay-agent");
    agent->mqtt = mosquitto_new(client_id.c_str(), true, agent);
    if (!agent->mqtt) {
        delete agent;
        mosquitto_lib_cleanup();
        return nullptr;
    }
    mosquitto_connect_callback_set(agent->mqtt, mqtt_on_connect);
    mosquitto_disconnect_callback_set(agent->mqtt, mqtt_on_disconnect);
    mosquitto_message_callback_set(agent->mqtt, mqtt_on_message);
    if (agent->config.api_key[0]) {
        mosquitto_username_pw_set(agent->mqtt,
                                  agent->device_info.device_id[0] ? agent->device_info.device_id : "device",
                                  agent->config.api_key);
    }
    const int reconnect_sec = std::max(1, agent->config.reconnect_interval_sec > 0
                                          ? agent->config.reconnect_interval_sec
                                          : kDefaultReconnectSeconds);
    mosquitto_reconnect_delay_set(agent->mqtt, reconnect_sec, reconnect_sec * 2, true);
#endif
    return agent;
}

void cloud_agent_destroy(CloudAgent* agent) {
    if (!agent) {
        return;
    }

    cloud_agent_disconnect(agent);

#ifdef HAS_MQTT
    if (agent->mqtt) {
        mosquitto_destroy(agent->mqtt);
        agent->mqtt = nullptr;
        mosquitto_lib_cleanup();
    }
#endif
    delete agent;
}

int cloud_agent_connect(CloudAgent* agent) {
    if (!agent) {
        return -1;
    }
    agent->stop_threads.store(false);
    agent->state.store(CLOUD_STATE_CONNECTING);

#ifdef HAS_MQTT
    std::string host;
    int port = 1883;
    parse_mqtt_endpoint(agent->config, &host, &port);

    if (mosquitto_connect_async(agent->mqtt, host.c_str(), port, 60) != MOSQ_ERR_SUCCESS) {
        agent->state.store(CLOUD_STATE_ERROR);
        return -1;
    }
    if (mosquitto_loop_start(agent->mqtt) != MOSQ_ERR_SUCCESS) {
        mosquitto_disconnect(agent->mqtt);
        agent->state.store(CLOUD_STATE_ERROR);
        return -1;
    }

    if (!agent->heartbeat_thread.joinable()) {
        agent->heartbeat_thread = std::thread(heartbeat_loop, agent);
    }
    if (!agent->telemetry_thread.joinable()) {
        agent->telemetry_thread = std::thread(telemetry_loop, agent);
    }
    return 0;
#else
    agent->state.store(CLOUD_STATE_CONNECTED);
    agent->connected.store(true);
    if (!agent->heartbeat_thread.joinable()) {
        agent->heartbeat_thread = std::thread(heartbeat_loop, agent);
    }
    if (!agent->telemetry_thread.joinable()) {
        agent->telemetry_thread = std::thread(telemetry_loop, agent);
    }
    emit_event(agent, CLOUD_EVENT_CONNECTED);
    return 0;
#endif
}

void cloud_agent_disconnect(CloudAgent* agent) {
    if (!agent) {
        return;
    }
    agent->stop_threads.store(true);
    agent->ack_cv.notify_all();
    agent->command_cv.notify_all();

#ifdef HAS_MQTT
    if (agent->mqtt) {
        mosquitto_disconnect(agent->mqtt);
        mosquitto_loop_stop(agent->mqtt, true);
    }
#endif

    if (agent->heartbeat_thread.joinable()) {
        agent->heartbeat_thread.join();
    }
    if (agent->telemetry_thread.joinable()) {
        agent->telemetry_thread.join();
    }

    agent->connected.store(false);
    agent->registered.store(false);
    agent->state.store(CLOUD_STATE_DISCONNECTED);
}

CloudState cloud_agent_get_state(CloudAgent* agent) {
    if (!agent) {
        return CLOUD_STATE_ERROR;
    }
    return agent->state.load();
}

int cloud_agent_register(CloudAgent* agent, const DeviceInfo* device_info) {
    if (!agent || !device_info) {
        return -1;
    }
    {
        std::lock_guard<std::mutex> lock(agent->mutex);
        agent->device_info = *device_info;
        agent->topics = make_topics(agent->device_info);
    }
#ifdef HAS_MQTT
    if (agent->connected.load()) {
        subscribe_topics(agent);
    }
#endif
    const std::string payload = serialize_device_registration(agent->config, agent->device_info);
    std::string response;
    const int timeout_ms = std::max(1000, agent->config.command_timeout_ms > 0
                                           ? agent->config.command_timeout_ms
                                           : kDefaultCommandTimeoutMs);
    if (publish_and_wait(agent, agent->topics.register_req, payload, &response, timeout_ms) != 0) {
        return -1;
    }
    agent->registered.store(true);
    return 0;
}

int cloud_agent_send_telemetry(CloudAgent* agent, const CloudTelemetry* telemetry) {
    if (!agent || !telemetry) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(agent->mutex);
    agent->telemetry_queue.push_back(*telemetry);
    return 0;
}

int cloud_agent_check_model_update(CloudAgent* agent, CloudModelInfo* model_info) {
    if (!agent) {
        return -1;
    }
    const std::string payload = build_simple_json(
        {
            {"device_id", agent->device_info.device_id},
            {"current_version", agent->previous_model_version},
            {"timestamp", iso8601_now()}
        });
    std::string response;
    const int timeout_ms = std::max(1000, agent->config.command_timeout_ms > 0
                                           ? agent->config.command_timeout_ms
                                           : kDefaultCommandTimeoutMs);
    if (publish_and_wait(agent, agent->topics.model_check_req, payload, &response, timeout_ms) != 0) {
        return -1;
    }
    return parse_model_info(response, model_info);
}

int cloud_agent_download_model(CloudAgent* agent,
                               const CloudModelInfo* model_info,
                               const char* target_path,
                               UpdateProgressCallback callback,
                               void* progress_userdata) {
    if (!agent || !model_info) {
        return -1;
    }
    const std::string install_target = target_path && target_path[0]
                                         ? target_path
                                         : default_model_download_path(agent, *model_info);
    const std::string delta_path = default_delta_download_path(agent, model_info->delta_url, "model.delta");
    std::string downloaded_path = install_target;

    if (model_info->delta_url[0] && model_info->base_version[0]) {
        if (download_file_with_resume(agent, model_info->delta_url, delta_path, callback, progress_userdata) == 0) {
            const std::string base_path = deduce_install_target(model_info->install_path, model_info->download_url);
            if (file_exists_cpp(base_path) && apply_delta_file(base_path, delta_path, install_target)) {
                if (!verify_download(agent, install_target,
                                     model_info->checksum,
                                     model_info->signature,
                                     agent->config.public_key_path)) {
                    return -1;
                }
                agent->previous_model_version = model_info->version;
                return 0;
            }
        }
    }

    if (download_file_with_resume(agent, model_info->download_url, downloaded_path, callback, progress_userdata) != 0) {
        return -1;
    }
    if (!verify_download(agent, downloaded_path,
                         model_info->checksum,
                         model_info->signature,
                         agent->config.public_key_path)) {
        return -1;
    }
    agent->previous_model_version = model_info->version;
    return 0;
}

int cloud_agent_check_update(CloudAgent* agent, UpdateInfo* update_info) {
    if (!agent) {
        return -1;
    }
    const std::string payload = build_simple_json(
        {
            {"device_id", agent->device_info.device_id},
            {"firmware_version", agent->device_info.firmware_version},
            {"hardware_version", agent->device_info.hardware_version},
            {"timestamp", iso8601_now()}
        },
        {
            {"enable_ota", agent->config.enable_ota}
        });
    std::string response;
    const int timeout_ms = std::max(1000, agent->config.command_timeout_ms > 0
                                           ? agent->config.command_timeout_ms
                                           : kDefaultCommandTimeoutMs);
    if (publish_and_wait(agent, agent->topics.ota_check_req, payload, &response, timeout_ms) != 0) {
        return -1;
    }
    const int rc = parse_update_info(response, update_info);
    if (rc == 0 && update_info) {
        emit_event(agent, CLOUD_EVENT_UPDATE_AVAILABLE, update_info);
    }
    return rc;
}

int cloud_agent_download_update(CloudAgent* agent,
                                const UpdateInfo* update,
                                UpdateProgressCallback callback,
                                void* progress_userdata) {
    if (!agent || !update) {
        return -1;
    }
    if (!agent->config.enable_ota) {
        return -1;
    }

    const std::string final_path = default_update_download_path(agent, *update);
    const std::string delta_path = default_delta_download_path(agent, update->delta_url, "update.delta");

    if (update->delta_url[0] && update->base_version[0]) {
        if (download_file_with_resume(agent, update->delta_url, delta_path, callback, progress_userdata) == 0) {
            const std::string current_path = update->install_path[0]
                                               ? update->install_path
                                               : std::string("current_firmware.bin");
            if (file_exists_cpp(current_path) && apply_delta_file(current_path, delta_path, final_path)) {
                if (!verify_download(agent, final_path,
                                     update->checksum,
                                     update->signature,
                                     agent->config.public_key_path)) {
                    emit_event(agent, CLOUD_EVENT_UPDATE_FAILED);
                    return -1;
                }
                agent->last_update_download_path = final_path;
                emit_event(agent, CLOUD_EVENT_UPDATE_DOWNLOADED, const_cast<UpdateInfo*>(update));
                return 0;
            }
        }
    }

    if (download_file_with_resume(agent, update->download_url, final_path, callback, progress_userdata) != 0) {
        emit_event(agent, CLOUD_EVENT_UPDATE_FAILED);
        return -1;
    }
    if (!verify_download(agent, final_path,
                         update->checksum,
                         update->signature,
                         agent->config.public_key_path)) {
        emit_event(agent, CLOUD_EVENT_UPDATE_FAILED);
        return -1;
    }

    agent->last_update_download_path = final_path;
    emit_event(agent, CLOUD_EVENT_UPDATE_DOWNLOADED, const_cast<UpdateInfo*>(update));
    return 0;
}

// [P1-FIX] cloud_agent_apply_update: 双 bank 固件更新 + atomic rename + fsync
int cloud_agent_apply_update(CloudAgent* agent, const UpdateInfo* update, bool verify_before_apply) {
    if (!agent || !update) {
        return -1;
    }
    if (agent->last_update_download_path.empty() || !file_exists_cpp(agent->last_update_download_path)) {
        return -1;
    }

    if (verify_before_apply &&
        !verify_download(agent, agent->last_update_download_path,
                         update->checksum,
                         update->signature,
                         agent->config.public_key_path)) {
        emit_event(agent, CLOUD_EVENT_UPDATE_FAILED);
        return -1;
    }

    const std::string target_path = deduce_install_target(update->install_path, update->download_url);
    const std::string rollback_path = rollback_marker_path(agent, "firmware");
    
    // Step 1: 保存当前版本到 rollback
    if (file_exists_cpp(target_path)) {
        copy_file_binary(target_path, rollback_path);
    }

    // Step 2: 双 bank 策略
    const std::string bank_a = target_path + ".bank_a";
    const std::string bank_b = target_path + ".bank_b";
    std::string inactive_bank;
    
    // 选择非活跃的 bank
    if (!file_exists_cpp(bank_a)) {
        inactive_bank = bank_a;
    } else if (!file_exists_cpp(bank_b)) {
        inactive_bank = bank_b;
    } else {
        // 两个 bank 都存在，使用较旧的
        inactive_bank = (file_size_cpp(bank_a) < file_size_cpp(bank_b)) ? bank_a : bank_b;
    }

    // Step 3: 写入新固件到 inactive bank
    if (!copy_file_binary(agent->last_update_download_path, inactive_bank)) {
        emit_event(agent, CLOUD_EVENT_UPDATE_FAILED);
        return -1;
    }

    // Step 4: fsync 确保数据落盘
#ifdef PLATFORM_LINUX
    int fd = open(inactive_bank.c_str(), O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
#endif

    // Step 5: Atomic rename
    if (file_exists_cpp(target_path)) {
        // 将当前固件移到 backup
        const std::string backup_path = target_path + ".backup";
        std::rename(target_path.c_str(), backup_path.c_str());
#ifdef PLATFORM_LINUX
        int dir_fd = open(dirname_of(target_path).c_str(), O_RDONLY);
        if (dir_fd >= 0) {
            fsync(dir_fd);
            close(dir_fd);
        }
#endif
    }
    
    // atomic rename 新固件
    if (std::rename(inactive_bank.c_str(), target_path.c_str()) != 0) {
        // rename 失败，emit event
        emit_event(agent, CLOUD_EVENT_UPDATE_FAILED);
        return -1;
    }
    
    // 确保目录元数据同步
#ifdef PLATFORM_LINUX
    int dir_fd = open(dirname_of(target_path).c_str(), O_RDONLY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }
#endif

    agent->previous_firmware_version = agent->device_info.firmware_version;
    agent->last_update_target_path = target_path;
    copy_cstr(agent->device_info.firmware_version, sizeof(agent->device_info.firmware_version), update->version);
    emit_event(agent, CLOUD_EVENT_UPDATE_READY, const_cast<UpdateInfo*>(update));
    return 0;
}

int cloud_agent_rollback(CloudAgent* agent) {
    if (!agent) {
        return -1;
    }
    const std::string rollback_path = rollback_marker_path(agent, "firmware");
    if (!file_exists_cpp(rollback_path)) {
        return -1;
    }
    const std::string target_path = !agent->last_update_target_path.empty()
                                      ? agent->last_update_target_path
                                      : std::string("current_firmware.bin");
    if (!copy_file_binary(rollback_path, target_path)) {
        return -1;
    }
    if (!agent->previous_firmware_version.empty()) {
        copy_cstr(agent->device_info.firmware_version,
                  sizeof(agent->device_info.firmware_version),
                  agent->previous_firmware_version);
    }
    return 0;
}

int cloud_agent_receive_command(CloudAgent* agent, CloudCommand* command, uint32_t timeout_ms) {
    if (!agent || !command) {
        return -1;
    }
    std::unique_lock<std::mutex> lock(agent->mutex);
    const auto ready = agent->command_cv.wait_for(
        lock,
        std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : static_cast<uint32_t>(kDefaultCommandTimeoutMs)),
        [&]() { return !agent->command_queue.empty() || agent->stop_threads.load(); });
    if (!ready) {
        return 1;
    }
    if (agent->stop_threads.load()) {
        return -1;
    }
    *command = agent->command_queue.front();
    agent->command_queue.pop_front();
    return 0;
}

int cloud_agent_send_response(CloudAgent* agent, const CloudCommandResponse* response) {
    if (!agent || !response) {
        return -1;
    }
#ifdef HAS_NLOHMANN_JSON
    Json json = {
        {"device_id", agent->device_info.device_id},
        {"command_id", response->command_id},
        {"status_code", response->status_code},
        {"message", response->message},
        {"payload", response->payload},
        {"timestamp", iso8601_now()}
    };
    return publish_message(agent, agent->topics.command_resp, json_dump(json), 1, false) ? 0 : -1;
#else
    const std::string payload = build_simple_json(
        {
            {"device_id", agent->device_info.device_id},
            {"command_id", response->command_id},
            {"message", response->message},
            {"payload", response->payload},
            {"timestamp", iso8601_now()}
        },
        {},
        {
            {"status_code", static_cast<double>(response->status_code)}
        });
    return publish_message(agent, agent->topics.command_resp, payload, 1, false) ? 0 : -1;
#endif
}

int cloud_agent_report_qc(CloudAgent* agent, const QCReport* report) {
    if (!agent || !report) {
        return -1;
    }
    return publish_message(agent, agent->topics.qc_report, serialize_qc_report(agent, *report), 1, false) ? 0 : -1;
}

int cloud_agent_get_recommendations(CloudAgent* agent,
                                    const char* device_id,
                                    char* recommendations,
                                    size_t buffer_size) {
    if (!agent || !device_id || !recommendations || buffer_size == 0) {
        return -1;
    }
    const std::string payload = build_simple_json(
        {
            {"device_id", device_id},
            {"request_type", "recommendations"},
            {"timestamp", iso8601_now()}
        });
    std::string response;
    if (publish_and_wait(agent,
                         agent->topics.qc_guidance_req,
                         payload,
                         &response,
                         std::max(1000, agent->config.command_timeout_ms > 0
                                          ? agent->config.command_timeout_ms
                                          : kDefaultCommandTimeoutMs)) != 0) {
        return -1;
    }
#ifdef HAS_NLOHMANN_JSON
    Json json = parse_json(response);
    if (json.is_object() && json.contains("guidance")) {
        response = json["guidance"].is_string() ? json["guidance"].get<std::string>() : json["guidance"].dump();
    }
#endif
    copy_cstr(recommendations, buffer_size, response);
    {
        std::lock_guard<std::mutex> lock(agent->mutex);
        agent->last_qc_guidance = response;
    }
    return 0;
}

int cloud_agent_report_inference_stats(CloudAgent* agent, const InferenceStats* stats) {
    if (!agent || !stats) {
        return -1;
    }
    return publish_message(agent, agent->topics.inference_stats, serialize_inference_stats(agent, *stats), 1, false)
             ? 0
             : -1;
}

int cloud_agent_download_federated_model(CloudAgent* agent,
                                         uint8_t* model_buffer,
                                         size_t buffer_size,
                                         size_t* actual_size) {
    if (!agent || !model_buffer || buffer_size == 0) {
        return -1;
    }
    const std::string payload = build_simple_json(
        {
            {"device_id", agent->device_info.device_id},
            {"request_type", "federated_model"},
            {"timestamp", iso8601_now()}
        });
    std::string response;
    if (publish_and_wait(agent,
                         agent->topics.federated_model_req,
                         payload,
                         &response,
                         std::max(1000, agent->config.command_timeout_ms > 0
                                          ? agent->config.command_timeout_ms
                                          : kDefaultCommandTimeoutMs)) != 0) {
        return -1;
    }
#ifdef HAS_NLOHMANN_JSON
    Json json = parse_json(response);
    if (json.is_object() && json.contains("model_blob")) {
        response = json["model_blob"].is_string() ? json["model_blob"].get<std::string>() : json["model_blob"].dump();
    }
#endif
    {
        std::lock_guard<std::mutex> lock(agent->mutex);
        agent->last_federated_model_blob = response;
    }
    const size_t copy_size = std::min(buffer_size, response.size());
    std::memcpy(model_buffer, response.data(), copy_size);
    if (actual_size) {
        *actual_size = copy_size;
    }
    return copy_size == response.size() ? 0 : -1;
}

int cloud_agent_upload_gradients(CloudAgent* agent, const uint8_t* gradients, size_t size) {
    if (!agent || !gradients || size == 0) {
        return -1;
    }
#ifdef HAS_NLOHMANN_JSON
    Json json = {
        {"device_id", agent->device_info.device_id},
        {"timestamp", iso8601_now()},
        {"size", size},
        {"gradients_hex", bytes_to_hex(gradients, size)}
    };
    return publish_message(agent, agent->topics.gradients, json_dump(json), 1, false) ? 0 : -1;
#else
    const std::string payload = build_simple_json(
        {
            {"device_id", agent->device_info.device_id},
            {"timestamp", iso8601_now()},
            {"gradients_hex", bytes_to_hex(gradients, size)}
        },
        {},
        {
            {"size", static_cast<double>(size)}
        });
    return publish_message(agent, agent->topics.gradients, payload, 1, false) ? 0 : -1;
#endif
}

int cloud_agent_sync_time(CloudAgent* agent, double* offset) {
    if (!agent || !offset) {
        return -1;
    }
    const uint64_t send_ts = now_ms();
    const std::string payload = build_simple_json(
        {
            {"device_id", agent->device_info.device_id},
            {"timestamp", iso8601_now()}
        },
        {},
        {
            {"client_ts_ms", static_cast<double>(send_ts)}
        });
    std::string response;
    if (publish_and_wait(agent,
                         agent->topics.time_sync_req,
                         payload,
                         &response,
                         std::max(1000, agent->config.command_timeout_ms > 0
                                          ? agent->config.command_timeout_ms
                                          : kDefaultCommandTimeoutMs)) != 0) {
        return -1;
    }
#ifdef HAS_NLOHMANN_JSON
    Json json = parse_json(response);
    const double server_ts_ms = json.value("server_ts_ms", static_cast<double>(now_ms()));
    const double receive_ts_ms = static_cast<double>(now_ms());
    *offset = (server_ts_ms - ((static_cast<double>(send_ts) + receive_ts_ms) / 2.0)) / 1000.0;
#else
    *offset = 0.0;
#endif
    return 0;
}

int cloud_agent_sync_calibration(CloudAgent* agent, const uint8_t* calibration_data, size_t size) {
    if (!agent || !calibration_data || size == 0) {
        return -1;
    }
#ifdef HAS_NLOHMANN_JSON
    Json json = {
        {"device_id", agent->device_info.device_id},
        {"timestamp", iso8601_now()},
        {"size", size},
        {"payload_hex", bytes_to_hex(calibration_data, size)}
    };
    return publish_message(agent, agent->topics.calibration_sync, json_dump(json), 1, false) ? 0 : -1;
#else
    const std::string payload = build_simple_json(
        {
            {"device_id", agent->device_info.device_id},
            {"timestamp", iso8601_now()},
            {"payload_hex", bytes_to_hex(calibration_data, size)}
        },
        {},
        {
            {"size", static_cast<double>(size)}
        });
    return publish_message(agent, agent->topics.calibration_sync, payload, 1, false) ? 0 : -1;
#endif
}

int cloud_agent_upload_calibration(CloudAgent* agent, const CloudCalibrationReport* report) {
    if (!agent || !report) {
        return -1;
    }
#ifdef HAS_NLOHMANN_JSON
    Json json = {
        {"device_id", report->device_id[0] ? report->device_id : agent->device_info.device_id},
        {"calibration_id", report->calibration_id},
        {"timestamp", report->timestamp},
        {"delta_e", report->delta_e},
        {"luminance", report->luminance},
        {"uniformity", report->uniformity},
        {"gamma", report->gamma},
        {"report_json", report->report_json}
    };
    return publish_message(agent, agent->topics.qc_report, json_dump(json), 1, false) ? 0 : -1;
#else
    const std::string payload = build_simple_json(
        {
            {"device_id", report->device_id[0] ? report->device_id : agent->device_info.device_id},
            {"calibration_id", report->calibration_id},
            {"timestamp", report->timestamp},
            {"report_json", report->report_json}
        },
        {},
        {
            {"delta_e", report->delta_e},
            {"luminance", report->luminance},
            {"uniformity", report->uniformity},
            {"gamma", report->gamma}
        });
    return publish_message(agent, agent->topics.qc_report, payload, 1, false) ? 0 : -1;
#endif
}

int cloud_agent_request_calibration_guidance(CloudAgent* agent,
                                             const char* device_id,
                                             char* guidance,
                                             size_t buffer_size) {
    if (!agent || !device_id || !guidance || buffer_size == 0) {
        return -1;
    }
    return cloud_agent_get_recommendations(agent, device_id, guidance, buffer_size);
}

void cloud_agent_set_event_callback(CloudAgent* agent, CloudEventCallback callback, void* userdata) {
    if (!agent) {
        return;
    }
    std::lock_guard<std::mutex> lock(agent->mutex);
    agent->event_callback = callback;
    agent->event_userdata = userdata;
}

}  // extern "C"
