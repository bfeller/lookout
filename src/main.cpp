// Lookout: stream the Cardputer Zero's camera and microphone to a phone browser.
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#include "app/app.hpp"
#include "lvgl.h"

using namespace lk;

static volatile std::sig_atomic_t g_stop = 0;
static uint32_t tickMs() { return static_cast<uint32_t>(nowMs()); }

// The Cardputer LCD is a small SPI panel; find its framebuffer rather than assuming fb0.
static std::string findFramebuffer()
{
    if (const char* e = std::getenv("LV_LINUX_FBDEV_DEVICE")) return e;
    std::ifstream f("/proc/fb");
    int n;
    std::string name;
    while (f >> n >> name)
        if (name.find("st7789") != std::string::npos || name.find("mipi") != std::string::npos ||
            name.find("panel") != std::string::npos)
            return "/dev/fb" + std::to_string(n);
    return "/dev/fb0";
}

int main()
{
    std::signal(SIGINT, [](int) { g_stop = 1; });
    std::signal(SIGTERM, [](int) { g_stop = 1; });
    std::signal(SIGPIPE, SIG_IGN);
    lv_init();
    lv_tick_set_cb(tickMs);
    lv_display_t* disp = lv_linux_fbdev_create();
    const std::string fb = findFramebuffer();
    if (lv_linux_fbdev_set_file(disp, fb.c_str()) != LV_RESULT_OK) {
        std::fprintf(stderr, "lookout: cannot open framebuffer %s\n", fb.c_str());
        return 1;
    }
    // Never destroyed: server threads may still reference it while the process exits.
    auto* app = new App();
    app->init();
    while (!g_stop && !app->quit()) {
        const uint32_t wait = lv_timer_handler();
        app->tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<uint32_t>(wait, 10)));
    }
    app->shutdown();
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_refr_now(disp);
    // Server threads may still be blocked on sockets; exit without waiting for them.
    std::_Exit(0);
}
