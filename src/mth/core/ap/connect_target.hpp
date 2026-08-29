#pragma once

#include <string>
#include <string_view>

namespace mth
{

// How a login target expects to reach the server. apclientpp arms its own wss/ws alternation only when the URI it
// receives carries no scheme (it looks for "://" and sets _tryWSS), so writing one in is what takes the fallback away.
enum class TlsMode
{
    Off,       // the caller pinned ws://, or the host is loopback: never encrypted, no bundle needed
    Preferred, // schemeless: wss is tried first and a failed handshake retries in the clear
    Pinned,    // the caller pinned wss://: nothing to fall back to, so a missing CA bundle is fatal
};

struct ConnectTarget
{
    std::string uri; // handed to APClient verbatim
    TlsMode tls;
};

[[nodiscard]] inline ConnectTarget plan_connection(std::string_view server)
{
    if (server.starts_with("wss://"))
        return {std::string(server), TlsMode::Pinned};
    if (server.starts_with("ws://"))
        return {std::string(server), TlsMode::Off};
    // A local server almost never terminates TLS, and each rejected scheme costs a reconnect interval, so pinning the
    // loopback names keeps the dev loop from paying a few seconds on every connect.
    if (server.starts_with("localhost") || server.starts_with("127.0.0.1"))
        return {"ws://" + std::string(server), TlsMode::Off};
    return {std::string(server), TlsMode::Preferred};
}

struct CertChoice
{
    std::string cert; // passed to APClient; empty leaves wswrap on the platform's default trust store
    bool refuse;      // no way to satisfy the target, so do not open a connection at all
    bool unverified;  // TLS is wanted with nothing to verify against, and only a plaintext leg is left
};

// Resolves a plan against whatever pal::ca_bundle_path() found, empty when it found nothing.
[[nodiscard]] inline CertChoice choose_cert(TlsMode tls, std::string_view bundle)
{
    if (tls == TlsMode::Off)
        return {"", false, false};
    if (!bundle.empty())
        return {std::string(bundle), false, false};
    return {"", tls == TlsMode::Pinned, tls != TlsMode::Pinned};
}

} // namespace mth
