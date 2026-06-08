#include "mth/net/deathlink.hpp"

#include <nlohmann/json.hpp>

namespace mth::net
{

std::string make_deathlink_payload(const std::string &source, const std::string &cause, double time_epoch_s)
{
    nlohmann::json j;
    j["time"] = time_epoch_s;
    j["source"] = source;
    if (!cause.empty())
        j["cause"] = cause;
    return j.dump();
}

std::optional<DeathLinkBounce> parse_deathlink_payload(const std::string &json_text)
{
    try
    {
        const auto j = nlohmann::json::parse(json_text);
        if (!j.is_object())
            return std::nullopt;
        DeathLinkBounce out;
        if (auto it = j.find("source"); it != j.end() && it->is_string())
            out.source = it->get<std::string>();
        if (auto it = j.find("cause"); it != j.end() && it->is_string())
            out.cause = it->get<std::string>();
        return out;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

} // namespace mth::net
