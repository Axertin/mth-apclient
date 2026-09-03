#include <cstdlib>
#include <string>

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
    // Steam runs the game inside a pressure-vessel container that ships its own trust store, and
    // that store can be years behind the one the player's browser and shell agree on (the sniper
    // runtime has no SSL.com 2022 roots). Its own --import-ca-certs is opt-in and bails out on a
    // host whose /etc/ssl/certs has no hashed symlinks, so it cannot be relied on. The host
    // filesystem is mounted at /run/host, so read its store first and treat the container's as the
    // fallback. Outside a container the prefixed paths simply do not exist.
    static const char *const kHostPrefixes[] = {"/run/host", ""};
    std::error_code ec;
    for (const char *prefix : kHostPrefixes)
        for (const char *cand : kCandidates)
            if (fs::path p = std::string(prefix) + cand; fs::exists(p, ec))
                return p;
    return std::nullopt;
}

} // namespace pal
