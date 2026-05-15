#include "demo_support.h"

extern "C" {
#include "dicom_gsdf.h"
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    int lut_size = 1024;
    int bit_depth = 12;
    int patches = 21;
    float ambient = 8.0f;
    float max_luminance = 500.0f;
    float noise = 0.012f;
    float drift = 0.025f;
    std::filesystem::path output_dir = std::filesystem::path("output") / "gsdf_calibration_demo";
};

struct PatchMeasurement {
    float pvalue = 0.0f;
    float target_luminance = 0.0f;
    float measured_luminance = 0.0f;
    float delta_jnd = 0.0f;
};

class MockColorimeter {
public:
    MockColorimeter(float noise, float drift)
        : noise_(noise), drift_(drift), generator_(std::random_device{}()), distribution_(-1.0f, 1.0f) {}

    float measure(float target, int index) {
        const float long_term = 1.0f + drift_ * std::sin(static_cast<float>(index) * 0.33f);
        const float random = 1.0f + noise_ * distribution_(generator_);
        return std::max(0.001f, target * long_term * random);
    }

private:
    float noise_;
    float drift_;
    std::mt19937 generator_;
    std::uniform_real_distribution<float> distribution_;
};

Options parse_options(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--lut-size" && i + 1 < argc) {
            options.lut_size = std::max(64, std::stoi(argv[++i]));
        } else if (arg == "--bit-depth" && i + 1 < argc) {
            options.bit_depth = std::max(8, std::min(16, std::stoi(argv[++i])));
        } else if (arg == "--patches" && i + 1 < argc) {
            options.patches = std::max(5, std::stoi(argv[++i]));
        } else if (arg == "--ambient" && i + 1 < argc) {
            options.ambient = std::stof(argv[++i]);
        } else if (arg == "--max-luminance" && i + 1 < argc) {
            options.max_luminance = std::stof(argv[++i]);
        } else if (arg == "--noise" && i + 1 < argc) {
            options.noise = std::stof(argv[++i]);
        } else if (arg == "--drift" && i + 1 < argc) {
            options.drift = std::stof(argv[++i]);
        } else if (arg == "--output-dir" && i + 1 < argc) {
            options.output_dir = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: gsdf_calibration_demo [--lut-size N] [--bit-depth N] [--patches N]\n"
                         "                              [--ambient cdm2] [--max-luminance cdm2]\n"
                         "                              [--noise f] [--drift f] [--output-dir dir]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("未知参数: " + arg);
        }
    }
    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parse_options(argc, argv);
        std::string error;
        if (!medicaldemo::ensure_directory(options.output_dir, error)) {
            throw std::runtime_error(error);
        }

        std::vector<float> lut(static_cast<std::size_t>(options.lut_size));
        if (gsdf_generate_lut(lut.data(), options.lut_size, options.bit_depth, options.ambient, options.max_luminance) != 0) {
            throw std::runtime_error("GSDF LUT 生成失败");
        }

        const bool lut_valid = gsdf_validate_lut(lut.data(), options.lut_size, 0.05f);
        const float theoretical_delta_jnd = gsdf_calculate_delta_jnd(lut.data(), options.lut_size);

        MockColorimeter colorimeter(options.noise, options.drift);
        std::vector<PatchMeasurement> measurements;
        measurements.reserve(static_cast<std::size_t>(options.patches));

        double sum_abs_delta = 0.0;
        double max_abs_delta = 0.0;
        double sum_gain = 0.0;

        for (int i = 0; i < options.patches; ++i) {
            const float pvalue = static_cast<float>(i) / static_cast<float>(options.patches - 1);
            const float target_luminance = gsdf_pvalue_to_luminance(pvalue, options.ambient);
            const float measured_luminance = colorimeter.measure(target_luminance, i);
            const float target_jnd = gsdf_luminance_to_jnd(target_luminance + options.ambient);
            const float delta_jnd = gsdf_measure_jnd_accuracy(measured_luminance, target_jnd, options.ambient);
            const float gain = target_luminance / std::max(0.001f, measured_luminance);

            measurements.push_back({pvalue, target_luminance, measured_luminance, delta_jnd});
            sum_abs_delta += std::abs(delta_jnd);
            max_abs_delta = std::max(max_abs_delta, static_cast<double>(std::abs(delta_jnd)));
            sum_gain += gain;
        }

        const double avg_abs_delta = sum_abs_delta / static_cast<double>(measurements.size());
        const double recommended_gain = sum_gain / static_cast<double>(measurements.size());
        const bool pass = lut_valid && max_abs_delta <= 6.0;

        std::cout << "GSDF Calibration Demo\n";
        std::cout << "patches=" << measurements.size()
                  << " ambient=" << options.ambient
                  << " max_luminance=" << options.max_luminance << '\n';
        std::cout << "lut_valid=" << (lut_valid ? "true" : "false")
                  << " theoretical_delta_jnd=" << theoretical_delta_jnd
                  << " avg_abs_delta_jnd=" << avg_abs_delta
                  << " max_abs_delta_jnd=" << max_abs_delta << '\n';
        std::cout << "recommended_gain=" << recommended_gain
                  << " calibration_result=" << (pass ? "PASS" : "REVIEW") << '\n';

        std::ofstream report(options.output_dir / "calibration_report.json");
        report << "{\n";
        report << "  \"generated_at\": \"" << medicaldemo::now_local_string() << "\",\n";
        report << "  \"lut_size\": " << options.lut_size << ",\n";
        report << "  \"bit_depth\": " << options.bit_depth << ",\n";
        report << "  \"ambient\": " << options.ambient << ",\n";
        report << "  \"max_luminance\": " << options.max_luminance << ",\n";
        report << "  \"lut_valid\": " << (lut_valid ? "true" : "false") << ",\n";
        report << "  \"theoretical_delta_jnd\": " << theoretical_delta_jnd << ",\n";
        report << "  \"avg_abs_delta_jnd\": " << avg_abs_delta << ",\n";
        report << "  \"max_abs_delta_jnd\": " << max_abs_delta << ",\n";
        report << "  \"recommended_gain\": " << recommended_gain << ",\n";
        report << "  \"result\": \"" << (pass ? "PASS" : "REVIEW") << "\",\n";
        report << "  \"patches\": [\n";
        for (std::size_t i = 0; i < measurements.size(); ++i) {
            const auto& patch = measurements[i];
            report << "    {\"pvalue\": " << patch.pvalue
                   << ", \"target\": " << patch.target_luminance
                   << ", \"measured\": " << patch.measured_luminance
                   << ", \"delta_jnd\": " << patch.delta_jnd << "}";
            if (i + 1 != measurements.size()) {
                report << ",";
            }
            report << "\n";
        }
        report << "  ]\n";
        report << "}\n";

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "gsdf_calibration_demo 失败: " << ex.what() << '\n';
        return 1;
    }
}
