// Linux crash handler: POSIX signal handlers for the fatal signals that log a
// backtrace as module(+offset) lines into the mod log, then re-raise the default
// disposition so the process still dies (and dumps core if enabled).
//
// Async-signal-safety is the hard constraint: the handler must touch nothing that
// can take a lock or call malloc, because the crash may have happened mid-logf or
// inside the allocator. So it does NOT call pal::logf; it writes straight to the
// log fd (captured at install) with backtrace_symbols_fd() and hand-rolled
// integer formatting. backtrace()/backtrace_symbols_fd() are the documented
// signal-safe pair and are ancient glibc symbols (GLIBC_2.2.5), so they stay
// within the Steam Linux Runtime symbol-version pin.

#include <cstring>

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

#include "pal/pal_crash.hpp"
#include "pal/pal_log.hpp"

namespace pal
{

namespace
{

constexpr int kMaxFrames = 64;
void *g_frames[kMaxFrames]; // reused in the handler; a crash is effectively single-shot
int g_log_fd = -1;          // captured at install so the handler never calls into the logger

// 64 KiB alternate stack so a stack-overflow SIGSEGV can still run the handler.
// Fixed size (not SIGSTKSZ, which is a non-constant sysconf call on glibc >= 2.34).
char g_altstack_mem[64 * 1024];

void safe_write(int fd, const char *s)
{
    if (fd >= 0)
    {
        const ssize_t n = ::write(fd, s, std::strlen(s));
        (void)n; // nothing useful to do with the result inside a crash
    }
}

// Append an unsigned value as 0x-hex. Async-signal-safe (no snprintf/malloc).
void safe_write_hex(int fd, unsigned long long v)
{
    char buf[2 + 16 + 1];
    char *p = buf + sizeof(buf);
    *--p = '\0';
    const char *digits = "0123456789abcdef";
    if (v == 0)
        *--p = '0';
    while (v != 0)
    {
        *--p = digits[v & 0xf];
        v >>= 4;
    }
    *--p = 'x';
    *--p = '0';
    safe_write(fd, p);
}

const char *signal_name(int sig)
{
    switch (sig)
    {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGABRT:
        return "SIGABRT";
    case SIGBUS:
        return "SIGBUS";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    default:
        return "signal";
    }
}

void handler(int sig, siginfo_t *info, void * /*ucontext*/)
{
    const int fds[2] = {g_log_fd, STDERR_FILENO};
    for (const int fd : fds)
    {
        if (fd < 0)
            continue;
        safe_write(fd, "\n*** mthap crash: ");
        safe_write(fd, signal_name(sig));
        safe_write(fd, " fault addr=");
        safe_write_hex(fd, reinterpret_cast<unsigned long long>(info != nullptr ? info->si_addr : nullptr));
        safe_write(fd, " ***\n");
    }

    // module(+0xRVA)[abs] per frame, no malloc; map game frames to the un-stripped
    // binary in Ghidra and libmthap.so frames to our code.
    const int n = backtrace(g_frames, kMaxFrames);
    for (const int fd : fds)
    {
        if (fd >= 0)
            backtrace_symbols_fd(g_frames, n, fd);
    }

    // Re-raise with the default disposition so the process terminates normally
    // (and writes a core dump if ulimit -c allows).
    signal(sig, SIG_DFL);
    raise(sig);
}

} // namespace

void install_crash_handler()
{
    g_log_fd = pal::log_fd();

    stack_t ss{};
    ss.ss_sp = g_altstack_mem;
    ss.ss_size = sizeof(g_altstack_mem);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    // Pre-warm backtrace(): its first call can dlopen libgcc / allocate, which we
    // must not do for the first time inside the handler.
    void *warm[4];
    (void)backtrace(warm, 4);

    struct sigaction sa{};
    sa.sa_sigaction = &handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    for (const int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL})
        sigaction(sig, &sa, nullptr);

    pal::logf(pal::LogLevel::Info, "crash: signal handlers installed (backtrace -> mod log; core on re-raise)");
}

} // namespace pal
