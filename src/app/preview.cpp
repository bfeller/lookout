#include "app/preview.hpp"

#include <dlfcn.h>

#include <algorithm>

namespace lk {
namespace {

// Subset of the TurboJPEG 2.x API (stable ABI).
using InitFn = void* (*)();
using HeaderFn = int (*)(void*, const unsigned char*, unsigned long, int*, int*, int*, int*);
using DecompressFn = int (*)(void*, const unsigned char*, unsigned long, unsigned char*, int, int, int, int, int);
using DestroyFn = int (*)(void*);
constexpr int kPixelRgb = 0;           // TJPF_RGB
constexpr int kFlags = 2048 | 256;     // TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE

InitFn g_init;
HeaderFn g_header;
DecompressFn g_decompress;
DestroyFn g_destroy;

}  // namespace

JpegPreview::~JpegPreview()
{
    if (handle_ && g_destroy) g_destroy(handle_);
    if (lib_) dlclose(lib_);
}

bool JpegPreview::init()
{
    lib_ = dlopen("libturbojpeg.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!lib_) return false;
    g_init = reinterpret_cast<InitFn>(dlsym(lib_, "tjInitDecompress"));
    g_header = reinterpret_cast<HeaderFn>(dlsym(lib_, "tjDecompressHeader3"));
    g_decompress = reinterpret_cast<DecompressFn>(dlsym(lib_, "tjDecompress2"));
    g_destroy = reinterpret_cast<DestroyFn>(dlsym(lib_, "tjDestroy"));
    if (!g_init || !g_header || !g_decompress || !g_destroy) return false;
    handle_ = g_init();
    return handle_ != nullptr;
}

bool JpegPreview::decode(const std::string& jpeg, int boxW, int boxH, bool crop, std::vector<uint16_t>& out)
{
    if (!handle_) return false;
    const auto* buf = reinterpret_cast<const unsigned char*>(jpeg.data());
    int w = 0, h = 0, sub = 0, cs = 0;
    if (g_header(handle_, buf, jpeg.size(), &w, &h, &sub, &cs) != 0 || w <= 0 || h <= 0) return false;
    // Decode at the smallest n/8 scale that is still at least the box size (cheap, sharp),
    // then resample to the box: fit (whole picture, letterboxed) or crop (fill the box).
    int num = 8;
    for (int n = 1; n <= 8; ++n) {
        const int sw = (w * n + 7) / 8, sh = (h * n + 7) / 8;
        if (sw >= boxW || sh >= boxH) {
            num = n;
            break;
        }
    }
    const int sw = (w * num + 7) / 8, sh = (h * num + 7) / 8;
    rgb_.resize(static_cast<size_t>(sw) * sh * 3);
    if (g_decompress(handle_, buf, jpeg.size(), rgb_.data(), sw, sw * 3, sh, kPixelRgb, kFlags) != 0) return false;

    const double sx = static_cast<double>(boxW) / sw, sy = static_cast<double>(boxH) / sh;
    const double scale = crop ? std::max(sx, sy) : std::min(sx, sy);
    const int dw = std::max(1, static_cast<int>(sw * scale + 0.5)), dh = std::max(1, static_cast<int>(sh * scale + 0.5));
    const int ox = (boxW - dw) / 2, oy = (boxH - dh) / 2;  // negative when cropping
    out.assign(static_cast<size_t>(boxW) * boxH, 0);
    for (int y = 0; y < boxH; ++y) {
        const int ry = y - oy;
        if (ry < 0 || ry >= dh) continue;
        const int srcY = std::min(sh - 1, static_cast<int>(ry / scale));
        for (int x = 0; x < boxW; ++x) {
            const int rx = x - ox;
            if (rx < 0 || rx >= dw) continue;
            const int srcX = std::min(sw - 1, static_cast<int>(rx / scale));
            const uint8_t* p = &rgb_[(static_cast<size_t>(srcY) * sw + srcX) * 3];
            out[static_cast<size_t>(y) * boxW + x] =
                static_cast<uint16_t>(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
        }
    }
    return true;
}

}  // namespace lk
