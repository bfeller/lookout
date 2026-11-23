#include "app/app.hpp"

#include <cstdio>
#include <cstdlib>

#include "src/misc/cache/instance/lv_image_cache.h"

namespace lk {
namespace {

constexpr int kW = 320, kH = 170, kTitleH = 20, kFooterH = 15;
constexpr int kPrevX = 4, kPrevY = 24, kPrevW = 176, kPrevH = 99;  // main-view preview (16:9)
constexpr int kQrSize = 132;      // main view; the full-screen QR view uses kQrBig
constexpr int kQrBig = 166;
// Softer than pure white so the backlit screen doesn't overexpose phone cameras.
const lv_color_t kQrLight = lv_color_hex(0xC8C8C8);

const lv_color_t kBg = lv_color_hex(0x111214);
const lv_color_t kCard = lv_color_hex(0x1E2024);
const lv_color_t kText = lv_color_hex(0xEDEDED);
const lv_color_t kDim = lv_color_hex(0x8A8F98);
const lv_color_t kAccent = lv_color_hex(0x29C5F6);
const lv_color_t kLive = lv_color_hex(0xFF3D57);
const lv_color_t kGreen = lv_color_hex(0x4CD964);

const int kGainDb[3] = {12, 24, 36};

lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t bg, int radius = 0)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t* text(lv_obj_t* parent, const std::string& s, const lv_font_t* font, lv_color_t color, int x, int y,
               int w = -1, lv_text_align_t align = LV_TEXT_ALIGN_LEFT)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, s.c_str());
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, align, 0);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    }
    lv_obj_set_pos(l, x, y);
    return l;
}

lv_obj_t* makeQr(lv_obj_t* parent, int size)
{
    lv_obj_t* qr = lv_qrcode_create(parent);
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, kQrLight);
    lv_qrcode_set_quiet_zone(qr, true);  // scanners need the blank border
    return qr;
}

const char* resName(int quality) { return quality == 0 ? "480p" : quality == 2 ? "1080p" : "720p"; }

}  // namespace

App::App() = default;

App::~App() = default;

void App::shutdown()
{
    video_.stop();
    audio_.stop();
    backlight_.restore();
}

void App::init()
{
    settings_.load();
    kb_.open();
    backlight_.init();
    previewOk_ = jpeg_.init();
    ip_ = localIp();

    ServerHooks hooks;
    hooks.token = [this] {
        std::lock_guard<std::mutex> lock(settingsMu_);
        return settings_.token;
    };
    hooks.statusJson = [this] { return statusJson(); };
    hooks.set = [this](const std::string& k, int v) { return applySetting(k, v); };
    server_ = std::make_unique<Server>(video_, audio_, hooks);
    if (!server_->start(settings_.port, serverError_)) server_.reset();

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, kBg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    root_ = box(scr, 0, 0, kW, kH, kBg);
    // For screenshots/testing: LOOKOUT_VIEW=qr|full opens that view directly.
    if (const char* v = std::getenv("LOOKOUT_VIEW")) {
        if (std::string(v) == "qr") view_ = View::Qr;
        else if (std::string(v) == "full") view_ = View::Full;
    }
    build();
}

std::string App::url() const
{
    std::lock_guard<std::mutex> lock(settingsMu_);
    if (ip_.empty() || !server_) return "";
    return "http://" + ip_ + ":" + std::to_string(server_->port()) + "/?k=" + settings_.token;
}

std::string App::statusJson()
{
    Settings s;
    {
        std::lock_guard<std::mutex> lock(settingsMu_);
        s = settings_;
    }
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "{\"res\":\"%s\",\"fps\":%.1f,\"quality\":%d,\"gain\":%d,\"battery\":%d,\"viewers\":%d,"
                  "\"listeners\":%d,\"mic\":%s}",
                  resName(s.quality), video_.fps(), s.quality, s.micGain, batteryPercent(),
                  server_ ? server_->viewers() : 0, server_ ? server_->listeners() : 0,
                  s.micEnabled ? "true" : "false");
    return buf;
}

// Called from server threads; media reconciliation picks the change up on the UI thread.
bool App::applySetting(const std::string& key, int value)
{
    std::lock_guard<std::mutex> lock(settingsMu_);
    if (key == "quality" && value >= 0 && value <= 2) settings_.quality = value;
    else if (key == "gain" && value >= 0 && value <= 2) settings_.micGain = value;
    else if (key == "rotate" && (value == 0 || value == 1)) settings_.rotate180 = value == 1;
    else return false;
    settings_.save();
    return true;
}

// ------------------------------------------------------------------ UI
void App::build()
{
    lv_obj_clean(root_);
    img_ = qr_ = liveLabel_ = infoLabel_ = urlLabel_ = micLabel_ = nullptr;
    shownUrl_.clear();
    previewSeq_ = 0;
    if (view_ == View::Sleep) {
        lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
        return;
    }
    lv_obj_set_style_bg_color(root_, kBg, 0);

    const bool full = view_ == View::Full;
    previewW_ = full ? kW : kPrevW;
    previewH_ = full ? kH : kPrevH;
    pixels_.assign(static_cast<size_t>(previewW_) * previewH_, 0);
    imgDsc_ = {};
    imgDsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    imgDsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    imgDsc_.header.w = static_cast<uint32_t>(previewW_);
    imgDsc_.header.h = static_cast<uint32_t>(previewH_);
    imgDsc_.header.stride = static_cast<uint32_t>(previewW_ * 2);
    imgDsc_.data = reinterpret_cast<const uint8_t*>(pixels_.data());
    imgDsc_.data_size = static_cast<uint32_t>(pixels_.size() * 2);

    if (full) {
        img_ = lv_image_create(root_);
        lv_obj_set_pos(img_, 0, 0);
        lv_image_set_src(img_, &imgDsc_);
        lv_obj_t* tag = box(root_, 4, 4, 150, 16, lv_color_black(), 4);
        lv_obj_set_style_bg_opa(tag, LV_OPA_60, 0);
        liveLabel_ = text(tag, "", &lv_font_montserrat_10, kText, 5, 2, 140);
        return;
    }

    if (view_ == View::Qr) {
        // Full-height QR code for easy scanning from a distance.
        qr_ = makeQr(root_, kQrBig);
        lv_obj_set_pos(qr_, (kW - kQrBig) / 2, (kH - kQrBig) / 2);
        const int side = (kW - kQrBig) / 2 - 6;
        lv_obj_t* l = text(root_, "Scan with your phone camera", &lv_font_montserrat_12, kText, 4, 50, side);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        urlLabel_ = text(root_, "", &lv_font_montserrat_10, kDim, kW - side - 4, 50, side);
        lv_label_set_long_mode(urlLabel_, LV_LABEL_LONG_MODE_WRAP);
        text(root_, "Enter back", &lv_font_montserrat_10, kDim, kW - side - 4, 140, side);
        return;
    }

    if (view_ == View::ConfirmNewLink) {
        lv_obj_t* bar = box(root_, 0, 0, kW, kTitleH, kCard);
        text(bar, "New viewing link?", &lv_font_montserrat_14, kAccent, 6, 2, 300);
        text(root_,
             "Anyone using the current link or QR code will be disconnected and will need to scan the new one.",
             &lv_font_montserrat_12, kText, 10, 34, 300);
        lv_label_set_long_mode(lv_obj_get_child(root_, -1), LV_LABEL_LONG_MODE_WRAP);
        text(root_, "Y  make a new link      N  cancel", &lv_font_montserrat_14, kAccent, 10, 104);
        return;
    }

    lv_obj_t* bar = box(root_, 0, 0, kW, kTitleH, kCard);
    text(bar, "Lookout", &lv_font_montserrat_14, kAccent, 6, 2, 90);
    liveLabel_ = text(bar, "", &lv_font_montserrat_10, kDim, 100, 5, 214, LV_TEXT_ALIGN_RIGHT);

    lv_obj_t* frame = box(root_, kPrevX - 1, kPrevY - 1, kPrevW + 2, kPrevH + 2, lv_color_hex(0x2C2F35), 3);
    (void)frame;
    img_ = lv_image_create(root_);
    lv_obj_set_pos(img_, kPrevX, kPrevY);
    lv_image_set_src(img_, &imgDsc_);
    if (!previewOk_) text(root_, "Preview unavailable", &lv_font_montserrat_10, kDim, kPrevX + 40, kPrevY + 50);

    qr_ = makeQr(root_, kQrSize);
    lv_obj_set_pos(qr_, kW - kQrSize - 4, kTitleH + 1);
    urlLabel_ = text(root_, "", &lv_font_montserrat_10, kText, kPrevX, kPrevY + kPrevH + 3, kPrevW);
    infoLabel_ = text(root_, "", &lv_font_montserrat_10, kDim, kPrevX, kPrevY + kPrevH + 16, kPrevW);
    micLabel_ = text(root_, "", &lv_font_montserrat_10, kDim, kPrevX, kPrevY + kPrevH + 16, kPrevW, LV_TEXT_ALIGN_RIGHT);

    lv_obj_t* foot = box(root_, 0, kH - kFooterH, kW, kFooterH, kCard);
    text(foot, LV_SYMBOL_NEW_LINE " QR  V view  Q qual  M mic  G gain  R rot  S sleep  N link", &lv_font_montserrat_10,
         kDim, 2, 1, kW - 4, LV_TEXT_ALIGN_CENTER);
}

void App::refreshStatus()
{
    Settings s;
    {
        std::lock_guard<std::mutex> lock(settingsMu_);
        s = settings_;
    }
    const int viewers = server_ ? server_->viewers() : 0;
    const int listeners = server_ ? server_->listeners() : 0;
    const int bat = batteryPercent();
    char live[96];
    if (viewers > 0)
        std::snprintf(live, sizeof live, LV_SYMBOL_EYE_OPEN " LIVE %d   %s%d%%", viewers,
                      bat >= 0 ? LV_SYMBOL_BATTERY_3 " " : "", bat);
    else
        std::snprintf(live, sizeof live, "no viewers   %s%d%%", bat >= 0 ? LV_SYMBOL_BATTERY_3 " " : "", bat);
    if (liveLabel_) {
        lv_label_set_text(liveLabel_, live);
        lv_obj_set_style_text_color(liveLabel_, viewers > 0 ? kLive : kDim, 0);
    }
    const std::string u = url();
    if (qr_ && u != shownUrl_) {
        shownUrl_ = u;
        if (u.empty()) {
            lv_qrcode_update(qr_, "no network", 10);
            lv_label_set_text(urlLabel_, serverError_.empty() ? "Connect to Wi-Fi" : serverError_.c_str());
        } else {
            lv_qrcode_update(qr_, u.data(), static_cast<uint32_t>(u.size()));
            lv_label_set_text(urlLabel_, (ip_ + ":" + std::to_string(server_->port())).c_str());
        }
    }
    if (view_ == View::Full || !infoLabel_) return;

    if (!flash_.empty() && nowMs() < flashUntil_) {
        lv_label_set_text(infoLabel_, flash_.c_str());
        lv_obj_set_style_text_color(infoLabel_, kGreen, 0);
    } else {
        char info[96];
        std::snprintf(info, sizeof info, "%s %.0f fps%s", resName(s.quality), video_.fps(),
                      s.rotate180 ? " " LV_SYMBOL_REFRESH : "");
        lv_label_set_text(infoLabel_, video_.running() ? info : "camera idle");
        lv_obj_set_style_text_color(infoLabel_, kDim, 0);
    }
    char mic[64];
    if (!s.micEnabled) std::snprintf(mic, sizeof mic, LV_SYMBOL_MUTE " off");
    else if (listeners > 0) std::snprintf(mic, sizeof mic, LV_SYMBOL_AUDIO " LIVE +%d", kGainDb[s.micGain]);
    else std::snprintf(mic, sizeof mic, LV_SYMBOL_AUDIO " +%ddB", kGainDb[s.micGain]);
    lv_label_set_text(micLabel_, mic);
    lv_obj_set_style_text_color(micLabel_, listeners > 0 ? kLive : kDim, 0);
}

void App::refreshPreview()
{
    if (!img_ || !previewOk_) return;
    uint64_t seq = 0;
    Frame f = video_.latest(&seq);
    if (!f || seq == previewSeq_) return;
    previewSeq_ = seq;
    std::vector<uint16_t> out;
    if (!jpeg_.decode(*f, previewW_, previewH_, view_ == View::Full, out)) return;
    pixels_.swap(out);
    imgDsc_.data = reinterpret_cast<const uint8_t*>(pixels_.data());
    lv_image_cache_drop(&imgDsc_);
    lv_image_set_src(img_, &imgDsc_);
    lv_obj_invalidate(img_);
}

// Start/stop the camera and microphone only when something needs them.
void App::reconcileMedia()
{
    Settings s;
    {
        std::lock_guard<std::mutex> lock(settingsMu_);
        s = settings_;
    }
    const int64_t now = nowMs();
    const int viewers = server_ ? server_->viewers() : 0;
    const int listeners = server_ ? server_->listeners() : 0;
    if (viewers > 0) noViewersSince_ = 0;
    else if (!noViewersSince_) noViewersSince_ = now;

    const bool wantVideo = view_ != View::Sleep || viewers > 0 ||
                           (noViewersSince_ && now - noViewersSince_ < 5000 && video_.running());
    const VideoConfig cfg = qualityPreset(s.quality, s.rotate180);
    if (wantVideo) {
        const bool needRestart = !video_.running() || !(video_.config() == cfg);
        if (needRestart && now - videoStartedAt_ > 2000) {  // back off if the camera keeps failing
            videoStartedAt_ = now;
            video_.start(cfg);
        }
    } else if (video_.running()) {
        video_.stop();
    }

    const bool wantAudio = s.micEnabled && listeners > 0;
    if (wantAudio) {
        const bool needRestart = !audio_.running() || audio_.gain() != kGainDb[s.micGain];
        if (needRestart && now - audioStartedAt_ > 2000) {
            audioStartedAt_ = now;
            audio_.start(kGainDb[s.micGain]);
        }
    } else if (audio_.running()) {
        audio_.stop();
    }
}

void App::tick()
{
    for (KeyEvent e = kb_.poll(); e.key != Key::None; e = kb_.poll()) onKey(e);
    const int64_t now = nowMs();
    if (now - lastIpCheck_ > 5000) {
        lastIpCheck_ = now;
        const std::string ip = localIp();
        std::lock_guard<std::mutex> lock(settingsMu_);
        ip_ = ip;
    }
    if (now - lastMediaCheck_ > 300) {
        lastMediaCheck_ = now;
        reconcileMedia();
    }
    if (view_ != View::Sleep && view_ != View::ConfirmNewLink) {
        if (now - lastPreview_ > (view_ == View::Full ? 66 : 100)) {
            lastPreview_ = now;
            refreshPreview();
        }
        if (now - lastStatus_ > 500) {
            lastStatus_ = now;
            refreshStatus();
        }
    }
}

void App::onKey(const KeyEvent& e)
{
    if (e.key == Key::LongEsc) {
        quit_ = true;
        return;
    }
    if (view_ == View::Sleep) {  // any key wakes the screen
        backlight_.set(true);
        view_ = View::Main;
        build();
        return;
    }
    if (view_ == View::ConfirmNewLink) {
        if (e.key == Key::Char && e.ch == 'y') {
            {
                std::lock_guard<std::mutex> lock(settingsMu_);
                settings_.token = Settings::newToken();
                settings_.save();
            }
            if (server_) server_->revokeAll();
            flash_ = "New link made; old viewers disconnected";
            flashUntil_ = nowMs() + 4000;
        }
        if (e.key == Key::Char || e.key == Key::Esc) {
            view_ = View::Main;
            build();
        }
        return;
    }
    if (e.key == Key::Enter || (e.key == Key::Esc && view_ == View::Qr)) {
        view_ = view_ == View::Qr ? View::Main : (e.key == Key::Enter ? View::Qr : view_);
        build();
        return;
    }
    if (e.key == Key::Esc) {
        if (view_ == View::Full) {
            view_ = View::Main;
            build();
        }
        return;
    }
    if (e.key != Key::Char) return;
    auto note = [this](const std::string& m) {
        flash_ = m;
        flashUntil_ = nowMs() + 2500;
        lastStatus_ = 0;
    };
    std::lock_guard<std::mutex> lock(settingsMu_);
    switch (e.ch) {
    case 'v':
        view_ = view_ == View::Full ? View::Main : View::Full;
        break;
    case 'q':
        settings_.quality = (settings_.quality + 1) % 3;
        note(std::string("Quality: ") + qualityName(settings_.quality));
        break;
    case 'm':
        settings_.micEnabled = !settings_.micEnabled;
        note(settings_.micEnabled ? "Microphone on" : "Microphone off");
        break;
    case 'g':
        settings_.micGain = (settings_.micGain + 1) % 3;
        note("Mic boost +" + std::to_string(kGainDb[settings_.micGain]) + " dB");
        break;
    case 'r':
        settings_.rotate180 = !settings_.rotate180;
        note(settings_.rotate180 ? "Rotated 180" : "Rotation normal");
        break;
    case 's':
        view_ = View::Sleep;
        backlight_.set(false);
        break;
    case 'n':
        view_ = View::ConfirmNewLink;
        break;
    default: return;
    }
    settings_.save();
    if (e.ch == 'v' || e.ch == 's' || e.ch == 'n') build();  // build() takes no locks
}

}  // namespace lk
