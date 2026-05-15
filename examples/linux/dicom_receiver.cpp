#include "demo_support.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint8_t kPduAssociateRq = 0x01;
constexpr std::uint8_t kPduAssociateAc = 0x02;
constexpr std::uint8_t kPduAssociateRj = 0x03;
constexpr std::uint8_t kPduPDataTf = 0x04;
constexpr std::uint8_t kPduReleaseRq = 0x05;
constexpr std::uint8_t kPduReleaseRp = 0x06;

constexpr const char* kApplicationContextUid = "1.2.840.10008.3.1.1.1";
constexpr const char* kTransferSyntaxExplicitLittle = "1.2.840.10008.1.2.1";
constexpr const char* kTransferSyntaxImplicitLittle = "1.2.840.10008.1.2";

struct Options {
    int port = 11112;
    int max_instances = 1;
    int idle_timeout_sec = 15;
    bool self_test = false;
    std::string modality = "CT";
    std::string ae_title = "MDISPLAY";
    std::filesystem::path output_dir = std::filesystem::path("output") / "dicom_receiver";
};

struct PresentationContextRequest {
    std::uint8_t id = 1;
    std::string abstract_syntax;
    std::vector<std::string> transfer_syntaxes;
};

struct AssociationRequest {
    std::string called_ae = "MDISPLAY";
    std::string calling_ae = "SCU";
    std::vector<PresentationContextRequest> contexts;
};

struct AcceptedContext {
    std::uint8_t id = 1;
    std::string transfer_syntax;
};

struct CommandInfo {
    std::uint16_t command_field = 0;
    std::uint16_t message_id = 0;
    std::uint16_t dataset_type = 0x0101;
    std::uint16_t status = 0;
    std::string sop_class_uid;
    std::string sop_instance_uid;
};

struct ReceiverStats {
    medicaldemo::PerformanceStats receive_stats;
    medicaldemo::PerformanceStats render_stats;
    std::size_t total_bytes = 0;
    int total_instances = 0;
};

struct SelfTestResult {
    bool success = false;
    std::string message;
};

void append_be16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void append_le16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void append_le32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

std::uint16_t read_be16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8) | data[offset + 1]);
}

std::uint32_t read_be32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return (static_cast<std::uint32_t>(data[offset]) << 24) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
           static_cast<std::uint32_t>(data[offset + 3]);
}

std::uint16_t read_le16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] | (static_cast<std::uint16_t>(data[offset + 1]) << 8));
}

std::uint32_t read_le32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::string pad_ae(const std::string& value) {
    std::string out = value.substr(0, 16);
    while (out.size() < 16) {
        out.push_back(' ');
    }
    return out;
}

std::string trim_text(std::string value) {
    while (!value.empty() && (value.back() == '\0' || value.back() == ' ' || value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }
    return value;
}

bool read_exact(int fd, void* buffer, std::size_t length, std::string& error) {
    std::size_t offset = 0;
    while (offset < length) {
        const ssize_t received = ::recv(fd, static_cast<char*>(buffer) + offset, length - offset, 0);
        if (received == 0) {
            error = "连接被对端关闭";
            return false;
        }
        if (received < 0) {
            error = std::string("读取 socket 失败: ") + std::strerror(errno);
            return false;
        }
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

bool write_exact(int fd, const void* buffer, std::size_t length, std::string& error) {
    std::size_t offset = 0;
    while (offset < length) {
        const ssize_t sent = ::send(fd, static_cast<const char*>(buffer) + offset, length - offset, 0);
        if (sent <= 0) {
            error = std::string("写入 socket 失败: ") + std::strerror(errno);
            return false;
        }
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

bool send_pdu(int fd, std::uint8_t type, const std::vector<std::uint8_t>& body, std::string& error) {
    std::vector<std::uint8_t> header;
    header.reserve(6);
    header.push_back(type);
    header.push_back(0);
    append_be32(header, static_cast<std::uint32_t>(body.size()));
    return write_exact(fd, header.data(), header.size(), error) &&
           (body.empty() || write_exact(fd, body.data(), body.size(), error));
}

bool receive_pdu(int fd, std::uint8_t& type, std::vector<std::uint8_t>& body, std::string& error) {
    std::array<std::uint8_t, 6> header{};
    if (!read_exact(fd, header.data(), header.size(), error)) {
        return false;
    }
    type = header[0];
    const std::uint32_t length = (static_cast<std::uint32_t>(header[2]) << 24) |
                                 (static_cast<std::uint32_t>(header[3]) << 16) |
                                 (static_cast<std::uint32_t>(header[4]) << 8) |
                                 static_cast<std::uint32_t>(header[5]);
    body.assign(length, 0);
    if (length > 0 && !read_exact(fd, body.data(), body.size(), error)) {
        return false;
    }
    return true;
}

Options parse_options(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            options.port = std::stoi(argv[++i]);
        } else if (arg == "--max-instances" && i + 1 < argc) {
            options.max_instances = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--idle-timeout-sec" && i + 1 < argc) {
            options.idle_timeout_sec = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--output-dir" && i + 1 < argc) {
            options.output_dir = argv[++i];
        } else if (arg == "--self-test") {
            options.self_test = true;
        } else if (arg == "--modality" && i + 1 < argc) {
            options.modality = argv[++i];
        } else if (arg == "--ae-title" && i + 1 < argc) {
            options.ae_title = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: dicom_receiver [--port 11112] [--max-instances N] [--idle-timeout-sec N]\n"
                         "                      [--output-dir dir] [--ae-title title] [--modality CT|MR|DX|US]\n"
                         "                      [--self-test]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("未知参数: " + arg);
        }
    }
    return options;
}

bool parse_associate_rq(const std::vector<std::uint8_t>& body, AssociationRequest& request, std::string& error) {
    if (body.size() < 68) {
        error = "A-ASSOCIATE-RQ 过短";
        return false;
    }
    request.called_ae = trim_text(std::string(reinterpret_cast<const char*>(&body[4]), 16));
    request.calling_ae = trim_text(std::string(reinterpret_cast<const char*>(&body[20]), 16));

    std::size_t offset = 68;
    while (offset + 4 <= body.size()) {
        const std::uint8_t item_type = body[offset];
        const std::uint16_t item_length = read_be16(body, offset + 2);
        const std::size_t item_value = offset + 4;
        if (item_value + item_length > body.size()) {
            error = "A-ASSOCIATE-RQ item 越界";
            return false;
        }

        if (item_type == 0x20) {
            PresentationContextRequest context;
            context.id = body[item_value];
            std::size_t sub_offset = item_value + 4;
            while (sub_offset + 4 <= item_value + item_length) {
                const std::uint8_t sub_type = body[sub_offset];
                const std::uint16_t sub_length = read_be16(body, sub_offset + 2);
                const std::size_t sub_value = sub_offset + 4;
                if (sub_value + sub_length > item_value + item_length) {
                    error = "Presentation Context sub-item 越界";
                    return false;
                }
                const std::string value(reinterpret_cast<const char*>(&body[sub_value]), sub_length);
                if (sub_type == 0x30) {
                    context.abstract_syntax = value;
                } else if (sub_type == 0x40) {
                    context.transfer_syntaxes.push_back(value);
                }
                sub_offset = sub_value + sub_length;
            }
            request.contexts.push_back(std::move(context));
        }
        offset = item_value + item_length;
    }
    return !request.contexts.empty();
}

std::vector<std::uint8_t> build_associate_ac(const AssociationRequest& request,
                                             std::vector<AcceptedContext>& accepted_contexts) {
    std::vector<std::uint8_t> body;
    append_be16(body, 0x0001);
    append_be16(body, 0x0000);
    const std::string called = pad_ae(request.called_ae.empty() ? "MDISPLAY" : request.called_ae);
    const std::string calling = pad_ae(request.calling_ae.empty() ? "SCU" : request.calling_ae);
    body.insert(body.end(), called.begin(), called.end());
    body.insert(body.end(), calling.begin(), calling.end());
    body.insert(body.end(), 32, 0);

    {
        std::vector<std::uint8_t> item{0x10, 0x00};
        append_be16(item, static_cast<std::uint16_t>(std::strlen(kApplicationContextUid)));
        item.insert(item.end(), kApplicationContextUid, kApplicationContextUid + std::strlen(kApplicationContextUid));
        body.insert(body.end(), item.begin(), item.end());
    }

    for (const auto& context : request.contexts) {
        std::string accepted_ts;
        for (const auto& ts : context.transfer_syntaxes) {
            if (ts == kTransferSyntaxExplicitLittle || ts == kTransferSyntaxImplicitLittle) {
                accepted_ts = ts;
                break;
            }
        }

        std::vector<std::uint8_t> context_item;
        context_item.push_back(context.id);
        context_item.push_back(0x00);
        context_item.push_back(accepted_ts.empty() ? 0x04 : 0x00);
        context_item.push_back(0x00);

        if (!accepted_ts.empty()) {
            std::vector<std::uint8_t> ts_item{0x40, 0x00};
            append_be16(ts_item, static_cast<std::uint16_t>(accepted_ts.size()));
            ts_item.insert(ts_item.end(), accepted_ts.begin(), accepted_ts.end());
            context_item.insert(context_item.end(), ts_item.begin(), ts_item.end());
            accepted_contexts.push_back({context.id, accepted_ts});
        }

        std::vector<std::uint8_t> wrapped{0x21, 0x00};
        append_be16(wrapped, static_cast<std::uint16_t>(context_item.size()));
        wrapped.insert(wrapped.end(), context_item.begin(), context_item.end());
        body.insert(body.end(), wrapped.begin(), wrapped.end());
    }

    {
        std::vector<std::uint8_t> max_pdu{0x51, 0x00};
        append_be16(max_pdu, 4);
        append_be32(max_pdu, 16384);
        std::vector<std::uint8_t> user_info{0x50, 0x00};
        append_be16(user_info, static_cast<std::uint16_t>(max_pdu.size()));
        user_info.insert(user_info.end(), max_pdu.begin(), max_pdu.end());
        body.insert(body.end(), user_info.begin(), user_info.end());
    }
    return body;
}

std::optional<AcceptedContext> find_context(const std::vector<AcceptedContext>& contexts, std::uint8_t id) {
    for (const auto& context : contexts) {
        if (context.id == id) {
            return context;
        }
    }
    return std::nullopt;
}

void append_implicit_element(std::vector<std::uint8_t>& out,
                             std::uint16_t group,
                             std::uint16_t element,
                             const std::uint8_t* bytes,
                             std::uint32_t length) {
    append_le16(out, group);
    append_le16(out, element);
    append_le32(out, length);
    out.insert(out.end(), bytes, bytes + length);
}

void append_implicit_string(std::vector<std::uint8_t>& out,
                            std::uint16_t group,
                            std::uint16_t element,
                            const std::string& value,
                            char pad = ' ') {
    std::string even = value;
    if (even.size() % 2 != 0) {
        even.push_back(pad);
    }
    append_implicit_element(out, group, element,
                            reinterpret_cast<const std::uint8_t*>(even.data()),
                            static_cast<std::uint32_t>(even.size()));
}

void append_implicit_us(std::vector<std::uint8_t>& out,
                        std::uint16_t group,
                        std::uint16_t element,
                        std::uint16_t value) {
    std::array<std::uint8_t, 2> raw{
        static_cast<std::uint8_t>(value & 0xFFu),
        static_cast<std::uint8_t>((value >> 8) & 0xFFu),
    };
    append_implicit_element(out, group, element, raw.data(), static_cast<std::uint32_t>(raw.size()));
}

bool parse_command_set(const std::vector<std::uint8_t>& bytes, CommandInfo& info, std::string& error) {
    std::size_t offset = 0;
    while (offset + 8 <= bytes.size()) {
        const std::uint16_t group = read_le16(bytes, offset);
        const std::uint16_t element = read_le16(bytes, offset + 2);
        const std::uint32_t length = read_le32(bytes, offset + 4);
        offset += 8;
        if (offset + length > bytes.size()) {
            error = "Command set 越界";
            return false;
        }
        if (group == 0x0000 && element == 0x0100 && length >= 2) {
            info.command_field = read_le16(bytes, offset);
        } else if (group == 0x0000 && element == 0x0110 && length >= 2) {
            info.message_id = read_le16(bytes, offset);
        } else if (group == 0x0000 && element == 0x0800 && length >= 2) {
            info.dataset_type = read_le16(bytes, offset);
        } else if (group == 0x0000 && element == 0x0900 && length >= 2) {
            info.status = read_le16(bytes, offset);
        } else if (group == 0x0000 && element == 0x0002) {
            info.sop_class_uid = trim_text(std::string(reinterpret_cast<const char*>(&bytes[offset]), length));
        } else if (group == 0x0000 && element == 0x1000) {
            info.sop_instance_uid = trim_text(std::string(reinterpret_cast<const char*>(&bytes[offset]), length));
        }
        offset += length;
    }
    return true;
}

bool send_pdata(int fd, std::uint8_t context_id, const std::vector<std::uint8_t>& data, bool is_command, std::string& error) {
    std::vector<std::uint8_t> body;
    append_be32(body, static_cast<std::uint32_t>(data.size() + 2));
    body.push_back(context_id);
    body.push_back(is_command ? 0x03 : 0x02);
    body.insert(body.end(), data.begin(), data.end());
    return send_pdu(fd, kPduPDataTf, body, error);
}

bool collect_dimse_from_pdata(int fd,
                              const std::vector<std::uint8_t>& first_body,
                              std::uint8_t& context_id,
                              std::vector<std::uint8_t>& command_set,
                              std::vector<std::uint8_t>& dataset,
                              std::string& error) {
    bool command_done = false;
    bool dataset_done = false;
    bool expects_dataset = true;
    std::vector<std::uint8_t> current_body = first_body;

    while (true) {
        std::size_t offset = 0;
        while (offset + 4 <= current_body.size()) {
            const std::uint32_t pdv_length = read_be32(current_body, offset);
            offset += 4;
            if (offset + pdv_length > current_body.size() || pdv_length < 2) {
                error = "P-DATA PDV 越界";
                return false;
            }

            context_id = current_body[offset];
            const std::uint8_t header = current_body[offset + 1];
            const bool is_command = (header & 0x01u) != 0;
            const bool is_last = (header & 0x02u) != 0;
            const std::size_t value_offset = offset + 2;
            const std::size_t value_length = pdv_length - 2;

            if (is_command) {
                command_set.insert(command_set.end(),
                                   current_body.begin() + static_cast<std::ptrdiff_t>(value_offset),
                                   current_body.begin() + static_cast<std::ptrdiff_t>(value_offset + value_length));
                if (is_last) {
                    command_done = true;
                    CommandInfo info;
                    if (!parse_command_set(command_set, info, error)) {
                        return false;
                    }
                    expects_dataset = info.dataset_type != 0x0101;
                    if (!expects_dataset) {
                        dataset_done = true;
                    }
                }
            } else {
                dataset.insert(dataset.end(),
                               current_body.begin() + static_cast<std::ptrdiff_t>(value_offset),
                               current_body.begin() + static_cast<std::ptrdiff_t>(value_offset + value_length));
                if (is_last) {
                    dataset_done = true;
                }
            }
            offset += pdv_length;
        }

        if (command_done && dataset_done) {
            return true;
        }

        std::uint8_t next_type = 0;
        if (!receive_pdu(fd, next_type, current_body, error)) {
            return false;
        }
        if (next_type != kPduPDataTf) {
            error = "等待 P-DATA 时收到非 P-DATA PDU";
            return false;
        }
    }
}

bool wait_for_socket(int fd, int timeout_sec) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval tv{};
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    return ::select(fd + 1, &set, nullptr, nullptr, &tv) > 0;
}

bool handle_association(int client_fd,
                        const Options& options,
                        ReceiverStats& stats,
                        std::string& error) {
    std::uint8_t pdu_type = 0;
    std::vector<std::uint8_t> body;
    if (!receive_pdu(client_fd, pdu_type, body, error)) {
        return false;
    }
    if (pdu_type != kPduAssociateRq) {
        error = "首个 PDU 不是 A-ASSOCIATE-RQ";
        return false;
    }

    AssociationRequest request;
    if (!parse_associate_rq(body, request, error)) {
        return false;
    }

    std::vector<AcceptedContext> accepted_contexts;
    const auto ac_body = build_associate_ac(request, accepted_contexts);
    if (accepted_contexts.empty()) {
        const std::vector<std::uint8_t> reject{0x00, 0x01, 0x01, 0x07};
        return send_pdu(client_fd, kPduAssociateRj, reject, error);
    }
    if (!send_pdu(client_fd, kPduAssociateAc, ac_body, error)) {
        return false;
    }

    if (!receive_pdu(client_fd, pdu_type, body, error)) {
        return false;
    }
    if (pdu_type != kPduPDataTf) {
        error = "期望收到 P-DATA-TF";
        return false;
    }

    const auto receive_begin = std::chrono::steady_clock::now();
    std::uint8_t context_id = 0;
    std::vector<std::uint8_t> command_set;
    std::vector<std::uint8_t> dataset;
    if (!collect_dimse_from_pdata(client_fd, body, context_id, command_set, dataset, error)) {
        return false;
    }
    const auto receive_end = std::chrono::steady_clock::now();
    stats.receive_stats.add(std::chrono::duration<double, std::milli>(receive_end - receive_begin).count());

    CommandInfo request_info;
    if (!parse_command_set(command_set, request_info, error)) {
        return false;
    }
    if (request_info.command_field != 0x0001) {
        error = "当前示例仅支持 C-STORE-RQ";
        return false;
    }

    const auto accepted_context = find_context(accepted_contexts, context_id);
    if (!accepted_context.has_value()) {
        error = "未找到对应的已接受 Presentation Context";
        return false;
    }

    const std::filesystem::path receive_dir = options.output_dir / "received";
    const std::filesystem::path preview_dir = options.output_dir / "preview";
    const std::string sop_uid = request_info.sop_instance_uid.empty() ? medicaldemo::random_uid() : request_info.sop_instance_uid;
    const std::filesystem::path dicom_path = receive_dir / (medicaldemo::sanitize_filename(sop_uid) + ".dcm");

    if (!medicaldemo::ensure_directory(receive_dir, error) || !medicaldemo::ensure_directory(preview_dir, error)) {
        return false;
    }
    if (!medicaldemo::save_part10_from_dataset(dicom_path,
                                               dataset,
                                               request_info.sop_class_uid.empty() ? medicaldemo::modality_to_sop_class(options.modality) : request_info.sop_class_uid,
                                               sop_uid,
                                               accepted_context->transfer_syntax,
                                               error)) {
        return false;
    }

    medicaldemo::DicomImage image;
    if (!medicaldemo::load_dicom_file(dicom_path, image, error)) {
        return false;
    }
    const auto recognition = medicaldemo::recognize_image(image);
    const auto render_metrics = medicaldemo::render_preview(image, recognition, preview_dir, stats.total_instances, true, error);
    if (!error.empty()) {
        return false;
    }
    stats.render_stats.add(render_metrics.render_ms);
    stats.total_bytes += dataset.size();
    ++stats.total_instances;

    std::cout << "收到 C-STORE: modality=" << image.modality
              << " sop=" << sop_uid
              << " bytes=" << dataset.size()
              << " file=" << dicom_path
              << " preview=" << (preview_dir / "latest.pgm") << '\n';

    std::vector<std::uint8_t> response_body;
    append_implicit_string(response_body, 0x0000, 0x0002, request_info.sop_class_uid, '\0');
    append_implicit_us(response_body, 0x0000, 0x0100, 0x8001);
    append_implicit_us(response_body, 0x0000, 0x0120, request_info.message_id);
    append_implicit_us(response_body, 0x0000, 0x0800, 0x0101);
    append_implicit_us(response_body, 0x0000, 0x0900, 0x0000);
    append_implicit_string(response_body, 0x0000, 0x1000, sop_uid, '\0');

    std::vector<std::uint8_t> response_full;
    append_le16(response_full, 0x0000);
    append_le16(response_full, 0x0000);
    append_le32(response_full, 4);
    append_le32(response_full, static_cast<std::uint32_t>(response_body.size()));
    response_full.insert(response_full.end(), response_body.begin(), response_body.end());

    if (!send_pdata(client_fd, context_id, response_full, true, error)) {
        return false;
    }

    if (!receive_pdu(client_fd, pdu_type, body, error)) {
        return false;
    }
    if (pdu_type != kPduReleaseRq) {
        error = "未收到 A-RELEASE-RQ";
        return false;
    }
    const std::vector<std::uint8_t> release_body(4, 0);
    return send_pdu(client_fd, kPduReleaseRp, release_body, error);
}

int create_listen_socket(int port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    const int enable = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 4) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool run_receiver_server(const Options& options, ReceiverStats& stats, std::string& error) {
    const int listen_fd = create_listen_socket(options.port);
    if (listen_fd < 0) {
        error = std::string("创建监听 socket 失败: ") + std::strerror(errno);
        return false;
    }

    std::cout << "DICOM Receiver 监听端口: " << options.port << '\n';
    std::cout << "AE Title: " << options.ae_title << '\n';
    std::cout << "输出目录: " << options.output_dir << '\n';

    int completed = 0;
    while (completed < options.max_instances) {
        if (!wait_for_socket(listen_fd, options.idle_timeout_sec)) {
            std::cout << "空闲超时，停止监听。\n";
            break;
        }
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            error = std::string("accept 失败: ") + std::strerror(errno);
            ::close(listen_fd);
            return false;
        }
        std::cout << "接收连接: " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << '\n';
        const bool ok = handle_association(client_fd, options, stats, error);
        ::close(client_fd);
        if (!ok) {
            ::close(listen_fd);
            return false;
        }
        ++completed;
    }

    ::close(listen_fd);
    return true;
}

std::vector<std::uint8_t> build_associate_rq(const Options& options, const std::string& sop_class_uid, std::uint8_t context_id) {
    std::vector<std::uint8_t> body;
    append_be16(body, 0x0001);
    append_be16(body, 0x0000);
    const std::string called = pad_ae(options.ae_title);
    const std::string calling = pad_ae("SELFTEST");
    body.insert(body.end(), called.begin(), called.end());
    body.insert(body.end(), calling.begin(), calling.end());
    body.insert(body.end(), 32, 0);

    {
        std::vector<std::uint8_t> item{0x10, 0x00};
        append_be16(item, static_cast<std::uint16_t>(std::strlen(kApplicationContextUid)));
        item.insert(item.end(), kApplicationContextUid, kApplicationContextUid + std::strlen(kApplicationContextUid));
        body.insert(body.end(), item.begin(), item.end());
    }
    {
        std::vector<std::uint8_t> context;
        context.push_back(context_id);
        context.push_back(0x00);
        context.push_back(0x00);
        context.push_back(0x00);

        std::vector<std::uint8_t> abstract_item{0x30, 0x00};
        append_be16(abstract_item, static_cast<std::uint16_t>(sop_class_uid.size()));
        abstract_item.insert(abstract_item.end(), sop_class_uid.begin(), sop_class_uid.end());
        context.insert(context.end(), abstract_item.begin(), abstract_item.end());

        for (const std::string& ts : {std::string(kTransferSyntaxExplicitLittle), std::string(kTransferSyntaxImplicitLittle)}) {
            std::vector<std::uint8_t> transfer_item{0x40, 0x00};
            append_be16(transfer_item, static_cast<std::uint16_t>(ts.size()));
            transfer_item.insert(transfer_item.end(), ts.begin(), ts.end());
            context.insert(context.end(), transfer_item.begin(), transfer_item.end());
        }

        std::vector<std::uint8_t> wrapped{0x20, 0x00};
        append_be16(wrapped, static_cast<std::uint16_t>(context.size()));
        wrapped.insert(wrapped.end(), context.begin(), context.end());
        body.insert(body.end(), wrapped.begin(), wrapped.end());
    }
    {
        std::vector<std::uint8_t> max_pdu{0x51, 0x00};
        append_be16(max_pdu, 4);
        append_be32(max_pdu, 16384);
        std::vector<std::uint8_t> user_info{0x50, 0x00};
        append_be16(user_info, static_cast<std::uint16_t>(max_pdu.size()));
        user_info.insert(user_info.end(), max_pdu.begin(), max_pdu.end());
        body.insert(body.end(), user_info.begin(), user_info.end());
    }
    return body;
}

bool parse_associate_ac(const std::vector<std::uint8_t>& body, std::uint8_t expected_context_id, std::string& transfer_syntax, std::string& error) {
    if (body.size() < 68) {
        error = "A-ASSOCIATE-AC 过短";
        return false;
    }
    std::size_t offset = 68;
    while (offset + 4 <= body.size()) {
        const std::uint8_t item_type = body[offset];
        const std::uint16_t item_length = read_be16(body, offset + 2);
        const std::size_t item_value = offset + 4;
        if (item_value + item_length > body.size()) {
            error = "A-ASSOCIATE-AC item 越界";
            return false;
        }
        if (item_type == 0x21 && body[item_value] == expected_context_id && body[item_value + 2] == 0x00) {
            std::size_t sub_offset = item_value + 4;
            while (sub_offset + 4 <= item_value + item_length) {
                const std::uint8_t sub_type = body[sub_offset];
                const std::uint16_t sub_length = read_be16(body, sub_offset + 2);
                const std::size_t sub_value = sub_offset + 4;
                if (sub_value + sub_length > item_value + item_length) {
                    error = "A-ASSOCIATE-AC transfer syntax 越界";
                    return false;
                }
                if (sub_type == 0x40) {
                    transfer_syntax = std::string(reinterpret_cast<const char*>(&body[sub_value]), sub_length);
                    return true;
                }
                sub_offset = sub_value + sub_length;
            }
        }
        offset = item_value + item_length;
    }
    error = "未找到接受的 Presentation Context";
    return false;
}

std::vector<std::uint8_t> build_store_rq_command(const medicaldemo::DicomImage& image) {
    std::vector<std::uint8_t> body;
    const std::string sop_class_uid = medicaldemo::modality_to_sop_class(image.modality);
    append_implicit_string(body, 0x0000, 0x0002, sop_class_uid, '\0');
    append_implicit_us(body, 0x0000, 0x0100, 0x0001);
    append_implicit_us(body, 0x0000, 0x0110, 1);
    append_implicit_us(body, 0x0000, 0x0700, 0);
    append_implicit_us(body, 0x0000, 0x0800, 0x0000);
    append_implicit_string(body, 0x0000, 0x1000, image.sop_instance_uid, '\0');

    std::vector<std::uint8_t> full;
    append_le16(full, 0x0000);
    append_le16(full, 0x0000);
    append_le32(full, 4);
    append_le32(full, static_cast<std::uint32_t>(body.size()));
    full.insert(full.end(), body.begin(), body.end());
    return full;
}

SelfTestResult run_self_test(const Options& options) {
    SelfTestResult result;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        result.message = std::string("创建 self-test socket 失败: ") + std::strerror(errno);
        return result;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<std::uint16_t>(options.port));
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
        result.message = std::string("self-test 连接失败: ") + std::strerror(errno);
        ::close(fd);
        return result;
    }

    std::string error;
    const std::uint8_t context_id = 1;
    medicaldemo::DicomImage image = medicaldemo::make_synthetic_image(options.modality, 384, 384, 0);
    if (image.sop_instance_uid.empty()) {
        image.sop_instance_uid = medicaldemo::random_uid();
    }

    if (!send_pdu(fd, kPduAssociateRq, build_associate_rq(options, medicaldemo::modality_to_sop_class(image.modality), context_id), error)) {
        result.message = error;
        ::close(fd);
        return result;
    }

    std::uint8_t pdu_type = 0;
    std::vector<std::uint8_t> body;
    if (!receive_pdu(fd, pdu_type, body, error) || pdu_type != kPduAssociateAc) {
        result.message = error.empty() ? "self-test 未收到 A-ASSOCIATE-AC" : error;
        ::close(fd);
        return result;
    }

    std::string transfer_syntax;
    if (!parse_associate_ac(body, context_id, transfer_syntax, error)) {
        result.message = error;
        ::close(fd);
        return result;
    }
    if (transfer_syntax != kTransferSyntaxExplicitLittle) {
        result.message = "服务端未接受显式 Little Endian";
        ::close(fd);
        return result;
    }

    const auto dataset = medicaldemo::encode_explicit_little_endian_dataset(image);
    if (!send_pdata(fd, context_id, build_store_rq_command(image), true, error) ||
        !send_pdata(fd, context_id, dataset, false, error)) {
        result.message = error;
        ::close(fd);
        return result;
    }

    if (!receive_pdu(fd, pdu_type, body, error) || pdu_type != kPduPDataTf) {
        result.message = error.empty() ? "self-test 未收到 C-STORE-RSP" : error;
        ::close(fd);
        return result;
    }

    std::uint8_t response_context = 0;
    std::vector<std::uint8_t> response_command;
    std::vector<std::uint8_t> response_dataset;
    if (!collect_dimse_from_pdata(fd, body, response_context, response_command, response_dataset, error)) {
        result.message = error;
        ::close(fd);
        return result;
    }
    CommandInfo response_info;
    if (!parse_command_set(response_command, response_info, error) || response_info.command_field != 0x8001 || response_info.status != 0x0000) {
        result.message = error.empty() ? "self-test 收到异常响应" : error;
        ::close(fd);
        return result;
    }

    const std::vector<std::uint8_t> release_body(4, 0);
    if (!send_pdu(fd, kPduReleaseRq, release_body, error) ||
        !receive_pdu(fd, pdu_type, body, error) || pdu_type != kPduReleaseRp) {
        result.message = error.empty() ? "self-test 未收到 A-RELEASE-RP" : error;
        ::close(fd);
        return result;
    }

    ::close(fd);
    result.success = true;
    result.message = "self-test 成功完成一次最小 C-STORE 往返";
    return result;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parse_options(argc, argv);
        std::string error;
        if (!medicaldemo::ensure_directory(options.output_dir, error)) {
            throw std::runtime_error(error);
        }

        ReceiverStats stats;
        if (options.self_test) {
            std::mutex mutex;
            std::condition_variable cv;
            bool server_ready = false;
            bool server_done = false;
            bool server_ok = false;
            std::string server_error;

            std::thread server_thread([&] {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    server_ready = true;
                }
                cv.notify_one();
                server_ok = run_receiver_server(options, stats, server_error);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    server_done = true;
                }
                cv.notify_one();
            });

            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&] { return server_ready; });
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const SelfTestResult test = run_self_test(options);
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&] { return server_done; });
            }
            server_thread.join();

            if (!test.success) {
                throw std::runtime_error(test.message);
            }
            if (!server_ok) {
                throw std::runtime_error(server_error);
            }
            std::cout << test.message << '\n';
        } else {
            if (!run_receiver_server(options, stats, error)) {
                throw std::runtime_error(error);
            }
        }

        const auto receive_summary = stats.receive_stats.summarize();
        const auto render_summary = stats.render_stats.summarize();
        std::cout << "\n接收统计\n";
        std::cout << "instances=" << stats.total_instances
                  << " bytes=" << medicaldemo::pretty_bytes(stats.total_bytes)
                  << " avg_receive_ms=" << receive_summary.avg_ms
                  << " avg_render_ms=" << render_summary.avg_ms << '\n';
        std::cout << "最新预览: " << (options.output_dir / "preview" / "latest.pgm") << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "dicom_receiver 失败: " << ex.what() << '\n';
        return 1;
    }
}
