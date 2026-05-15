#include "demo_support.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    int displays = 3;
    int frames = 12;
    std::string modality = "CT";
    std::filesystem::path output_dir = std::filesystem::path("output") / "multi_display_demo";
};

struct WorkerMetrics {
    double render_ms = 0.0;
    double skew_us = 0.0;
    std::size_t bytes_written = 0;
    std::string error;
};

struct SharedState {
    std::mutex mutex;
    std::condition_variable frame_ready;
    std::condition_variable frame_done;
    bool stop = false;
    int generation = 0;
    int completed = 0;
    int frame_index = 0;
    std::chrono::steady_clock::time_point target_time{};
    medicaldemo::DicomImage image;
    medicaldemo::RecognitionResult recognition;
    std::vector<WorkerMetrics> worker_metrics;
};

Options parse_options(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--displays" && i + 1 < argc) {
            options.displays = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--frames" && i + 1 < argc) {
            options.frames = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "--modality" && i + 1 < argc) {
            options.modality = argv[++i];
        } else if (arg == "--output-dir" && i + 1 < argc) {
            options.output_dir = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: multi_display_demo [--displays N] [--frames N] [--modality CT|MR|DX|US] [--output-dir dir]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("未知参数: " + arg);
        }
    }
    return options;
}

void worker_loop(int display_index, const std::filesystem::path& output_dir, SharedState& shared) {
    int seen_generation = 0;
    while (true) {
        medicaldemo::DicomImage image;
        medicaldemo::RecognitionResult recognition;
        int frame_index = 0;
        std::chrono::steady_clock::time_point target_time;

        {
            std::unique_lock<std::mutex> lock(shared.mutex);
            shared.frame_ready.wait(lock, [&] { return shared.stop || shared.generation > seen_generation; });
            if (shared.stop) {
                return;
            }
            seen_generation = shared.generation;
            image = shared.image;
            recognition = shared.recognition;
            frame_index = shared.frame_index;
            target_time = shared.target_time;
        }

        std::this_thread::sleep_until(target_time);
        const auto render_begin = std::chrono::steady_clock::now();
        std::string error;
        const auto metrics = medicaldemo::render_preview(
            image,
            recognition,
            output_dir / ("display_" + std::to_string(display_index)),
            frame_index,
            true,
            error);
        const auto render_end = std::chrono::steady_clock::now();

        WorkerMetrics worker_metrics;
        worker_metrics.render_ms = std::chrono::duration<double, std::milli>(render_end - render_begin).count();
        worker_metrics.skew_us = std::chrono::duration<double, std::micro>(render_begin - target_time).count();
        worker_metrics.bytes_written = metrics.bytes_written;
        worker_metrics.error = error;

        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.worker_metrics[display_index] = worker_metrics;
            ++shared.completed;
        }
        shared.frame_done.notify_one();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parse_options(argc, argv);
        std::string error;
        if (!medicaldemo::ensure_directory(options.output_dir, error)) {
            throw std::runtime_error(error);
        }

        SharedState shared;
        shared.worker_metrics.resize(static_cast<std::size_t>(options.displays));

        std::vector<std::thread> workers;
        for (int i = 0; i < options.displays; ++i) {
            workers.emplace_back(worker_loop, i, options.output_dir, std::ref(shared));
        }

        medicaldemo::PerformanceStats render_stats;
        medicaldemo::PerformanceStats skew_stats;
        std::size_t total_bytes = 0;

        const auto wall_begin = std::chrono::steady_clock::now();
        for (int frame_index = 0; frame_index < options.frames; ++frame_index) {
            medicaldemo::DicomImage image = medicaldemo::make_synthetic_image(options.modality, 512, 512, frame_index);
            medicaldemo::RecognitionResult recognition = medicaldemo::recognize_image(image);
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.image = std::move(image);
                shared.recognition = recognition;
                shared.frame_index = frame_index;
                shared.completed = 0;
                shared.target_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(12);
                ++shared.generation;
            }
            shared.frame_ready.notify_all();

            {
                std::unique_lock<std::mutex> lock(shared.mutex);
                shared.frame_done.wait(lock, [&] { return shared.completed == options.displays; });

                double min_skew = shared.worker_metrics.front().skew_us;
                double max_skew = shared.worker_metrics.front().skew_us;
                double max_render = 0.0;
                for (const auto& metrics : shared.worker_metrics) {
                    if (!metrics.error.empty()) {
                        throw std::runtime_error(metrics.error);
                    }
                    min_skew = std::min(min_skew, metrics.skew_us);
                    max_skew = std::max(max_skew, metrics.skew_us);
                    max_render = std::max(max_render, metrics.render_ms);
                    total_bytes += metrics.bytes_written;
                }
                render_stats.add(max_render);
                skew_stats.add((max_skew - min_skew) / 1000.0);
                std::cout << "Frame " << std::setw(3) << frame_index
                          << " displays=" << options.displays
                          << " sync_skew_us=" << std::fixed << std::setprecision(2) << (max_skew - min_skew)
                          << '\n';
            }
        }
        const auto wall_end = std::chrono::steady_clock::now();
        const double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_begin).count();

        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.stop = true;
        }
        shared.frame_ready.notify_all();
        for (auto& worker : workers) {
            worker.join();
        }

        const auto render_summary = render_stats.summarize(wall_ms);
        const auto skew_summary = skew_stats.summarize();

        std::cout << "\n多屏同步统计\n";
        std::cout << "frame_count=" << options.frames
                  << " avg_render_ms=" << render_summary.avg_ms
                  << " p95_render_ms=" << render_summary.p95_ms
                  << " fps=" << render_summary.fps << '\n';
        std::cout << "avg_sync_skew_us=" << skew_summary.avg_ms * 1000.0
                  << " max_sync_skew_us=" << skew_summary.max_ms * 1000.0 << '\n';
        std::cout << "输出数据量=" << medicaldemo::pretty_bytes(total_bytes) << '\n';
        std::cout << "输出目录=" << options.output_dir << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "multi_display_demo 失败: " << ex.what() << '\n';
        return 1;
    }
}
