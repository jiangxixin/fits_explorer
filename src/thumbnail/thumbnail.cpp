#include "thumbnail.h"
#include <fitsio.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

// Simple downsampling: sample every Nth pixel
static void downsample_2d(const float* src, float* dst,
                           int src_w, int src_h, int dst_w, int dst_h) {
    float sx = (float)src_w / dst_w;
    float sy = (float)src_h / dst_h;
    for (int dy = 0; dy < dst_h; dy++) {
        for (int dx = 0; dx < dst_w; dx++) {
            int sx0 = (int)(dx * sx);
            int sy0 = (int)(dy * sy);
            int sx1 = std::min(sx0 + 1, src_w - 1);
            int sy1 = std::min(sy0 + 1, src_h - 1);

            // Bilinear sample
            float fx = dx * sx - sx0;
            float fy = dy * sy - sy0;
            float v00 = src[sy0 * src_w + sx0];
            float v10 = src[sy0 * src_w + sx1];
            float v01 = src[sy1 * src_w + sx0];
            float v11 = src[sy1 * src_w + sx1];
            float v = v00 * (1-fx)*(1-fy) + v10 * fx*(1-fy) +
                      v01 * (1-fx)*fy    + v11 * fx*fy;
            dst[dy * dst_w + dx] = v;
        }
    }
}

float ThumbnailGenerator::percentile(const float* arr, size_t n, float p) {
    if (n == 0) return 0.0f;
    std::vector<float> copy(arr, arr + n);
    std::sort(copy.begin(), copy.end());
    size_t idx = size_t(p * (n - 1));
    return copy[std::min(idx, n - 1)];
}

float ThumbnailGenerator::median_of_floats(const float* arr, size_t n) {
    return percentile(arr, n, 0.5f);
}

std::vector<uint8_t> ThumbnailGenerator::float_to_rgba(const float* data,
                                                         int w, int h,
                                                         int max_size) {
    if (w == 0 || h == 0) return {};

    // Compute thumbnail dimensions maintaining aspect ratio
    int tw = w, th = h;
    if (tw > max_size || th > max_size) {
        if (tw > th) {
            th = th * max_size / tw;
            tw = max_size;
        } else {
            tw = tw * max_size / th;
            th = max_size;
        }
    }

    // Downsample if needed
    std::vector<float> sampled;
    const float* ptr = data;
    if (tw < w || th < h) {
        sampled.resize(tw * th);
        downsample_2d(data, sampled.data(), w, h, tw, th);
        ptr = sampled.data();
    }

    // Stretch using percentile-based normalization (robust to outliers)
    float lo = percentile(ptr, tw * th, 0.02f);
    float hi = percentile(ptr, tw * th, 0.98f);
    float range = hi - lo;
    if (range < 1e-10f) range = 1.0f;

    std::vector<uint8_t> out(tw * th * 4);
    for (int i = 0; i < tw * th; i++) {
        float v = (ptr[i] - lo) / range;
        v = std::max(0.0f, std::min(1.0f, v));
        uint8_t gray = (uint8_t)(v * 255.0f);
        out[i * 4 + 0] = gray;
        out[i * 4 + 1] = gray;
        out[i * 4 + 2] = gray;
        out[i * 4 + 3] = 255;
    }

    // Also return dimensions packed in the struct - caller unpacks
    return out;
}

std::optional<ThumbResult> ThumbnailGenerator::generate(const std::string& fits_path,
                                                        int hdu_index,
                                                        int max_size) {
    int status = 0;
    fitsfile* fptr = nullptr;

    if (fits_open_file(&fptr, fits_path.c_str(), READONLY, &status))
        return {};

    if (fits_movabs_hdu(fptr, hdu_index + 1, nullptr, &status)) {
        fits_close_file(fptr, &status);
        return {};
    }

    int bitpix, naxis;
    long naxes[3] = {0, 0, 0};
    if (fits_get_img_param(fptr, 3, &bitpix, &naxis, naxes, &status)) {
        fits_close_file(fptr, &status);
        return {};
    }

    // Only support 2D images for now
    if (naxis != 2 || naxes[0] == 0 || naxes[1] == 0) {
        fits_close_file(fptr, &status);
        return {};
    }

    int w = (int)naxes[0];
    int h = (int)naxes[1];

    // Compute thumbnail size
    int tw = w, th = h;
    if (tw > max_size || th > max_size) {
        if (tw > th) {
            th = th * max_size / tw;
            tw = max_size;
        } else {
            tw = tw * max_size / th;
            th = max_size;
        }
    }

    // Read full image as float (supports all BITPIX)
    int64_t total_pixels = (int64_t)w * h;
    if (total_pixels > 10000LL * 10000LL) {
        fits_close_file(fptr, &status);
        return {};  // Too large
    }

    std::vector<float> img_data(total_pixels);
    std::vector<double> dbl_buf(total_pixels);
    int anynul = 0;

    if (bitpix == DOUBLE_IMG) {
        fits_read_img(fptr, TDOUBLE, 1, total_pixels, nullptr,
                       dbl_buf.data(), &anynul, &status);
        for (size_t i = 0; i < total_pixels; i++)
            img_data[i] = (float)dbl_buf[i];
    } else if (bitpix == FLOAT_IMG) {
        fits_read_img(fptr, TFLOAT, 1, total_pixels, nullptr,
                       img_data.data(), &anynul, &status);
    } else {
        // Convert any integer type to float
        LONGLONG fpixel = 1;
        switch (bitpix) {
            case BYTE_IMG:     fits_readImg(fptr, TBYTE,  fpixel, total_pixels, nullptr, img_data.data(), &anynul, &status); break;
            case SHORT_IMG:    {
                std::vector<short> sbuf(total_pixels);
                fits_readImg(fptr, TSHORT, fpixel, total_pixels, nullptr, sbuf.data(), &anynul, &status);
                for (size_t i = 0; i < total_pixels; i++) img_data[i] = sbuf[i];
                break;
            }
            case USHORT_IMG:  {
                std::vector<unsigned short> usbuf(total_pixels);
                fits_readImg(fptr, TUSHORT, fpixel, total_pixels, nullptr, usbuf.data(), &anynul, &status);
                for (size_t i = 0; i < total_pixels; i++) img_data[i] = usbuf[i];
                break;
            }
            case LONG_IMG:    {
                std::vector<long> lbuf(total_pixels);
                fits_readImg(fptr, TLONG, fpixel, total_pixels, nullptr, lbuf.data(), &anynul, &status);
                for (size_t i = 0; i < total_pixels; i++) img_data[i] = lbuf[i];
                break;
            }
            case LONGLONG_IMG: {
                std::vector<long long> llbuf(total_pixels);
                fits_readImg(fptr, TLONGLONG, fpixel, total_pixels, nullptr, llbuf.data(), &anynul, &status);
                for (size_t i = 0; i < total_pixels; i++) img_data[i] = (float)llbuf[i];
                break;
            }
            default:
                fits_close_file(fptr, &status);
                return {};
        }
    }

    fits_close_file(fptr, &status);
    if (status) return {};

    ThumbResult result;
    result.width = tw;
    result.height = th;
    result.pixels = float_to_rgba(img_data.data(), w, h, max_size);
    return result;
}
