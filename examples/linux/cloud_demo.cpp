#include "demo_support.h"
#include "ai_engine.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string device_id = "demo-device-001";
    std::string current_version = "1.0.0";
    std::string server;
    std::filesystem::path manifest_path;
    std::filesystem::path output_dir = std::filesystem::path("output") / "cloud_demo";
};

struct Manifest {
    std::string version;
    std::filesystem::path package_path;
    std::string checksum;
    bool mandatory = false;
    std::string notes;
};

Options parse_options(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--device-id" && i + 1 < argc) {
            options.device_id = argv[++i];
        } else if (arg == "--current-version" && i + 1 < argc) {
            options.current_version = argv[++i];
        } else if (arg == "--server" && i + 1 < argc) {
            options.server = argv[++i];
        } else if (arg == "--manifest" && i + 1 < argc) {
            options.manifest_path = argv[++i];
        } else if (arg == "--output-dir" && i + 1 < argc) {
            options.output_dir = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: cloud_demo [--device-id id] [--current-version v] [--server host:port]\n"
                         "                  [--manifest path] [--output-dir dir]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("未知参数: " + arg);
        }
    }
    return options;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("无法读取文件: " + path.string());
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

std::string json_string_value(const std::string& text, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_pos = text.find(pattern);
    if (key_pos == std::string::npos) return {};
    const std::size_t colon_pos = text.find(':', key_pos + pattern.size());
    const std::size_t first_quote = text.find('"', colon_pos + 1);
    const std::size_t second_quote = text.find('"', first_quote + 1);
    if (colon_pos == std::string::npos || first_quote == std::string::npos || second_quote == std::string::npos) return {};
    return text.substr(first_quote + 1, second_quote - first_quote - 1);
}

bool json_bool_value(const std::string& text, const std::string& key, bool fallback) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_pos = text.find(pattern);
    if (key_pos == std::string::npos) return fallback;
    const std::size_t colon_pos = text.find(':', key_pos + pattern.size());
    const std::string tail = text.substr(colon_pos + 1, 8);
    if (tail.find("true") != std::string::npos) return true;
    if (tail.find("false") != std::string::npos) return false;
    return fallback;
}

int compare_versions(const std::string& lhs, const std::string& rhs) {
    std::istringstream left(lhs);
    std::istringstream right(rhs);
    while (left.good() || right.good()) {
        std::string left_part;
        std::string right_part;
        const bool has_left = static_cast<bool>(std::getline(left, left_part, '.'));
        const bool has_right = static_cast<bool>(std::getline(right, right_part, '.'));
        if (!has_left && !has_right) {
            break;
        }
        const int a = left_part.empty() ? 0 : std::stoi(left_part);
        const int b = right_part.empty() ? 0 : std::stoi(right_part);
        if (a < b) return -1;
        if (a > b) return 1;
    }
    return 0;
}

bool try_connect_server(const std::string& endpoint, std::string& error) {
    const std::size_t colon = endpoint.rfind(':');
    if (colon == std::string::npos) {
        error = "server 需要 host:port 格式";
        return false;
    }

    const std::string host = endpoint.substr(0, colon);
    const std::string port = endpoint.substr(colon + 1);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &results) != 0) {
        error = "解析服务端地址失败";
        return false;
    }

    bool connected = false;
    for (addrinfo* current = results; current != nullptr; current = current->ai_next) {
        const int fd = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, current->ai_addr, current->ai_addrlen) == 0) {
            connected = true;
            ::close(fd);
            break;
        }
        ::close(fd);
    }

    freeaddrinfo(results);
    if (!connected) {
        error = "连接服务端失败: " + endpoint;
    }
    return connected;
}

Manifest create_sample_bundle(const std::filesystem::path& output_dir, std::filesystem::path& manifest_path) {
    std::filesystem::path package_path = output_dir / "ota_package_v1.1.0.bin";
    std::ofstream package(package_path, std::ios::binary);
    for (int i = 0; i < 256 * 1024; ++i) {
        const std::uint8_t byte = static_cast<std::uint8_t>((i * 37) % 251);
        package.write(reinterpret_cast<const char*>(&byte), 1);
    }
    package.close();

    std::string error;
    const std::string checksum = medicaldemo::hex32(medicaldemo::fnv1a32_file(package_path, error));
    if (!error.empty()) {
        throw std::runtime_error(error);
    }

    manifest_path = output_dir / "cloud_manifest.json";
    std::ofstream manifest(manifest_path);
    manifest << "{\n"
             << "  \"version\": \"1.1.0\",\n"
             << "  \"package\": \"" << package_path.filename().string() << "\",\n"
             << "  \"checksum\": \"" << checksum << "\",\n"
             << "  \"mandatory\": false,\n"
             << "  \"notes\": \"Demo OTA package with staged checksum verification\"\n"
             << "}\n";
    manifest.close();

    return {"1.1.0", package_path, checksum, false, "Demo OTA package with staged checksum verification"};
}

Manifest load_manifest(const std::filesystem::path& manifest_path) {
    const std::string text = read_text_file(manifest_path);
    Manifest manifest;
    manifest.version = json_string_value(text, "version");
    manifest.package_path = manifest_path.parent_path() / json_string_value(text, "package");
    manifest.checksum = json_string_value(text, "checksum");
    manifest.mandatory = json_bool_value(text, "mandatory", false);
    manifest.notes = json_string_value(text, "notes");
    if (manifest.version.empty() || manifest.package_path.empty() || manifest.checksum.empty()) {
        throw std::runtime_error("OTA 清单缺少必要字段");
    }
    return manifest;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parse_options(argc, argv);
        std::string error;
        if (!medicaldemo::ensure_directory(options.output_dir, error)) {
            throw std::runtime_error(error);
        }

        std::filesystem::path manifest_path = options.manifest_path;
        Manifest manifest;
        if (manifest_path.empty()) {
            manifest = create_sample_bundle(options.output_dir, manifest_path);
        } else {
            manifest = load_manifest(manifest_path);
        }

        if (!options.server.empty()) {
            if (!try_connect_server(options.server, error)) {
                throw std::runtime_error(error);
            }
            std::cout << "云端连接成功: " << options.server << '\n';
        } else {
            std::cout << "使用本地云清单: " << manifest_path << '\n';
        }

        AIEngineConfig ai_config{};
        ai_config.input_width = 512;
        ai_config.input_height = 512;
        AIEngine* ai_engine = ai_engine_create(&ai_config);
        if (!ai_engine) {
            throw std::runtime_error("AIEngine 初始化失败");
        }
        const char* ai_backend = ai_engine_backend_status_name(ai_engine_get_backend_status(ai_engine));

        std::ofstream telemetry(options.output_dir / "telemetry.jsonl", std::ios::app);
        telemetry << "{\"time\":\"" << medicaldemo::now_local_string()
                  << "\",\"device_id\":\"" << options.device_id
                  << "\",\"version\":\"" << options.current_version
                  << "\",\"ai_backend\":\"" << ai_backend
                  << "\",\"fps\":60.0,\"temperature\":42.5,\"health\":98.0}\n";

        if (compare_versions(options.current_version, manifest.version) >= 0) {
            std::cout << "当前版本已是最新: " << options.current_version << '\n';
            ai_engine_destroy(ai_engine);
            return 0;
        }

        const std::filesystem::path staging_dir = options.output_dir / "staging";
        if (!medicaldemo::ensure_directory(staging_dir, error)) {
            throw std::runtime_error(error);
        }

        const std::filesystem::path staged_package = staging_dir / manifest.package_path.filename();
        std::cout << "发现更新: " << options.current_version << " -> " << manifest.version << '\n';
        std::cout << "更新说明: " << manifest.notes << '\n';

        std::size_t last_percent = 0;
        if (!medicaldemo::copy_file_with_progress(
                manifest.package_path,
                staged_package,
                64 * 1024,
                [&](std::size_t transferred, std::size_t total) {
                    const std::size_t percent = total == 0 ? 100 : (transferred * 100 / total);
                    if (percent >= last_percent + 25 || percent == 100) {
                        last_percent = percent;
                        std::cout << "下载进度: " << percent << "%\n";
                    }
                },
                error)) {
            throw std::runtime_error(error);
        }

        const std::string staged_checksum = medicaldemo::hex32(medicaldemo::fnv1a32_file(staged_package, error));
        if (!error.empty()) {
            throw std::runtime_error(error);
        }
        if (staged_checksum != manifest.checksum) {
            throw std::runtime_error("OTA 校验失败: expected=" + manifest.checksum + " actual=" + staged_checksum);
        }

        std::ofstream device_state(options.output_dir / "device_state.json");
        device_state << "{\n"
                     << "  \"device_id\": \"" << options.device_id << "\",\n"
                     << "  \"version\": \"" << manifest.version << "\",\n"
                     << "  \"staged_package\": \"" << staged_package.filename().string() << "\",\n"
                     << "  \"updated_at\": \"" << medicaldemo::now_local_string() << "\"\n"
                     << "}\n";

        std::cout << "OTA 校验通过: " << staged_checksum << '\n';
        std::cout << "升级完成，当前版本: " << manifest.version << '\n';
        std::cout << "升级包大小: " << medicaldemo::pretty_bytes(std::filesystem::file_size(staged_package)) << '\n';
        std::cout << "设备状态文件: " << (options.output_dir / "device_state.json") << '\n';
        ai_engine_destroy(ai_engine);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "cloud_demo 失败: " << ex.what() << '\n';
        return 1;
    }
}
