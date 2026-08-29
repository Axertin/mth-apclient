#include <array>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "mth/core/ap/connect_target.hpp"

using mth::choose_cert;
using mth::ConnectTarget;
using mth::plan_connection;
using mth::TlsMode;

namespace
{
// Hosts a player might reasonably type. None carry a scheme; the scheme cases are built from these.
constexpr std::array<std::string_view, 5> kHosts{
    "archipelago.gg:38281", "archipelago.gg", "ap.example.org:12345", "192.0.2.7:38281", "localhost:38281",
};
} // namespace

TEST_CASE("plan_connection: a typed host stays schemeless, which is what arms apclientpp's fallback", "[mth][net][tls]")
{
    const ConnectTarget t = plan_connection("archipelago.gg:38281");

    // apclientpp only alternates wss/ws when it finds no "://" in the URI, so the absence of a scheme
    // is the property under test, not the particular string.
    CHECK(t.uri.find("://") == std::string::npos);
    CHECK(t.tls == TlsMode::Preferred);
}

TEST_CASE("plan_connection: an explicit scheme decides encryption and gives up the fallback", "[mth][net][tls]")
{
    for (const auto host : kHosts)
    {
        CHECK(plan_connection("wss://" + std::string(host)).tls == TlsMode::Pinned);
        CHECK(plan_connection("ws://" + std::string(host)).tls == TlsMode::Off);
    }
}

TEST_CASE("plan_connection: loopback is never encrypted, so a local server costs no rejected handshake", "[mth][net][tls]")
{
    for (const auto host : {"localhost:38281", "localhost", "127.0.0.1:38281"})
    {
        const ConnectTarget t = plan_connection(host);
        CHECK(t.tls == TlsMode::Off);
        CHECK(t.uri.starts_with("ws://"));
    }
}

TEST_CASE("plan_connection: replanning its own URI changes nothing", "[mth][net][tls]")
{
    // Catches a scheme being written into the URI again: that would leave the second pass with a
    // pinned target where the first had a fallback.
    for (const auto host : kHosts)
    {
        for (const std::string &in : {std::string(host), "ws://" + std::string(host), "wss://" + std::string(host)})
        {
            const ConnectTarget once = plan_connection(in);
            const ConnectTarget twice = plan_connection(once.uri);
            CHECK(twice.uri == once.uri);
            CHECK(twice.tls == once.tls);
        }
    }
}

TEST_CASE("choose_cert: an unencrypted target never carries a bundle", "[mth][net][tls]")
{
    for (const std::string_view bundle : {"", "/etc/ssl/certs/ca-certificates.crt"})
    {
        const mth::CertChoice c = choose_cert(TlsMode::Off, bundle);
        CHECK(c.cert.empty());
        CHECK_FALSE(c.refuse);
        CHECK_FALSE(c.unverified);
    }
}

TEST_CASE("choose_cert: an encrypted target verifies against the bundle it was given", "[mth][net][tls]")
{
    const std::string_view bundle = "/etc/ssl/certs/ca-certificates.crt";
    for (const TlsMode tls : {TlsMode::Preferred, TlsMode::Pinned})
    {
        const mth::CertChoice c = choose_cert(tls, bundle);
        CHECK(c.cert == bundle);
        CHECK_FALSE(c.refuse);
        CHECK_FALSE(c.unverified);
    }
}

TEST_CASE("choose_cert: a missing bundle only refuses the target that has nothing left to try", "[mth][net][tls]")
{
    const mth::CertChoice pinned = choose_cert(TlsMode::Pinned, "");
    CHECK(pinned.refuse);

    const mth::CertChoice preferred = choose_cert(TlsMode::Preferred, "");
    CHECK_FALSE(preferred.refuse);
    CHECK(preferred.unverified);
}

TEST_CASE("choose_cert: refusing and connecting unverified are mutually exclusive", "[mth][net][tls]")
{
    for (const TlsMode tls : {TlsMode::Off, TlsMode::Preferred, TlsMode::Pinned})
    {
        for (const std::string_view bundle : {"", "/etc/ssl/certs/ca-certificates.crt"})
        {
            const mth::CertChoice c = choose_cert(tls, bundle);
            CHECK_FALSE((c.refuse && c.unverified));
            // The bundle is either used as given or not used; nothing else may reach APClient.
            CHECK((c.cert.empty() || c.cert == bundle));
        }
    }
}
