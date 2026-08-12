#include "mth/core/save/ap_save_bundle.hpp"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "mth/core/save/ap_save_store.hpp"
#include "pal/pal_log.hpp"

namespace mth
{
namespace
{
constexpr const char *kEntrySave = "save.ycsave";
constexpr const char *kEntryState = "ap.state";
constexpr const char *kEntryManifest = "manifest.json";
constexpr int kFormatVersion = 1;

std::optional<std::string> read_file(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
}

// The pre-container .state name interpolated the raw seed, so a seed carrying a separator would
// have escaped its directory. Refuse to build such a path rather than follow it.
std::optional<std::string> legacy_state_filename(std::string_view seed, std::string_view slot)
{
    for (std::string_view part : {seed, slot})
    {
        if (part.find('/') != std::string_view::npos || part.find('\\') != std::string_view::npos)
            return std::nullopt;
        if (part.find(':') != std::string_view::npos || part.find('\0') != std::string_view::npos)
            return std::nullopt;
        if (part == "." || part == "..")
            return std::nullopt;
    }
    return "ap_" + std::string(seed) + "_" + std::string(slot) + ".state";
}

// nullopt unless the container parses AND claims this exact raw identity, so two seeds that
// sanitize to one filename never read each other's run.
std::optional<std::vector<zip::Entry>> read_container(const std::filesystem::path &path, std::string_view seed, std::string_view slot)
{
    const auto image = read_file(path);
    if (!image)
        return std::nullopt;
    auto entries = zip::read(*image);
    if (!entries)
        return std::nullopt;

    for (const zip::Entry &e : *entries)
    {
        if (e.name != kEntryManifest)
            continue;
        const auto doc = nlohmann::json::parse(e.data, nullptr, false);
        if (doc.is_discarded() || !doc.is_object())
            return std::nullopt;
        if (doc.value("format", 0) != kFormatVersion)
            return std::nullopt;
        if (doc.value("seed", std::string{}) != seed || doc.value("slot", std::string{}) != slot)
            return std::nullopt;
        return entries;
    }
    return std::nullopt; // no manifest: not one of ours
}
} // namespace

std::string ap_bundle_filename(std::string_view seed, std::string_view slot)
{
    return "ap_" + sanitize_save_key_part(seed) + "_" + sanitize_save_key_part(slot) + ".zip";
}

ApSaveBundleStore::ApSaveBundleStore(std::filesystem::path dir, LegacyDirs legacy) : dir_(std::move(dir)), legacy_(std::move(legacy))
{
}

std::filesystem::path ApSaveBundleStore::path_for(std::string_view seed, std::string_view slot) const
{
    return dir_ / ap_bundle_filename(seed, slot);
}

void ApSaveBundleStore::ensure_loaded(std::string_view seed, std::string_view slot) const
{
    if (cache_.loaded && cache_.seed == seed && cache_.slot == slot)
        return;

    cache_ = Cache{};
    cache_.loaded = true;
    cache_.seed = std::string(seed);
    cache_.slot = std::string(slot);

    const auto path = path_for(seed, slot);
    auto entries = read_container(path, seed, slot);
    if (!entries)
    {
        // A crash between the backup rotation and the replace leaves the previous generation here.
        auto bak = path;
        bak += ".bak";
        entries = read_container(bak, seed, slot);
    }

    if (entries)
    {
        for (const zip::Entry &e : *entries)
        {
            if (e.name == kEntrySave)
                cache_.game_save = e.data;
            else if (e.name == kEntryState)
                cache_.ap_state = e.data;
        }
    }
    else if (std::filesystem::exists(path))
    {
        // Present but unreadable: keep it (persist rotates it to .bak) and fall through to the old
        // layout rather than treating the run as absent.
        pal::logf(pal::LogLevel::Warn, "save: container %s is unreadable or belongs to another session; falling back to the previous layout",
                  path.string().c_str());
    }

    // Per entry, so a container holding only one payload still picks the other up from the old layout.
    if (!cache_.game_save)
    {
        if (auto blob = read_file(legacy_.ycsave_dir / ap_save_filename(seed, slot)); blob && looks_like_save_blob(*blob))
        {
            cache_.game_save = std::move(*blob);
            pal::logf(pal::LogLevel::Info, "save: adopted the previous-layout game save for seed=%s slot=%s", cache_.seed.c_str(), cache_.slot.c_str());
        }
    }
    if (!cache_.ap_state)
    {
        if (const auto name = legacy_state_filename(seed, slot))
        {
            if (auto text = read_file(legacy_.state_dir / *name))
            {
                cache_.ap_state = std::move(*text);
                pal::logf(pal::LogLevel::Info, "save: adopted the previous-layout AP state for seed=%s slot=%s", cache_.seed.c_str(), cache_.slot.c_str());
            }
        }
    }
}

bool ApSaveBundleStore::persist() const
{
    std::error_code ec;
    if (!dir_ready_)
    {
        std::filesystem::create_directories(dir_, ec);
        if (ec)
        {
            pal::logf(pal::LogLevel::Error, "save: cannot create %s: %s", dir_.string().c_str(), ec.message().c_str());
            return false;
        }
        dir_ready_ = true;
    }

    nlohmann::json manifest;
    manifest["format"] = kFormatVersion;
    manifest["seed"] = cache_.seed;
    manifest["slot"] = cache_.slot;

    std::vector<zip::Blob> blobs;
    blobs.push_back(zip::compress(kEntryManifest, manifest.dump()));

    // Re-emit an unchanged entry from its cached compressed form: a state write must not pay deflate
    // on the save blob, which is far larger and rewritten far less often. The cache is only valid
    // because every mutator erases its own entry before persisting; the size check is a cheap guard
    // against a future one that forgets.
    const auto emit = [this, &blobs](const char *name, const std::optional<std::string> &value)
    {
        if (!value)
            return;
        const auto it = cache_.blobs.find(name);
        if (it != cache_.blobs.end() && it->second.uncompressed_size == value->size())
        {
            blobs.push_back(it->second);
            return;
        }
        auto blob = zip::compress(name, *value);
        cache_.blobs[name] = blob;
        blobs.push_back(std::move(blob));
    };
    emit(kEntrySave, cache_.game_save);
    emit(kEntryState, cache_.ap_state);

    // Dropping a payload we are holding is always a bug, never an intended state.
    const std::size_t expected = 1 + (cache_.game_save ? 1u : 0u) + (cache_.ap_state ? 1u : 0u);
    if (blobs.size() != expected)
    {
        pal::logf(pal::LogLevel::Error, "save: refusing to write a container with %zu of %zu entries", blobs.size(), expected);
        return false;
    }

    const std::string image = zip::write(blobs);
    const auto final_path = path_for(cache_.seed, cache_.slot);
    auto tmp_path = final_path;
    tmp_path += ".tmp";
    auto bak_path = final_path;
    bak_path += ".bak";

    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            pal::logf(pal::LogLevel::Error, "save: cannot open %s for writing", tmp_path.string().c_str());
            return false;
        }
        out.write(image.data(), static_cast<std::streamsize>(image.size()));
        if (!out)
        {
            pal::logf(pal::LogLevel::Error, "save: short write to %s", tmp_path.string().c_str());
            return false;
        }
    }

    // Rotate before the replace: a crash in between leaves a good .bak and no container, which the
    // load path recovers from.
    if (std::filesystem::exists(final_path, ec))
    {
        std::filesystem::rename(final_path, bak_path, ec);
        ec.clear(); // a failed rotation must not block the write
    }

    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec)
    {
        pal::logf(pal::LogLevel::Error, "save: cannot replace %s: %s", final_path.string().c_str(), ec.message().c_str());
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    return true;
}

std::optional<std::string> ApSaveBundleStore::load(std::string_view seed, std::string_view slot) const
{
    ensure_loaded(seed, slot);
    return cache_.game_save;
}

bool ApSaveBundleStore::store(std::string_view seed, std::string_view slot, std::string_view blob)
{
    if (!looks_like_save_blob(blob))
        return false;
    ensure_loaded(seed, slot);
    cache_.game_save = std::string(blob);
    cache_.blobs.erase(kEntrySave);
    return persist();
}

std::optional<std::string> ApSaveBundleStore::load_state(std::string_view seed, std::string_view slot) const
{
    ensure_loaded(seed, slot);
    return cache_.ap_state;
}

bool ApSaveBundleStore::store_state(std::string_view seed, std::string_view slot, std::string_view text)
{
    ensure_loaded(seed, slot);
    cache_.ap_state = std::string(text);
    cache_.blobs.erase(kEntryState);
    return persist();
}

} // namespace mth
