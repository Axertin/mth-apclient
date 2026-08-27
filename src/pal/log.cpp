// File logger shared by both platforms. Only log_dir() differs enough to justify its own TU, so it
// stays in linux/ and windows/; the two CRT spellings below are the whole rest of the divergence.

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>

#include "pal/pal_log.hpp"

namespace fs = std::filesystem;

namespace
{

// "<date><sep><time><sep><millis>", the one timestamp both the log lines and the log filename want.
void stamp(char (&out)[32], const char *fmt, char sep)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t secs = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char date[24];
    std::strftime(date, sizeof(date), fmt, &tm);
    std::snprintf(out, sizeof(out), "%s%c%03lld", date, sep, static_cast<long long>(millis));
}

// Only the Linux crash handler reads this, to keep writing after the process is already unwinding.
// The Windows handler goes through logf plus MiniDumpWriteDump, so it never asks.
int descriptor_of(std::FILE *fp)
{
#if defined(_WIN32)
    (void)fp;
    return -1;
#else
    return fp ? fileno(fp) : -1;
#endif
}

class FileLog final : public pal::ILog
{
  public:
    explicit FileLog(const fs::path &path)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        // string() rather than c_str(): the latter is wchar_t* on Windows.
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
        // Stamped before the lock so a line records when it was written, not when it won the mutex.
        char ts[32];
        stamp(ts, "%Y-%m-%d %H:%M:%S", '.');

        std::lock_guard<std::mutex> lock(mu_);
        std::fprintf(fp_, "[%s] [%s] %.*s\n", ts, tag, static_cast<int>(message.size()), message.data());
        std::fflush(fp_);
    }

    void flush() override
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (fp_)
            std::fflush(fp_);
    }

    int fd() const
    {
        return descriptor_of(fp_);
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

void log_init(std::string_view stem)
{
    if (g_file_log)
        return;
    char ts[32];
    stamp(ts, "%Y-%m-%d_%H-%M-%S", '_');
    char fname[64];
    std::snprintf(fname, sizeof(fname), "%.*s_%s.log", static_cast<int>(stem.size()), stem.data(), ts);
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
    return g_file_log ? g_file_log->fd() : -1;
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
