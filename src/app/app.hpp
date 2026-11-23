// Lookout on-device UI and the logic that starts/stops the camera and microphone.
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "app/preview.hpp"
#include "lvgl.h"
#include "media/media.hpp"
#include "net/server.hpp"
#include "platform/device.hpp"

namespace lk {

class App {
public:
    App();
    ~App();
    void init();
    void tick();
    bool quit() const { return quit_; }
    // Stop the camera and microphone and restore the backlight (call before exiting).
    void shutdown();

private:
    enum class View { Main, Full, Qr, Sleep, ConfirmNewLink };

    void build();
    void onKey(const KeyEvent& e);
    void reconcileMedia();
    void refreshPreview();
    void refreshStatus();
    std::string url() const;
    std::string statusJson();
    bool applySetting(const std::string& key, int value);

    Settings settings_;
    mutable std::mutex settingsMu_;  // settings_ is read by server threads
    VideoSource video_;
    AudioSource audio_;
    std::unique_ptr<Server> server_;
    std::string serverError_;
    Keyboard kb_;
    Backlight backlight_;
    JpegPreview jpeg_;
    bool previewOk_ = false;

    View view_ = View::Main;
    bool quit_ = false;
    std::string ip_, shownUrl_;
    int64_t lastIpCheck_ = 0, lastMediaCheck_ = 0, lastStatus_ = 0, lastPreview_ = 0;
    int64_t videoStartedAt_ = 0, audioStartedAt_ = 0, noViewersSince_ = 0;
    uint64_t previewSeq_ = 0;
    std::string flash_;
    int64_t flashUntil_ = 0;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* img_ = nullptr;
    lv_obj_t* qr_ = nullptr;
    lv_obj_t* liveLabel_ = nullptr;
    lv_obj_t* infoLabel_ = nullptr;
    lv_obj_t* urlLabel_ = nullptr;
    lv_obj_t* micLabel_ = nullptr;
    lv_image_dsc_t imgDsc_{};
    std::vector<uint16_t> pixels_;
    int previewW_ = 0, previewH_ = 0;
};

}  // namespace lk
