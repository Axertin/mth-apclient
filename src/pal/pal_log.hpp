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

// printf-style format checking. Clang only supports the printf archetype (it
// ignores gnu_printf on every target with -Wignored-attributes); GCC needs
// gnu_printf for C99 specifiers (%zu etc.). Clang's printf archetype already
// accepts those, so the split is purely clang vs gcc.
#if defined(__clang__)
#define MTHAP_PRINTF_FORMAT(fmt, va) __attribute__((format(printf, fmt, va)))
#elif defined(__GNUC__)
#define MTHAP_PRINTF_FORMAT(fmt, va) __attribute__((format(gnu_printf, fmt, va)))
#else
#define MTHAP_PRINTF_FORMAT(fmt, va)
#endif

void logf(LogLevel, const char *fmt, ...) MTHAP_PRINTF_FORMAT(2, 3);
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
