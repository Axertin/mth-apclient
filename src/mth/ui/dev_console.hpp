#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "mth/core/log_ring.hpp"
#include "mth/ui/broadcast_banner.hpp"
#include "pal/pal_overlay.hpp"

namespace mth
{

class ICommandSink;
class BannerQueue;

// Dev console UI. Registers as pal log observer; output pane mirrors the log stream.
class DevConsole final
{
  public:
    DevConsole(ICommandSink &sink, BannerQueue &banner_queue);
    ~DevConsole();

    DevConsole(const DevConsole &) = delete;
    DevConsole &operator=(const DevConsole &) = delete;

    void draw(bool console_open);

  private:
    void draw_version_hud();
    void draw_console();
    void run_input();
    void println(const std::string &line);

    ICommandSink &sink_;
    BroadcastBanner banner_; // always-on PrintJSON banner, drawn regardless of console_open
    LogRing log_;
    std::array<char, 512> input_{};
    bool scroll_to_bottom_{true};
    std::size_t last_log_size_{0}; // tracks growth for auto-scroll
};

} // namespace mth
