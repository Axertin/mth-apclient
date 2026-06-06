#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#include "pal/pal_log.hpp"

namespace fs = std::filesystem;

namespace
{

class FileLog final : public pal::ILog
{
  public:
    explicit FileLog(const fs::path &path)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fp_ = std::fopen(path.string().c_str(), "a");
    }

    ~FileLog() override
    {
        if (fp_)
            std::fclose(fp_);
    }

    void write(pal::LogLevel level, std::string_view message) override
    {
        if (!fp_)
            return;
        const char *tag = "INFO";
        switch (level)
        {
        case pal::LogLevel::Debug:
            tag = "DEBUG";
            break;
        case pal::LogLevel::Info:
            tag = "INFO";
            break;
        case pal::LogLevel::Warn:
            tag = "WARN";
            break;
        case pal::LogLevel::Error:
            tag = "ERROR";
            break;
        }
        const auto now = std::chrono::system_clock::now();
        const auto secs_since = std::chrono::system_clock::to_time_t(now);
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

        std::lock_guard<std::mutex> lock(mu_);
        std::tm tm{};
        localtime_s(&tm, &secs_since);
        char ts[24];
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
        std::fprintf(fp_, "[%s.%03lld] [%s] %.*s\n", ts, static_cast<long long>(millis), tag, static_cast<int>(message.size()), message.data());
        std::fflush(fp_);
    }

    void flush() override
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (fp_)
            std::fflush(fp_);
    }

  private:
    std::FILE *fp_{nullptr};
    std::mutex mu_;
};

class NullLog final : public pal::ILog
{
  public:
    void write(pal::LogLevel, std::string_view) override
    {
    }
    void flush() override
    {
    }
};

NullLog g_null_log;
std::atomic<pal::ILog *> g_log{&g_null_log};
FileLog *g_file_log{nullptr};

std::mutex g_observer_mu;
pal::LogObserver g_observer; // guarded by g_observer_mu

} // namespace

namespace pal
{

fs::path log_dir()
{
    if (const char *appdata = std::getenv("LOCALAPPDATA"); appdata && *appdata)
        return fs::path(appdata) / "mth-apclient";
    if (const char *tmp = std::getenv("TEMP"); tmp && *tmp)
        return fs::path(tmp) / "mth-apclient";
    return fs::temp_directory_path() / "mth-apclient";
}

void log_init(std::string_view stem)
{
    if (g_file_log)
        return;
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    localtime_s(&tm, &t);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tm);
    char fname[64];
    std::snprintf(fname, sizeof(fname), "%.*s_%s_%03lld.log", static_cast<int>(stem.size()), stem.data(), ts, static_cast<long long>(ms));
    g_file_log = new FileLog(log_dir() / fname);
    g_log.store(g_file_log, std::memory_order_release);
}

void log_shutdown()
{
    ILog *prev = g_log.exchange(&g_null_log, std::memory_order_acq_rel);
    if (prev == g_file_log)
    {
        delete g_file_log;
        g_file_log = nullptr;
    }
}

int log_fd()
{
    return -1; // unused on Windows; the crash handler uses logf + MiniDumpWriteDump
}

ILog &default_log()
{
    return *g_log.load(std::memory_order_acquire);
}

void set_default_log(ILog *log)
{
    g_log.store(log ? log : &g_null_log, std::memory_order_release);
}

void set_log_observer(LogObserver obs)
{
    std::lock_guard<std::mutex> lock(g_observer_mu);
    g_observer = std::move(obs);
}

void vlogf(LogLevel level, const char *fmt, std::va_list ap)
{
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    default_log().write(level, buf);
    {
        std::lock_guard<std::mutex> lock(g_observer_mu);
        if (g_observer)
            g_observer(level, buf);
    }
}

void logf(LogLevel level, const char *fmt, ...)
{
    std::va_list ap;
    va_start(ap, fmt);
    vlogf(level, fmt, ap);
    va_end(ap);
}

} // namespace pal
