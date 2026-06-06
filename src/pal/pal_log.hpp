#pragma once

#include <cstdarg>
#include <filesystem>
#include <functional>
#include <string_view>

namespace pal
{

enum class LogLevel
{
    Debug,
    Info,
    Warn,
    Error
};

class ILog
{
  public:
    virtual ~ILog() = default;
    virtual void write(LogLevel, std::string_view message) = 0;
    virtual void flush() = 0;
};

void log_init(std::string_view stem = "mthap");
void log_shutdown();

// gnu_printf: accepted by gcc and clang/clang-cl; covers C99 specifiers (%zu etc.).
void logf(LogLevel, const char *fmt, ...) __attribute__((format(gnu_printf, 2, 3)));
void vlogf(LogLevel, const char *fmt, std::va_list ap);

std::filesystem::path log_dir();

// Underlying file descriptor of the active file log, or -1 if unavailable. For
// the crash handler to emit a backtrace using only async-signal-safe writes (no
// logf/malloc). Real fd on Linux; -1 on Windows (its handler uses logf + minidump).
int log_fd();

ILog &default_log();
void set_default_log(ILog *);

// Observer runs under the log mutex; must not call logf/vlogf (deadlock).
using LogObserver = std::function<void(LogLevel, std::string_view)>;
void set_log_observer(LogObserver);

} // namespace pal
