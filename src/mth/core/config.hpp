#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>

#include "mth/core/modifier_config.hpp"

namespace mth
{

// Startup configuration parsed once from MTHAP_* environment variables (the offline
// test and headless-connect seams). App acts on the result.
struct Config
{
    std::string remove_locks_csv;                // MTHAP_REMOVE_LOCKS: csv of lock slots to pre-open
    ModifierRequest modifiers;                   // MTHAP_MODIFIERS
    bool modifiers_from_env{false};              // set => enforce modifiers without an AP connection (test)
    std::optional<std::array<int, 3>> stat_caps; // MTHAP_STAT_CAPS "a,d,s"; nullopt if unset/malformed
    std::optional<int> mock_ap_max_idx;          // MTHAP_MOCK_AP; <2 or non-numeric -> 1024
    bool deathlink{false};                       // MTHAP_DEATHLINK nonzero
    std::string ap_server;                       // MTHAP_AP_SERVER; empty = net idle
    std::string ap_slot{"Player1"};              // MTHAP_AP_SLOT
    std::string ap_password;                     // MTHAP_AP_PASSWORD
};

// getter(name) returns the variable's value or nullptr (std::getenv-shaped, injectable for tests).
using EnvGetter = std::function<const char *(const char *)>;

[[nodiscard]] Config parse_config(const EnvGetter &getter);
[[nodiscard]] Config load_config_from_env(); // parse_config over std::getenv

} // namespace mth
