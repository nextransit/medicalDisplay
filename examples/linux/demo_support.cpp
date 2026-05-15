#include "demo_support.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>

namespace medicaldemo {

namespace {

constexpr const char* kTransferSyntaxExplicitLittle = "1.2.840.10008.1.2.1";
constexpr const char* kTransferSyntaxImplicitLittle = "1.2.840.10008.1.2";
constexpr const char* kImplementationClassUid = "1.2.826.0.1.3680043.10.5432.1";

struct ParsedElement {
    std::uint16_t group = 0;
    std::uint16_t element = 0;
    std::string vr;
    std::uint32_t length = 0;
    std::size_t value_offset = 0;
};

std::uint16_t read_le16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] | (static_cast<std::uint16_t>(data[offset + 1]) << 8));
}

std::uint32_t read_le32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
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

bool vr_uses_32bit_length(const std::string& vr) {
    return vr == "OB" || vr == "OD" || vr == "OF" || vr == "OL" ||
           vr == "OW" || vr == "SQ" || vr == "UC" || vr == "UR" ||
           vr == "UT" || vr == "UN";
}

std::string trim_dicom_text(std::string value) {
    while (!value.empty() && (value.back() == '\0' || value.back() == ' ' || value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }
    std::size_t first = 0;
    while (first < value.size() && value[first] == ' ') {
        ++first;
    }
    if (first > 0) {
        value.erase(0, first);
    }
    return value;
}

std::string first_component(const std::string& value) {
    const std::size_t pos = value.find('\\');
    return trim_dicom_text(pos == std::string::npos ? value : value.substr(0, pos));
}

std::string pad_even(const std::string& value, char pad) {
    std::string out = value;
    if (out.size() % 2 != 0) {
        out.push_back(pad);
    }
    return out;
}

void append_explicit_element(std::vector<std::uint8_t>& out,
                             std::uint16_t group,
                             std::uint16_t element,
                             const std::string& vr,
                             const std::uint8_t* bytes,
                             std::uint32_t length) {
    append_le16(out, group);
    append_le16(out, element);
    out.push_back(static_cast<std::uint8_t>(vr[0]));
    out.push_back(static_cast<std::uint8_t>(vr[1]));
    if (vr_uses_32bit_length(vr)) {
        out.push_back(0);
        out.push_back(0);
        append_le32(out, length);
    } else {
        append_le16(out, static_cast<std::uint16_t>(length));
    }
    out.insert(out.end(), bytes, bytes + length);
}

void append_explicit_string(std::vector<std::uint8_t>& out,
                            std::uint16_t group,
                            std::uint16_t element,
                            const std::string& vr,
                            const std::string& value) {
    const char pad = vr == "UI" ? '\0' : ' ';
    const std::string even = pad_even(value, pad);
    append_explicit_element(out, group, element, vr,
                            reinterpret_cast<const std::uint8_t*>(even.data()),
                            static_cast<std::uint32_t>(even.size()));
}

void append_explicit_us(std::vector<std::uint8_t>& out,
                        std::uint16_t group,
                        std::uint16_t element,
                        std::uint16_t value) {
    std::array<std::uint8_t, 2> raw{
        static_cast<std::uint8_t>(value & 0xFFu),
        static_cast<std::uint8_t>((value >> 8) & 0xFFu),
    };
    append_explicit_element(out, group, element, "US", raw.data(), static_cast<std::uint32_t>(raw.size()));
}

void append_explicit_ds(std::vector<std::uint8_t>& out,
                        std::uint16_t group,
                        std::uint16_t element,
                        double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    append_explicit_string(out, group, element, "DS", stream.str());
}

bool parse_explicit_or_implicit_element(const std::vector<std::uint8_t>& data,
                                        std::size_t& offset,
                                        bool explicit_vr,
                                        ParsedElement& element,
                                        std::string& error) {
    if (offset + 8 > data.size()) {
        error = "DICOM 元素头不足";
        return false;
    }

    element.group = read_le16(data, offset);
    element.element = read_le16(data, offset + 2);
    offset += 4;

    if (explicit_vr) {
        if (offset + 4 > data.size()) {
            error = "显式 VR 长度字段不足";
            return false;
        }
        element.vr.assign(reinterpret_cast<const char*>(&data[offset]), 2);
        offset += 2;
        if (vr_uses_32bit_length(element.vr)) {
            offset += 2;
            if (offset + 4 > data.size()) {
                error = "显式 VR 32-bit 长度字段不足";
                return false;
            }
            element.length = read_le32(data, offset);
            offset += 4;
        } else {
            element.length = read_le16(data, offset);
            offset += 2;
        }
    } else {
        element.vr.clear();
        if (offset + 4 > data.size()) {
            error = "隐式 VR 长度字段不足";
            return false;
        }
        element.length = read_le32(data, offset);
        offset += 4;
    }

    if (element.length == 0xFFFFFFFFu) {
        error = "示例解析器不支持未定长元素";
        return false;
    }

    if (offset + element.length > data.size()) {
        error = "DICOM 元素越界";
        return false;
    }

    element.value_offset = offset;
    offset += element.length;
    return true;
}

std::string element_string(const std::vector<std::uint8_t>& data, const ParsedElement& element) {
    return trim_dicom_text(std::string(reinterpret_cast<const char*>(&data[element.value_offset]), element.length));
}

float element_float(const std::vector<std::uint8_t>& data, const ParsedElement& element, float fallback) {
    try {
        return std::stof(first_component(element_string(data, element)));
    } catch (...) {
        return fallback;
    }
}

int modality_rank(const std::string& modality) {
    if (modality == "CT") return 1;
    if (modality == "MR") return 2;
    if (modality == "DX") return 3;
    if (modality == "CR") return 4;
    if (modality == "US") return 5;
    if (modality == "PT") return 6;
    if (modality == "XA") return 7;
    if (modality == "SM") return 8;
    return 0;
}

std::uint16_t clamp_u12(double value) {
    value = std::max(0.0, std::min(4095.0, value));
    return static_cast<std::uint16_t>(std::lround(value));
}

std::vector<std::uint8_t> build_file_meta(const std::string& sop_class_uid,
                                          const std::string& sop_instance_uid,
                                          const std::string& transfer_syntax_uid) {
    std::vector<std::uint8_t> body;
    std::array<std::uint8_t, 2> version{{0x00, 0x01}};
    append_explicit_element(body, 0x0002, 0x0001, "OB", version.data(), static_cast<std::uint32_t>(version.size()));
    append_explicit_string(body, 0x0002, 0x0002, "UI", sop_class_uid);
    append_explicit_string(body, 0x0002, 0x0003, "UI", sop_instance_uid);
    append_explicit_string(body, 0x0002, 0x0010, "UI", transfer_syntax_uid);
    append_explicit_string(body, 0x0002, 0x0012, "UI", kImplementationClassUid);

    std::vector<std::uint8_t> meta;
    append_le16(meta, 0x0002);
    append_le16(meta, 0x0000);
    meta.push_back('U');
    meta.push_back('L');
    append_le16(meta, 4);
    append_le32(meta, static_cast<std::uint32_t>(body.size()));
    meta.insert(meta.end(), body.begin(), body.end());
    return meta;
}

bool write_pgm(const std::filesystem::path& path,
               int width,
               int height,
               const std::vector<std::uint8_t>& pixels,
               std::string& error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "无法写入预览文件: " + path.string();
        return false;
    }
    out << "P5\n" << width << " " << height << "\n255\n";
    out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    if (!out) {
        error = "写入预览文件失败: " + path.string();
        return false;
    }
    return true;
}

}  // namespace

void PerformanceStats::add(double milliseconds) {
    samples_.push_back(milliseconds);
}

LatencySummary PerformanceStats::summarize(double wall_ms) const {
    LatencySummary summary;
    summary.count = samples_.size();
    if (samples_.empty()) {
        return summary;
    }

    summary.max_ms = *std::max_element(samples_.begin(), samples_.end());
    summary.avg_ms = std::accumulate(samples_.begin(), samples_.end(), 0.0) / static_cast<double>(samples_.size());

    std::vector<double> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    const auto pick = [&](double quantile) {
        const double index = quantile * static_cast<double>(sorted.size() - 1);
        return sorted[static_cast<std::size_t>(std::round(index))];
    };
    summary.p50_ms = pick(0.50);
    summary.p95_ms = pick(0.95);
    if (wall_ms > 0.0) {
        summary.fps = static_cast<double>(samples_.size()) / (wall_ms / 1000.0);
    }
    return summary;
}

const std::vector<double>& PerformanceStats::samples() const {
    return samples_;
}

std::string now_local_string() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream stream;
    stream << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::string random_uid() {
    static thread_local std::mt19937_64 generator(std::random_device{}());
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    const auto entropy = generator();
    std::ostringstream stream;
    stream << "1.2.826.0.1.3680043.10.5432." << micros << "." << (entropy % 1000000000ULL);
    return stream.str();
}

std::string sanitize_filename(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (const char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.') {
            output.push_back(ch);
        } else {
            output.push_back('_');
        }
    }
    if (output.empty()) {
        output = "unnamed";
    }
    return output;
}

std::string pretty_bytes(std::size_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    std::size_t unit_index = 0;
    constexpr std::size_t unit_count = sizeof(units) / sizeof(units[0]);
    while (value >= 1024.0 && unit_index + 1 < unit_count) {
        value /= 1024.0;
        ++unit_index;
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(unit_index == 0 ? 0 : 2) << value << units[unit_index];
    return stream.str();
}

std::string hex32(std::uint32_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(8) << value;
    return stream.str();
}

std::string modality_to_sop_class(const std::string& modality) {
    if (modality == "CT") return "1.2.840.10008.5.1.4.1.1.2";
    if (modality == "MR") return "1.2.840.10008.5.1.4.1.1.4";
    if (modality == "US") return "1.2.840.10008.5.1.4.1.1.6.1";
    if (modality == "DX") return "1.2.840.10008.5.1.4.1.1.1.1";
    if (modality == "CR") return "1.2.840.10008.5.1.4.1.1.1";
    if (modality == "PT") return "1.2.840.10008.5.1.4.1.1.128";
    if (modality == "XA") return "1.2.840.10008.5.1.4.1.1.12.1";
    if (modality == "SM") return "1.2.840.10008.5.1.4.1.1.77.1.6";
    return "1.2.840.10008.5.1.4.1.1.7";
}

bool ensure_directory(const std::filesystem::path& dir, std::string& error) {
    if (dir.empty()) {
        return true;
    }
    std::error_code ec;
    if (std::filesystem::exists(dir, ec)) {
        if (!std::filesystem::is_directory(dir, ec)) {
            error = "路径存在但不是目录: " + dir.string();
            return false;
        }
        return true;
    }
    if (!std::filesystem::create_directories(dir, ec)) {
        error = "创建目录失败: " + dir.string() + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

DicomImage make_synthetic_image(const std::string& modality, int width, int height, int frame_index) {
    DicomImage image;
    image.modality = modality;
    image.width = width;
    image.height = height;
    image.sop_instance_uid = random_uid();
    image.series_description = "Synthetic " + modality + " Series";
    image.study_description = "Synthetic " + modality + " Study";
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    if (modality == "CT") {
        image.window_center = 40.0f;
        image.window_width = 400.0f;
        image.rescale_intercept = -1024.0f;
    } else if (modality == "MR") {
        image.window_center = 500.0f;
        image.window_width = 1000.0f;
    } else if (modality == "DX" || modality == "CR") {
        image.window_center = 2000.0f;
        image.window_width = 4000.0f;
    } else if (modality == "US") {
        image.window_center = 127.0f;
        image.window_width = 255.0f;
        image.bits_stored = 8;
    } else if (modality == "PT") {
        image.window_center = 5.0f;
        image.window_width = 10.0f;
    }

    const double cx = static_cast<double>(width) / 2.0;
    const double cy = static_cast<double>(height) / 2.0;
    const double pulse = 0.5 + 0.5 * std::sin(static_cast<double>(frame_index) * 0.35);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx = static_cast<double>(x) - cx;
            const double dy = static_cast<double>(y) - cy;
            const double radius = std::sqrt(dx * dx + dy * dy);
            const double angle = std::atan2(dy, dx);
            double value = 0.0;

            if (modality == "CT") {
                value = 1100.0 + radius * 1.4;
                if (radius < width * 0.18) value = 3200.0 - radius * 8.0;
                if (radius > width * 0.22 && radius < width * 0.34) value = 600.0;
                value += 250.0 * std::sin(angle * 5.0 + frame_index * 0.2);
            } else if (modality == "MR") {
                value = 900.0 + 350.0 * std::sin(radius * 0.06 + frame_index * 0.08);
                value += 180.0 * std::cos(angle * 3.0);
            } else if (modality == "DX" || modality == "CR") {
                value = 1000.0 + (static_cast<double>(x) / width) * 1800.0;
                if (std::abs(dx) < width * 0.03 || std::abs(dy) < height * 0.03) value += 800.0;
                value += 120.0 * pulse;
            } else if (modality == "US") {
                const double speckle = std::sin(x * 0.17 + frame_index * 0.11) * std::cos(y * 0.13);
                value = 80.0 + 90.0 * std::exp(-(radius * radius) / (width * height * 0.025));
                value += 50.0 * speckle;
            } else if (modality == "PT") {
                value = 300.0 + 1500.0 * std::exp(-(radius * radius) / (width * height * 0.02));
                value += 400.0 * pulse;
            } else {
                value = 1024.0 + 256.0 * std::sin(radius * 0.04 + frame_index * 0.1);
            }

            image.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                clamp_u12(value);
        }
    }

    return image;
}

RecognitionResult recognize_image(const DicomImage& image) {
    const auto begin = std::chrono::steady_clock::now();
    RecognitionResult result;

    if (!image.modality.empty() && modality_rank(image.modality) > 0) {
        result.modality = image.modality;
        result.confidence = 0.985;
    } else {
        const auto [min_it, max_it] = std::minmax_element(image.pixels.begin(), image.pixels.end());
        const std::uint16_t min_value = min_it == image.pixels.end() ? 0 : *min_it;
        const std::uint16_t max_value = max_it == image.pixels.end() ? 0 : *max_it;
        if (image.rescale_intercept < -500.0f || max_value - min_value > 2500) {
            result.modality = "CT";
            result.confidence = 0.91;
        } else if (image.bits_stored <= 8) {
            result.modality = "US";
            result.confidence = 0.88;
        } else {
            result.modality = "MR";
            result.confidence = 0.80;
        }
    }

    result.window_center = image.window_center;
    result.window_width = image.window_width;
    result.gamma = 2.2;
    result.gsdf_enabled = false;
    result.color_space = "sRGB";
    result.hdr_enabled = false;
    result.enhancement = "soft-tissue";

    if (result.modality == "CT") {
        result.window_center = image.window_center != 0.0f ? image.window_center : 40.0f;
        result.window_width = image.window_width != 0.0f ? image.window_width : 400.0f;
        result.gsdf_enabled = true;
        result.color_space = "DICOM_GSDF";
        result.gamma = 2.0;
        result.enhancement = "bone-lung adaptive";
    } else if (result.modality == "MR") {
        result.window_center = image.window_center != 0.0f ? image.window_center : 500.0f;
        result.window_width = image.window_width != 0.0f ? image.window_width : 1000.0f;
        result.gsdf_enabled = true;
        result.color_space = "DICOM_GSDF";
        result.enhancement = "soft-tissue contrast";
    } else if (result.modality == "DX" || result.modality == "CR") {
        result.window_center = 2000.0f;
        result.window_width = 4000.0f;
        result.gsdf_enabled = true;
        result.color_space = "DICOM_GSDF";
        result.enhancement = "edge enhancement";
    } else if (result.modality == "US") {
        result.window_center = 127.0f;
        result.window_width = 255.0f;
        result.enhancement = "speckle-preserving";
    } else if (result.modality == "PT") {
        result.window_center = 5.0f;
        result.window_width = 10.0f;
        result.hdr_enabled = true;
        result.color_space = "DCI-P3";
        result.enhancement = "hotspot emphasis";
    }

    const auto end = std::chrono::steady_clock::now();
    result.inference_ms = std::chrono::duration<double, std::milli>(end - begin).count();
    return result;
}

std::vector<std::uint8_t> encode_explicit_little_endian_dataset(const DicomImage& image) {
    std::vector<std::uint8_t> out;

    const std::string sop_class_uid = modality_to_sop_class(image.modality);
    const std::string sop_instance_uid = image.sop_instance_uid.empty() ? random_uid() : image.sop_instance_uid;

    append_explicit_string(out, 0x0008, 0x0016, "UI", sop_class_uid);
    append_explicit_string(out, 0x0008, 0x0018, "UI", sop_instance_uid);
    append_explicit_string(out, 0x0008, 0x0060, "CS", image.modality);
    append_explicit_string(out, 0x0010, 0x0010, "PN", image.patient_name);
    append_explicit_string(out, 0x0010, 0x0020, "LO", image.patient_id);
    append_explicit_string(out, 0x0008, 0x1030, "LO", image.study_description);
    append_explicit_string(out, 0x0008, 0x103E, "LO", image.series_description);
    append_explicit_us(out, 0x0028, 0x0002, 1);
    append_explicit_string(out, 0x0028, 0x0004, "CS", "MONOCHROME2");
    append_explicit_us(out, 0x0028, 0x0010, static_cast<std::uint16_t>(image.height));
    append_explicit_us(out, 0x0028, 0x0011, static_cast<std::uint16_t>(image.width));
    append_explicit_us(out, 0x0028, 0x0100, static_cast<std::uint16_t>(image.bits_allocated));
    append_explicit_us(out, 0x0028, 0x0101, static_cast<std::uint16_t>(image.bits_stored));
    append_explicit_us(out, 0x0028, 0x0102, static_cast<std::uint16_t>(image.bits_stored - 1));
    append_explicit_us(out, 0x0028, 0x0103, 0);
    append_explicit_ds(out, 0x0028, 0x1050, image.window_center);
    append_explicit_ds(out, 0x0028, 0x1051, image.window_width);
    append_explicit_ds(out, 0x0028, 0x1052, image.rescale_intercept);
    append_explicit_ds(out, 0x0028, 0x1053, image.rescale_slope);

    std::vector<std::uint8_t> pixel_bytes;
    pixel_bytes.reserve(image.pixels.size() * 2);
    for (const std::uint16_t pixel : image.pixels) {
        pixel_bytes.push_back(static_cast<std::uint8_t>(pixel & 0xFFu));
        pixel_bytes.push_back(static_cast<std::uint8_t>((pixel >> 8) & 0xFFu));
    }
    append_explicit_element(out, 0x7FE0, 0x0010, "OW", pixel_bytes.data(), static_cast<std::uint32_t>(pixel_bytes.size()));
    return out;
}

bool save_part10_from_dataset(const std::filesystem::path& path,
                              const std::vector<std::uint8_t>& dataset_bytes,
                              const std::string& sop_class_uid,
                              const std::string& sop_instance_uid,
                              const std::string& transfer_syntax_uid,
                              std::string& error) {
    if (!ensure_directory(path.parent_path(), error)) {
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "无法创建 DICOM 文件: " + path.string();
        return false;
    }

    std::array<char, 128> preamble{};
    out.write(preamble.data(), static_cast<std::streamsize>(preamble.size()));
    out.write("DICM", 4);

    const std::vector<std::uint8_t> meta = build_file_meta(sop_class_uid, sop_instance_uid, transfer_syntax_uid);
    out.write(reinterpret_cast<const char*>(meta.data()), static_cast<std::streamsize>(meta.size()));
    out.write(reinterpret_cast<const char*>(dataset_bytes.data()), static_cast<std::streamsize>(dataset_bytes.size()));

    if (!out) {
        error = "写入 DICOM 文件失败: " + path.string();
        return false;
    }
    return true;
}

bool save_dicom_file(const std::filesystem::path& path, const DicomImage& image, std::string& error) {
    DicomImage mutable_image = image;
    if (mutable_image.sop_instance_uid.empty()) {
        mutable_image.sop_instance_uid = random_uid();
    }
    const std::vector<std::uint8_t> dataset = encode_explicit_little_endian_dataset(mutable_image);
    return save_part10_from_dataset(path,
                                    dataset,
                                    modality_to_sop_class(mutable_image.modality),
                                    mutable_image.sop_instance_uid,
                                    kTransferSyntaxExplicitLittle,
                                    error);
}

bool load_dicom_file(const std::filesystem::path& path, DicomImage& image, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "无法打开 DICOM 文件: " + path.string();
        return false;
    }

    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() < 8) {
        error = "DICOM 文件过小: " + path.string();
        return false;
    }

    bool explicit_vr = false;
    std::size_t dataset_offset = 0;
    std::string transfer_syntax = kTransferSyntaxImplicitLittle;

    if (bytes.size() > 132 && std::memcmp(bytes.data() + 128, "DICM", 4) == 0) {
        std::size_t offset = 132;
        while (offset + 8 <= bytes.size()) {
            ParsedElement element;
            if (!parse_explicit_or_implicit_element(bytes, offset, true, element, error)) {
                return false;
            }
            if (element.group != 0x0002) {
                dataset_offset = element.value_offset - 8;
                if (vr_uses_32bit_length(element.vr)) {
                    dataset_offset -= 4;
                }
                break;
            }
            if (element.group == 0x0002 && element.element == 0x0010) {
                transfer_syntax = trim_dicom_text(std::string(
                    reinterpret_cast<const char*>(&bytes[element.value_offset]), element.length));
            }
            dataset_offset = offset;
        }
    }

    explicit_vr = transfer_syntax == kTransferSyntaxExplicitLittle;
    if (transfer_syntax != kTransferSyntaxExplicitLittle && transfer_syntax != kTransferSyntaxImplicitLittle) {
        error = "示例解析器仅支持未压缩 Little Endian 传输语法";
        return false;
    }

    image = DicomImage{};
    image.source_path = path.string();

    std::size_t offset = dataset_offset;
    while (offset + 8 <= bytes.size()) {
        ParsedElement element;
        if (!parse_explicit_or_implicit_element(bytes, offset, explicit_vr, element, error)) {
            return false;
        }

        if (element.group == 0x0010 && element.element == 0x0010) {
            image.patient_name = element_string(bytes, element);
        } else if (element.group == 0x0010 && element.element == 0x0020) {
            image.patient_id = element_string(bytes, element);
        } else if (element.group == 0x0008 && element.element == 0x0060) {
            image.modality = first_component(element_string(bytes, element));
        } else if (element.group == 0x0008 && element.element == 0x1030) {
            image.study_description = element_string(bytes, element);
        } else if (element.group == 0x0008 && element.element == 0x103E) {
            image.series_description = element_string(bytes, element);
        } else if (element.group == 0x0008 && element.element == 0x0018) {
            image.sop_instance_uid = element_string(bytes, element);
        } else if (element.group == 0x0028 && element.element == 0x0010 && element.length >= 2) {
            image.height = static_cast<int>(read_le16(bytes, element.value_offset));
        } else if (element.group == 0x0028 && element.element == 0x0011 && element.length >= 2) {
            image.width = static_cast<int>(read_le16(bytes, element.value_offset));
        } else if (element.group == 0x0028 && element.element == 0x0100 && element.length >= 2) {
            image.bits_allocated = static_cast<int>(read_le16(bytes, element.value_offset));
        } else if (element.group == 0x0028 && element.element == 0x0101 && element.length >= 2) {
            image.bits_stored = static_cast<int>(read_le16(bytes, element.value_offset));
        } else if (element.group == 0x0028 && element.element == 0x1050) {
            image.window_center = element_float(bytes, element, image.window_center);
        } else if (element.group == 0x0028 && element.element == 0x1051) {
            image.window_width = element_float(bytes, element, image.window_width);
        } else if (element.group == 0x0028 && element.element == 0x1052) {
            image.rescale_intercept = element_float(bytes, element, image.rescale_intercept);
        } else if (element.group == 0x0028 && element.element == 0x1053) {
            image.rescale_slope = element_float(bytes, element, image.rescale_slope);
        } else if (element.group == 0x7FE0 && element.element == 0x0010) {
            if (image.width <= 0 || image.height <= 0) {
                error = "像素数据之前缺少 Rows/Columns";
                return false;
            }
            image.pixels.clear();
            if (image.bits_allocated <= 8) {
                image.pixels.reserve(element.length);
                for (std::size_t i = 0; i < element.length; ++i) {
                    image.pixels.push_back(bytes[element.value_offset + i]);
                }
            } else {
                if (element.length % 2 != 0) {
                    error = "16-bit 像素数据长度非法";
                    return false;
                }
                image.pixels.reserve(element.length / 2);
                for (std::size_t i = 0; i + 1 < element.length; i += 2) {
                    image.pixels.push_back(read_le16(bytes, element.value_offset + i));
                }
            }
            break;
        }
    }

    if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
        error = "未能从 DICOM 中提取有效像素数据";
        return false;
    }

    if (image.sop_instance_uid.empty()) {
        image.sop_instance_uid = random_uid();
    }
    return true;
}

RenderMetrics render_preview(const DicomImage& image,
                             const RecognitionResult& result,
                             const std::filesystem::path& output_dir,
                             int frame_index,
                             bool write_latest,
                             std::string& error) {
    const auto begin = std::chrono::steady_clock::now();
    if (!ensure_directory(output_dir, error)) {
        return {};
    }

    if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
        error = "无可渲染的像素数据";
        return {};
    }

    const float center = result.window_center != 0.0f ? result.window_center : image.window_center;
    const float width = result.window_width > 1.0f ? result.window_width : std::max(255.0f, std::pow(2.0f, static_cast<float>(image.bits_stored)) - 1.0f);
    const float low = center - width * 0.5f;
    const float high = center + width * 0.5f;

    std::vector<std::uint8_t> preview;
    preview.reserve(image.pixels.size());
    for (const std::uint16_t raw_pixel : image.pixels) {
        float value = static_cast<float>(raw_pixel) * image.rescale_slope + image.rescale_intercept;
        float normalized = (value - low) / std::max(1.0f, high - low);
        normalized = std::max(0.0f, std::min(1.0f, normalized));
        if (result.gsdf_enabled) {
            normalized = std::pow(normalized, 0.90f);
        } else {
            normalized = std::pow(normalized, static_cast<float>(1.0 / std::max(0.1, result.gamma)));
        }
        preview.push_back(static_cast<std::uint8_t>(std::lround(normalized * 255.0f)));
    }

    std::ostringstream name_stream;
    name_stream << "frame_" << std::setfill('0') << std::setw(3) << frame_index << ".pgm";
    const std::filesystem::path frame_path = output_dir / name_stream.str();
    if (!write_pgm(frame_path, image.width, image.height, preview, error)) {
        return {};
    }

    if (write_latest) {
        const std::filesystem::path latest_path = output_dir / "latest.pgm";
        if (!write_pgm(latest_path, image.width, image.height, preview, error)) {
            return {};
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const std::size_t file_bytes = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) + 32;
    return {
        std::chrono::duration<double, std::milli>(end - begin).count(),
        file_bytes,
    };
}

std::uint32_t fnv1a32(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t hash = 2166136261u;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

std::uint32_t fnv1a32_file(const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "无法打开文件计算校验: " + path.string();
        return 0;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return fnv1a32(bytes);
}

bool copy_file_with_progress(const std::filesystem::path& source,
                             const std::filesystem::path& destination,
                             std::size_t chunk_size,
                             const std::function<void(std::size_t, std::size_t)>& on_progress,
                             std::string& error) {
    std::ifstream in(source, std::ios::binary);
    if (!in) {
        error = "无法打开源文件: " + source.string();
        return false;
    }
    if (!ensure_directory(destination.parent_path(), error)) {
        return false;
    }
    std::ofstream out(destination, std::ios::binary);
    if (!out) {
        error = "无法创建目标文件: " + destination.string();
        return false;
    }

    std::error_code ec;
    const std::size_t total = static_cast<std::size_t>(std::filesystem::file_size(source, ec));
    if (ec) {
        error = "无法获取源文件大小: " + ec.message();
        return false;
    }

    std::vector<char> buffer(std::max<std::size_t>(4096, chunk_size));
    std::size_t transferred = 0;
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read_bytes = in.gcount();
        if (read_bytes <= 0) {
            break;
        }
        out.write(buffer.data(), read_bytes);
        transferred += static_cast<std::size_t>(read_bytes);
        if (on_progress) {
            on_progress(transferred, total);
        }
    }

    if (!out) {
        error = "写入目标文件失败: " + destination.string();
        return false;
    }
    return true;
}

}  // namespace medicaldemo
