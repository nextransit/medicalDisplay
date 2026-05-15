#include "demo_support.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using medicaldemo::DicomImage;
using medicaldemo::LatencySummary;
using medicaldemo::PerformanceStats;
using medicaldemo::RecognitionResult;

namespace {

struct Options {
    std::string input_path;
    std::string modality = "CT";
    std::filesystem::path output_dir = std::filesystem::path("output") / "medical_display_demo";
    int frames = 8;
    int width = 512;
    int height = 512;
    bool realtime = false;
};

Options parse_options(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            options.input_path = argv[++i];
        } else if (arg == "--output-dir" && i + 1 < argc) {
            options.output_dir = argv[++i];
        } else if (arg == "--modality" && i + 1 < argc) {
            options.modality = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            options.frames = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--width" && i + 1 < argc) {
            options.width = std::max(64, std::stoi(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            options.height = std::max(64, std::stoi(argv[++i]));
        } else if (arg == "--realtime") {
            options.realtime = true;
        } else if (arg == "--help") {
            std::cout
                << "Usage: medical_display_demo [--input path] [--modality CT|MR|DX|US|PT]\n"
                << "                            [--frames N] [--width N] [--height N]\n"
                << "                            [--output-dir dir] [--realtime]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("未知参数: " + arg);
        }
    }
    return options;
}

void print_summary(const char* title, const LatencySummary& summary) {
    std::cout << std::left << std::setw(16) << title
              << " count=" << std::setw(3) << summary.count
              << " avg=" << std::setw(8) << std::fixed << std::setprecision(3) << summary.avg_ms << "ms"
              << " p50=" << std::setw(8) << summary.p50_ms << "ms"
              << " p95=" << std::setw(8) << summary.p95_ms << "ms"
              << " max=" << std::setw(8) << summary.max_ms << "ms";
    if (summary.fps > 0.0) {
        std::cout << " fps=" << std::setprecision(2) << summary.fps;
    }
    std::cout << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parse_options(argc, argv);
        std::string error;
        if (!medicaldemo::ensure_directory(options.output_dir, error)) {
            throw std::runtime_error(error);
        }

        std::cout << "AI Adaptive Medical Display Demo\n";
        std::cout << "启动时间: " << medicaldemo::now_local_string() << '\n';
        std::cout << "输出目录: " << options.output_dir << '\n';

        PerformanceStats load_stats;
        PerformanceStats inference_stats;
        PerformanceStats render_stats;
        std::size_t bytes_written = 0;

        DicomImage seed_image;
        const auto load_begin = std::chrono::steady_clock::now();
        if (!options.input_path.empty()) {
            if (!medicaldemo::load_dicom_file(options.input_path, seed_image, error)) {
                throw std::runtime_error(error);
            }
        } else {
            seed_image = medicaldemo::make_synthetic_image(options.modality, options.width, options.height, 0);
            const auto synthetic_path = options.output_dir / "synthetic_input.dcm";
            if (!medicaldemo::save_dicom_file(synthetic_path, seed_image, error)) {
                throw std::runtime_error(error);
            }
            std::cout << "生成示例 DICOM: " << synthetic_path << '\n';
        }
        const auto load_end = std::chrono::steady_clock::now();
        load_stats.add(std::chrono::duration<double, std::milli>(load_end - load_begin).count());

        std::cout << "输入模态: " << seed_image.modality
                  << " 尺寸: " << seed_image.width << "x" << seed_image.height
                  << " bits=" << seed_image.bits_stored << '\n';

        const auto wall_begin = std::chrono::steady_clock::now();
        for (int frame_index = 0; frame_index < options.frames; ++frame_index) {
            DicomImage frame = options.input_path.empty()
                ? medicaldemo::make_synthetic_image(seed_image.modality, seed_image.width, seed_image.height, frame_index)
                : seed_image;

            const RecognitionResult result = medicaldemo::recognize_image(frame);
            inference_stats.add(result.inference_ms);

            const auto frame_output = options.output_dir / "rendered";
            const auto render_result = medicaldemo::render_preview(frame, result, frame_output, frame_index, true, error);
            if (!error.empty()) {
                throw std::runtime_error(error);
            }
            render_stats.add(render_result.render_ms);
            bytes_written += render_result.bytes_written;

            std::cout << "Frame " << std::setw(3) << frame_index
                      << " modality=" << std::setw(2) << result.modality
                      << " confidence=" << std::fixed << std::setprecision(3) << result.confidence
                      << " wc/ww=" << result.window_center << "/" << result.window_width
                      << " color=" << result.color_space
                      << " render=" << render_result.render_ms << "ms\n";

            if (options.realtime) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        }
        const auto wall_end = std::chrono::steady_clock::now();
        const double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_begin).count();

        const LatencySummary load_summary = load_stats.summarize();
        const LatencySummary inference_summary = inference_stats.summarize(wall_ms);
        const LatencySummary render_summary = render_stats.summarize(wall_ms);

        std::cout << "\n性能统计\n";
        print_summary("Load", load_summary);
        print_summary("Inference", inference_summary);
        print_summary("Render", render_summary);
        std::cout << "输出数据量: " << medicaldemo::pretty_bytes(bytes_written) << '\n';
        std::cout << "最新预览: " << (options.output_dir / "rendered" / "latest.pgm") << '\n';

        std::ofstream summary_file(options.output_dir / "summary.txt");
        summary_file << "medical_display_demo\n";
        summary_file << "generated_at=" << medicaldemo::now_local_string() << '\n';
        summary_file << "frames=" << options.frames << '\n';
        summary_file << "modality=" << seed_image.modality << '\n';
        summary_file << "bytes_written=" << bytes_written << '\n';
        summary_file << "inference_avg_ms=" << inference_summary.avg_ms << '\n';
        summary_file << "render_avg_ms=" << render_summary.avg_ms << '\n';
        summary_file << "fps=" << render_summary.fps << '\n';

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "medical_display_demo 失败: " << ex.what() << '\n';
        return 1;
    }
}
