#include <cstdlib>

#include "pal/pal_cert.hpp"

namespace fs = std::filesystem;

namespace
{

std::optional<fs::path> env_path(const char *var)
{
    if (const char *v = std::getenv(var); v && *v)
    {
        std::error_code ec;
        if (fs::exists(v, ec))
            return fs::path(v);
    }
    return std::nullopt;
}

} // namespace

namespace pal
{

std::optional<fs::path> ca_bundle_path()
{
    if (auto p = env_path("MTHAP_AP_CERT"))
        return p;
    if (auto p = env_path("SSL_CERT_FILE"))
        return p;

    static const char *const kCandidates[] = {
        "/etc/ssl/certs/ca-certificates.crt", // Debian/Ubuntu/Steam sniper
        "/etc/pki/tls/certs/ca-bundle.crt",   // Fedora/RHEL
        "/etc/ssl/cert.pem",                  // Alpine/BSD
        "/etc/ssl/ca-bundle.pem",
        "/etc/pki/tls/cacert.pem",
    };
    std::error_code ec;
    for (const char *cand : kCandidates)
        if (fs::exists(cand, ec))
            return fs::path(cand);
    return std::nullopt;
}

} // namespace pal
