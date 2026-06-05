#include <cstdlib>
#include <fstream>
#include <string>

#include "pal/pal_cert.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <wincrypt.h>

namespace fs = std::filesystem;

namespace
{

std::string base64(const unsigned char *data, std::size_t len)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= len; i += 3)
    {
        const unsigned n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back(tbl[n & 63]);
    }
    if (len - i == 1)
    {
        const unsigned n = data[i] << 16;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.append("==");
    }
    else if (len - i == 2)
    {
        const unsigned n = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

fs::path out_path()
{
    if (const char *appdata = std::getenv("LOCALAPPDATA"); appdata && *appdata)
        return fs::path(appdata) / "mth-apclient" / "system-ca.pem";
    return fs::temp_directory_path() / "mth-apclient" / "system-ca.pem";
}

} // namespace

namespace pal
{

std::optional<fs::path> ca_bundle_path()
{
    if (const char *o = std::getenv("MTHAP_AP_CERT"); o && *o)
    {
        std::error_code ec;
        if (fs::exists(o, ec))
            return fs::path(o);
    }

    // Materialized once (function-local static); MTHAP_AP_CERT override remains per-call.
    static const std::optional<fs::path> cached = []() -> std::optional<fs::path>
    {
        HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
        if (!store)
            return std::nullopt;

        const fs::path out = out_path();
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            CertCloseStore(store, 0);
            return std::nullopt;
        }

        int count = 0;
        PCCERT_CONTEXT ctx = nullptr;
        while ((ctx = CertEnumCertificatesInStore(store, ctx)) != nullptr)
        {
            const std::string body = base64(ctx->pbCertEncoded, ctx->cbCertEncoded);
            f << "-----BEGIN CERTIFICATE-----\n";
            for (std::size_t i = 0; i < body.size(); i += 64)
                f << body.substr(i, 64) << "\n";
            f << "-----END CERTIFICATE-----\n";
            ++count;
        }
        CertCloseStore(store, 0);
        f.flush();
        const bool ok = f.good();
        f.close();

        if (count > 0 && ok)
            return out;
        return std::nullopt;
    }();
    return cached;
}

} // namespace pal
