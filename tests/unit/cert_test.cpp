#include <cstdlib>

#include <catch2/catch_test_macros.hpp>

#include "pal/pal_cert.hpp"

TEST_CASE("ca_bundle_path: honors MTHAP_AP_CERT when the file exists", "[pal][cert]")
{
    // This source file exists, so use it as a stand-in "bundle".
    setenv("MTHAP_AP_CERT", __FILE__, 1);
    auto p = pal::ca_bundle_path();
    REQUIRE(p.has_value());
    REQUIRE(p->string() == __FILE__);
    unsetenv("MTHAP_AP_CERT");
}

TEST_CASE("ca_bundle_path: finds a system bundle on this host", "[pal][cert]")
{
    unsetenv("MTHAP_AP_CERT");
    unsetenv("SSL_CERT_FILE");
    auto p = pal::ca_bundle_path();
    // CI/dev Linux always ships /etc/ssl/certs/ca-certificates.crt.
    REQUIRE(p.has_value());
}
