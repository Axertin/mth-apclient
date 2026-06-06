// Keeps the LD_PRELOAD .so loadable on the game's older runtime glibc (floor 2.35). All
// definitions are hidden so we resolve our own static-libc++/net refs without interposing
// these symbols process-wide.
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdlib> // abort

#include <sys/random.h> // getentropy (GLIBC_2.25)

#include "pal/pal_log.hpp"

extern "C"
{
    // C++23 + glibc >= 2.38 redirect strtol/sscanf to __isoc23_*@GLIBC_2.38; forward to the
    // asm-named legacy symbols (strtol == strtol@GLIBC_2.2.5), bypassing the redirect.
    long real_strtol(const char *, char **, int) __asm__("strtol");
    long long real_strtoll(const char *, char **, int) __asm__("strtoll");
    unsigned long real_strtoul(const char *, char **, int) __asm__("strtoul");
    unsigned long long real_strtoull(const char *, char **, int) __asm__("strtoull");
    int real_vsscanf(const char *, const char *, va_list) __asm__("vsscanf");

    __attribute__((visibility("hidden"))) long __isoc23_strtol(const char *p, char **e, int b);
    __attribute__((visibility("hidden"))) long long __isoc23_strtoll(const char *p, char **e, int b);
    __attribute__((visibility("hidden"))) unsigned long __isoc23_strtoul(const char *p, char **e, int b);
    __attribute__((visibility("hidden"))) unsigned long long __isoc23_strtoull(const char *p, char **e, int b);
    __attribute__((visibility("hidden"))) int __isoc23_sscanf(const char *s, const char *f, ...);
    __attribute__((visibility("hidden"))) std::uint32_t arc4random(void) noexcept;

    long __isoc23_strtol(const char *p, char **e, int b)
    {
        return real_strtol(p, e, b);
    }
    long long __isoc23_strtoll(const char *p, char **e, int b)
    {
        return real_strtoll(p, e, b);
    }
    unsigned long __isoc23_strtoul(const char *p, char **e, int b)
    {
        return real_strtoul(p, e, b);
    }
    unsigned long long __isoc23_strtoull(const char *p, char **e, int b)
    {
        return real_strtoull(p, e, b);
    }
    int __isoc23_sscanf(const char *s, const char *f, ...)
    {
        va_list a;
        va_start(a, f);
        int r = real_vsscanf(s, f, a);
        va_end(a);
        return r;
    }

    // arc4random is @GLIBC_2.36; back it with getentropy (@2.25) to stay under the floor.
    // getentropy only fails on pre-3.17 kernels (unreachable here), so treat failure as fatal
    // rather than hand libc++ non-random bytes.
    std::uint32_t arc4random(void) noexcept
    {
        std::uint32_t v = 0;
        if (getentropy(&v, sizeof v) != 0)
        {
            pal::logf(pal::LogLevel::Error, "arc4random: getentropy failed (errno=%d); aborting", errno);
            std::abort();
        }
        return v;
    }
}
