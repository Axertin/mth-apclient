#include "mth/core/modifier_config.hpp"

#include <cctype>
#include <sstream>

namespace mth
{

namespace
{

std::string trim(const std::string &s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

} // namespace

ModifierRequest parse_modifier_indices(const std::string &csv)
{
    ModifierRequest req;
    std::set<int> seen;
    std::stringstream ss(csv);
    std::string raw;
    while (std::getline(ss, raw, ','))
    {
        std::string tok = trim(raw);
        bool forced = false;
        if (tok.rfind("force:", 0) == 0)
        {
            forced = true;
            tok = trim(tok.substr(6));
        }
        try
        {
            std::size_t pos = 0;
            const int n = std::stoi(tok, &pos);
            if (pos != tok.size())
                continue; // trailing junk like "7abc"
            if (n < 0 || n >= kCheatCount)
                continue;
            if (seen.insert(n).second)
                req.indices.push_back(n);
            if (forced)
                req.forced.insert(n);
        }
        catch (...)
        {
            // non-numeric token: skip
        }
    }
    return req;
}

} // namespace mth
