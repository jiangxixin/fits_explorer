#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <optional>

struct ThumbResult {
    int width;
    int height;
    std::vector<uint8_t> pixels;  // RGBA, 8-bit per channel
};

class ThumbnailGenerator {
public:
    ThumbnailGenerator() = default;

    // Generate thumbnail from a FITS file's HDU
    // Returns {} on failure
    std::optional<ThumbResult> generate(const std::string& fits_path,
                                        int hdu_index,
                                        int max_size = 128);

    // Downsample float array to 8-bit RGBA using histogram stretch
    static std::vector<uint8_t> float_to_rgba(const float* data,
                                                int w, int h,
                                                int max_size);

private:
    static float median_of_floats(const float* arr, size_t n);
    static float percentile(const float* arr, size_t n, float p);
};
