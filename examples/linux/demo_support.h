#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace medicaldemo {

struct DicomImage {
    std::string source_path;
    std::string patient_name = "SYNTHETIC^PATIENT";
    std::string patient_id = "DEMO-0001";
    std::string study_description = "Synthetic Study";
    std::string series_description = "Synthetic Series";
    std::string modality = "CT";
    std::string sop_instance_uid;
    int width = 0;
    int height = 0;
    int bits_allocated = 16;
    int bits_stored = 12;
    float window_center = 40.0f;
    float window_width = 400.0f;
    float rescale_slope = 1.0f;
    float rescale_intercept = 0.0f;
    std::vector<uint16_t> pixels;
};

struct RecognitionResult {
    std::string modality = "UNKNOWN";
    double confidence = 0.0;
    double inference_ms = 0.0;
    float window_center = 40.0f;
    float window_width = 400.0f;
    double gamma = 2.2;
    bool gsdf_enabled = false;
    std::string color_space = "sRGB";
    bool hdr_enabled = false;
    std::string enhancement = "none";
};

struct RenderMetrics {
    double render_ms = 0.0;
    std::size_t bytes_written = 0;
};

struct LatencySummary {
    std::size_t count = 0;
    double avg_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double max_ms = 0.0;
    double fps = 0.0;
};

class PerformanceStats {
public:
    void add(double milliseconds);
    LatencySummary summarize(double wall_ms = 0.0) const;
    const std::vector<double>& samples() const;

private:
    std::vector<double> samples_;
};

std::string now_local_string();
std::string random_uid();
std::string sanitize_filename(const std::string& value);
std::string pretty_bytes(std::size_t bytes);
std::string hex32(std::uint32_t value);
std::string modality_to_sop_class(const std::string& modality);
bool ensure_directory(const std::filesystem::path& dir, std::string& error);

DicomImage make_synthetic_image(const std::string& modality, int width, int height, int frame_index = 0);
RecognitionResult recognize_image(const DicomImage& image);

bool save_dicom_file(const std::filesystem::path& path, const DicomImage& image, std::string& error);
bool save_part10_from_dataset(const std::filesystem::path& path,
                              const std::vector<std::uint8_t>& dataset_bytes,
                              const std::string& sop_class_uid,
                              const std::string& sop_instance_uid,
                              const std::string& transfer_syntax_uid,
                              std::string& error);
bool load_dicom_file(const std::filesystem::path& path, DicomImage& image, std::string& error);
std::vector<std::uint8_t> encode_explicit_little_endian_dataset(const DicomImage& image);

RenderMetrics render_preview(const DicomImage& image,
                             const RecognitionResult& result,
                             const std::filesystem::path& output_dir,
                             int frame_index,
                             bool write_latest,
                             std::string& error);

std::uint32_t fnv1a32(const std::vector<std::uint8_t>& bytes);
std::uint32_t fnv1a32_file(const std::filesystem::path& path, std::string& error);
bool copy_file_with_progress(const std::filesystem::path& source,
                             const std::filesystem::path& destination,
                             std::size_t chunk_size,
                             const std::function<void(std::size_t, std::size_t)>& on_progress,
                             std::string& error);

}  // namespace medicaldemo
