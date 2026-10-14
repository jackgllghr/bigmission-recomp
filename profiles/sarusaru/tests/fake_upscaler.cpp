// Stands in for realesrgan-ncnn-vulkan in tests: the same command line
// (-i DIR -o DIR -n MODEL -s SCALE -m MODELS -f png), enlarging each PNG with
// nearest-neighbour so the results are exact. Appends "MODEL SCALE" to
// DIR/../calls.txt for the test to check.
#include "textures/image.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char **argv) {
    std::filesystem::path input, output, models;
    std::string model;
    std::uint32_t scale = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view flag = argv[i];
        if (flag == "-i") input = argv[i + 1];
        else if (flag == "-o") output = argv[i + 1];
        else if (flag == "-n") model = argv[i + 1];
        else if (flag == "-s") scale = static_cast<std::uint32_t>(std::stoul(argv[i + 1]));
        else if (flag == "-m") models = argv[i + 1];
    }
    if (input.empty() || output.empty() || model.empty() || scale == 0u || models.empty()) {
        std::cerr << "fake upscaler: bad command line\n";
        return 2;
    }
    std::ofstream(output.parent_path() / "calls.txt", std::ios::app) << model << ' ' << scale << '\n';
    for (const auto &entry : std::filesystem::directory_iterator(input)) {
        auto image = sarusaru::textures::load_png(entry.path());
        if (!image) {
            std::cerr << "fake upscaler: cannot read " << entry.path() << '\n';
            return 1;
        }
        if (!sarusaru::textures::save_png(output / entry.path().filename(), sarusaru::textures::resize_nearest(*image, scale), true))
            return 1;
    }
    return 0;
}
