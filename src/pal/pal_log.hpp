#pragma once

#include <cstdarg>
#include <filesystem>
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

// Opens a fresh log file per call, named `<stem>_YYYY-MM-DD_HH-MM-SS_mmm.log`,
// in log_dir(). Filename only uses cross-platform-legal characters.
void log_init(std::string_view stem = "mthap");
void log_shutdown();

// gnu_printf archetype: understood by gcc and clang/clang-cl, and accepts the
// C99 specifiers (%zu etc.) we use.
void logf(LogLevel, const char *fmt, ...) __attribute__((format(gnu_printf, 2, 3)));
void vlogf(LogLevel, const char *fmt, std::va_list ap);

std::filesystem::path log_dir();

ILog &default_log();
void set_default_log(ILog *);

} // namespace pal
