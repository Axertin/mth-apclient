#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "mth/core/log_ring.hpp"
#include "pal/pal_overlay.hpp"

namespace mth
{

class ICommandSink;

// The dev console UI. Owns the output buffer; reads command effects through a
// CommandSink. Registers itself as the pal log observer on construction so the
// output pane mirrors the live log stream. Drawn each frame via draw().
class DevConsole final : public pal::IOverlayUi
{
  public:
    explicit DevConsole(ICommandSink &sink);
    ~DevConsole() override;

    DevConsole(const DevConsole &) = delete;
    DevConsole &operator=(const DevConsole &) = delete;

    void draw() override;

  private:
    void run_input();                      // parse + dispatch the current input buffer
    void println(const std::string &line); // echo into the output pane

    ICommandSink &sink_;
    LogRing log_;
    std::array<char, 512> input_{};
    bool scroll_to_bottom_{true};
    std::size_t last_log_size_{0}; // grows-check so observer-pushed log lines also pin to bottom
};

} // namespace mth
