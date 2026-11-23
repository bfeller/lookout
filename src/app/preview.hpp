// Fast JPEG -> RGB565 thumbnail decoding for the on-device preview.
// libturbojpeg (present on Raspberry Pi OS) is loaded at runtime with dlopen, and JPEG's
// built-in DCT scaling (1/8 .. 1) decodes straight to roughly the preview size.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lk {

class JpegPreview {
public:
    ~JpegPreview();
    bool init();  // false if libturbojpeg is unavailable (preview disabled)
    // Decode `jpeg` into a w x h RGB565 image that covers (crop=true) or fits inside
    // (crop=false) a boxW x boxH box. Output is exactly boxW x boxH, centred.
    bool decode(const std::string& jpeg, int boxW, int boxH, bool crop, std::vector<uint16_t>& out);

private:
    void* lib_ = nullptr;
    void* handle_ = nullptr;
    std::vector<uint8_t> rgb_;
};

}  // namespace lk
